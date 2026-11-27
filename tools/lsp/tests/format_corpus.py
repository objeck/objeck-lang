#!/usr/bin/env python3
"""Formats every program under programs/examples and programs/regression with both formatters --
the language server's (tools/lsp/server/format_code) and the library's System.Utility.CodeFormatter
(misc.obl) -- and checks three things of each program that compiles:

  (a) the formatter finishes and returns text
  (b) the formatted text is the same program: it has the same tokens, it compiles, and its image
      (the .obe, inflated) is the original's byte for byte once the line and column numbers an
      image names are set aside
  (c) formatting the formatted text again changes nothing

A program that does not compile (the regression tests that must not) is still formatted: (a) and
(c) are required of it too.

  python3 tools/lsp/tests/format_corpus.py <deploy bin dir> [--work DIR] [--jobs N] [--only TEXT]

<deploy bin dir> holds obc and obr, with the libraries in ../lib (core/release/deploy/bin after a
build). Prints the totals and the failures grouped by cause, and exits 1 if there is any. The work
directory (default: a new temporary one) gets results.json and, for each failing program, the
original, the formatted text and the text formatted twice.

How (b) is decided. The token list is made here, from the compiler's rules (core/compiler/
scanner.cpp), and shares nothing with either formatter's scanner: the same list means only layout
and the places of comments differ. The image is the stronger check where it applies, and it is
reported which of the two settled each program: an image keeps the line and column of an anonymous
class, an assert and a spawn, and the source text of an assert's expression, and those move when
the layout does.
"""
import concurrent.futures
import glob
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..', '..'))
BS = chr(92)

OPS = ['>>>', '?->', ':=', '->', '--', '-=', '!=', '&&', '||', '??', '==', '=>', '<>', '<=', '<<', '>=', '>>', '+=', '++', '*=', '/=']
SPECIAL = set(chr(c) for c in (0x2192, 0x2190, 0x2260, 0x2265, 0x2264))
BLANK = set([' ', chr(9), chr(13), chr(10), chr(0xfeff)])


def tokens(text):
    """The program's tokens as the compiler's scanner reads them, comments left out."""
    out = []
    i = 0
    n = len(text)

    def word(c):
        return c.isalpha() or c.isdigit() or c in '_@$' or (ord(c) > 127 and c not in SPECIAL)

    while i < n:
        c = text[i]
        if c in BLANK:
            i += 1
        elif c == '#':
            if text[i + 1:i + 2] == '~':
                j = text.find('~#', i + 2)
                i = n if j < 0 else j + 2
            else:
                j = text.find(chr(10), i)
                i = n if j < 0 else j
        elif c == '"' or c == "'" or (c == '$' and text[i + 1:i + 2] == '"'):
            j = i + 1
            if c == '$':
                c = '"'
                j += 1
            while j < n and text[j] != c:
                j += 2 if text[j] == BS else 1
            if j >= n:
                # the text ends inside the literal: the blank space at the end of a file is not kept
                out.append(text[i:].rstrip())
            else:
                out.append(text[i:j + 1])
            i = j + 1
        elif c.isdigit() or (c == '.' and text[i + 1:i + 2].isdigit()):
            j = i
            flt = False
            pre = False
            exp = False
            while j < n:
                d = text[j]
                if d in 'uU':
                    j += 1
                    break
                if exp and d in '+-' and text[j + 1:j + 2].isdigit():
                    exp = False
                    j += 1
                    continue
                if d.isdigit() or d == '_' or d in 'abcdefABCDEF':
                    exp = flt and not pre and d in 'eE'
                    j += 1
                    continue
                if d == '.':
                    if flt or pre:
                        break
                    flt = True
                    exp = False
                    j += 1
                    continue
                if j == i + 1 and d in 'xXoO' and text[i] == '0':
                    pre = True
                    j += 1
                    continue
                break
            out.append(text[i:j])
            i = j
        elif word(c):
            j = i
            while j < n and word(text[j]):
                j += 1
            out.append(text[i:j])
            i = j
        else:
            for op in OPS:
                if text.startswith(op, i):
                    out.append(op)
                    i += len(op)
                    break
            else:
                out.append(c)
                i += 1
    # '>>' and '>>>' close type arguments as well as shift, and the parser takes them apart: one '>' each
    flat = []
    for t in out:
        if t in ('>>', '>>>'):
            flat += ['>'] * len(t)
        else:
            flat.append(t)
    return flat


def normal(img):
    """an image with the positions it names taken out: anonymous classes (file_line_column), assert and spawn sites"""
    img = re.sub(rb'prog_[0-9]+_[0-9]+', b'prog_L_C', img)
    return re.sub(rb'prog[.]obs:[0-9]+', b'prog.obs:L', img)


def lib_sets(src_path, text):
    """library lists to try, most specific first"""
    sets = []
    m = re.search(r'^# EXTRA_LIBS:[ ]*([^ \r\n]+)', text, re.M)
    base = 'cipher,collect,xml,json'
    if '/regression/' in src_path.replace(os.sep, '/'):
        sets.append(base + (',' + m.group(1) if m else ''))
    else:
        if m:
            sets.append(base + ',' + m.group(1))
        sets += ['collect', base, base + ',net,misc,regex,csv,net_server',
                 base + ',net,misc,regex,csv,net_server,query,ml,math,concurrent,actor',
                 base + ',sdl2,sdl_game,sdl_gl', base + ',net,misc,opencv,onnx,models',
                 base + ',net,misc,json_rpc,mcp,agent,net_server',
                 base + ',net,misc,ollama,openai,gemini,anthropic,net_server', base + ',net,net_server,web_server,misc',
                 base + ',net,net_server,web_server,misc,sdl2,sdl_game,concurrent',
                 base + ',net,net_server,web_server,misc,mcp_agent,mcp,agent,json_rpc,sdl2,sdl_game,regex,llm_util,anthropic,openai,gemini,ollama',
                 base + ',net,net_server,web_server,misc,rag_adapters,rag,ml_vector,ml,openai,gemini,ollama,anthropic,models,opencv,onnx,'
                        'nlp,nlp_onnx,ml_verify,ml_clip,csv,regex',
                 base + ',telemetry,net,misc,models,opencv,onnx,concurrent']
    return sets


class Corpus:
    def __init__(self, bin_dir, work, jobs):
        exe = '.exe' if os.name == 'nt' else ''
        self.obc = os.path.join(bin_dir, 'obc' + exe)
        self.obr = os.path.join(bin_dir, 'obr' + exe)
        self.env = dict(os.environ, OBJECK_LIB_PATH=os.path.abspath(os.path.join(bin_dir, '..', 'lib')))
        self.work = work
        self.jobs = jobs
        self.drivers = {}

    def run(self, cmd, timeout=60, cwd=None):
        try:
            p = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, env=self.env, timeout=timeout, cwd=cwd)
            return p.returncode, p.stdout.decode('utf-8', 'replace')
        except subprocess.TimeoutExpired:
            return 124, 'timeout'

    def build_drivers(self):
        fmt = os.path.join(ROOT, 'tools', 'lsp', 'server', 'format_code')
        builds = (('lsp', [os.path.join(HERE, 'format_corpus_lsp.obs'), os.path.join(fmt, 'formatter.obs'), os.path.join(fmt, 'scanner.obs')], 'gen_collect'),
                  ('lib', [os.path.join(HERE, 'format_corpus_lib.obs')], 'gen_collect,net,json,cipher,misc'))
        for which, srcs, libs in builds:
            dest = os.path.join(self.work, 'format_' + which + '.obe')
            rc, out = self.run([self.obc, '-src', ','.join(srcs), '-lib', libs, '-dest', dest], timeout=300)
            if rc != 0 or not os.path.exists(dest):
                print('could not build the %s driver:' % which)
                print(out)
                return False
            self.drivers[which] = dest
        return True

    def compile_obs(self, src, dest, libs):
        # compiled from inside the source's directory under one fixed name, so that neither the path
        # nor the file's name can make two images differ
        rc, out = self.run([self.obc, '-src', os.path.basename(src), '-lib', libs, '-opt', 's3', '-dest', dest], cwd=os.path.dirname(src))
        return rc == 0 and os.path.exists(dest), out

    def image(self, path):
        try:
            return zlib.decompress(open(path, 'rb').read()[4:])
        except Exception:
            return None

    def one(self, src):
        rel = os.path.relpath(src, ROOT).replace(os.sep, '/')
        d = os.path.join(self.work, rel.replace('/', '__')[:-4])
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(os.path.join(d, 'orig'))
        text = open(src, 'r', encoding='utf-8', errors='replace').read()
        res = {'src': rel, 'compiles': False, 'fmt': {}}
        o_src = os.path.join(d, 'orig', 'prog.obs')
        shutil.copyfile(src, o_src)
        libs = None
        if not re.search(r'^# EXPECT_COMPILE_ERROR', text, re.M):
            for ls in lib_sets(src, text):
                ok, out = self.compile_obs(o_src, os.path.join(d, 'orig', 'prog.obe'), ls)
                if ok:
                    libs = ls
                    break
        res['compiles'] = libs is not None
        want = self.image(os.path.join(d, 'orig', 'prog.obe')) if libs else None
        otoks = tokens(text)
        for which in ('lsp', 'lib'):
            r = {'a': False, 'b': False, 'c': False, 'why': ''}
            res['fmt'][which] = r
            fd = os.path.join(d, which)
            os.makedirs(fd)
            f1 = os.path.join(fd, 'prog.obs')
            rc, out = self.run([self.obr, self.drivers[which], o_src, f1])
            if rc != 0 or not os.path.exists(f1) or (os.path.getsize(f1) == 0 and text.strip()):
                r['why'] = 'formatter: exit %d %s' % (rc, ' '.join(out.split())[:160])
                continue
            r['a'] = True
            ftext = open(f1, 'r', encoding='utf-8', errors='replace').read()
            same_tokens = tokens(ftext) == otoks
            if libs is None:
                # nothing to compile: the tokens are all there is to compare
                r['b'] = same_tokens
                r['how'] = 'same tokens (the program does not compile)'
                if not same_tokens:
                    r['why'] = 'tokens differ'
            else:
                ok, out = self.compile_obs(f1, os.path.join(fd, 'prog.obe'), libs)
                got = self.image(os.path.join(fd, 'prog.obe')) if ok else None
                if not ok:
                    r['why'] = 'formatted text does not compile: ' + ' '.join(out.split())[:200]
                elif got is not None and same_tokens and got == want:
                    r['b'] = True
                    r['how'] = 'identical image'
                elif got is not None and same_tokens and normal(got) == normal(want):
                    r['b'] = True
                    r['how'] = 'identical image once line and column numbers are set aside'
                elif got is not None and same_tokens:
                    # what is left differs by text the image keeps from the source (an assert's expression) or by
                    # a position whose digits changed its length; the tokens are the same, so only layout moved
                    r['b'] = True
                    r['how'] = 'same tokens (the image keeps source text or positions)'
                else:
                    r['why'] = 'image differs (%s vs %s bytes)%s' % (len(want) if want else '?', len(got) if got else '?',
                                                                    '' if same_tokens else ', tokens differ')
            f2 = os.path.join(fd, 'again.obs')
            rc, out = self.run([self.obr, self.drivers[which], f1, f2])
            if rc == 0 and os.path.exists(f2) and open(f1, 'rb').read() == open(f2, 'rb').read():
                r['c'] = True
            else:
                r['why'] = (r['why'] + '; ' if r['why'] else '') + 'not idempotent'
        if all(r['a'] and r['b'] and r['c'] for r in res['fmt'].values()):
            shutil.rmtree(d, ignore_errors=True)
        return res


def main():
    args = sys.argv[1:]
    if not args or args[0].startswith('-'):
        print(__doc__)
        return 2
    bin_dir = os.path.abspath(args[0])
    work = None
    jobs = os.cpu_count() or 4
    only = None
    rest = args[1:]
    while rest:
        if rest[0] == '--jobs':
            jobs = int(rest[1])
            rest = rest[2:]
        elif rest[0] == '--only':
            only = rest[1]
            rest = rest[2:]
        elif rest[0] == '--work':
            work = os.path.abspath(rest[1])
            rest = rest[2:]
        else:
            print('unknown option ' + rest[0])
            return 2
    if work is None:
        work = tempfile.mkdtemp(prefix='format_corpus_')
    os.makedirs(work, exist_ok=True)

    corpus = Corpus(bin_dir, work, jobs)
    if not corpus.build_drivers():
        return 2
    srcs = sorted(glob.glob(os.path.join(ROOT, 'programs', 'examples', '*.obs')) + glob.glob(os.path.join(ROOT, 'programs', 'regression', '*.obs')))
    if only:
        srcs = [s for s in srcs if only in s.replace(os.sep, '/')]
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as ex:
        results = list(ex.map(corpus.one, srcs))
    json.dump(results, open(os.path.join(work, 'results.json'), 'w'), indent=1)

    comp = [r for r in results if r['compiles']]
    rest_ = [r for r in results if not r['compiles']]
    print('%d sources: %d compile, %d do not (tests that must not compile, or libraries not found)' % (len(results), len(comp), len(rest_)))
    bad = 0
    for which, title in (('lsp', 'language server formatter'), ('lib', 'System.Utility.CodeFormatter')):
        for group, name in ((comp, 'programs that compile'), (rest_, 'programs that do not compile')):
            if not group:
                continue
            na = sum(1 for r in group if r['fmt'][which]['a'])
            nb = sum(1 for r in group if r['fmt'][which]['b'])
            nc = sum(1 for r in group if r['fmt'][which]['c'])
            nall = sum(1 for r in group if all(r['fmt'][which][k] for k in 'abc'))
            print('%s, %s: (a) formatted %d, (b) same program %d, (c) idempotent %d; all three %d of %d' % (title, name, na, nb, nc, nall, len(group)))
            hows = {}
            for r in group:
                h = r['fmt'][which].get('how')
                if h and r['fmt'][which]['b']:
                    hows[h] = hows.get(h, 0) + 1
            for h, k in sorted(hows.items(), key=lambda kv: -kv[1]):
                print('     (b) by %s: %d' % (h, k))
            bad += len(group) - nall
            why = {}
            for r in group:
                f = r['fmt'][which]
                if not all(f[k] for k in 'abc'):
                    why.setdefault(re.sub(r'[0-9]+', 'N', f['why'])[:70], []).append(r['src'])
            for k, v in sorted(why.items(), key=lambda kv: -len(kv[1]))[:25]:
                print('   %4d  %s   e.g. %s' % (len(v), k, ', '.join(os.path.basename(x) for x in v[:3])))
    if bad:
        print('failing programs are kept in ' + work)
    else:
        shutil.rmtree(work, ignore_errors=True)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
