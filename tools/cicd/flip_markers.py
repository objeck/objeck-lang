#!/usr/bin/env python3
"""Flip the "current release" markers in README.md and the objeck.org pages to a newly PUBLISHED release.

    python tools/cicd/flip_markers.py <NEW> [<OLD>] [--root DIR]

NEW is the version just published (e.g. 2026.11.2). OLD is the version the markers name now; when left out it is
read from the README's release badge. Run it only AFTER the release is published: these markers put download URLs
on the front page, and flipping them before the tag exists serves a 404 (the v2026.9.1 mistake, PR #786).

What it changes, and it asserts every one of them, so a marker that has moved fails loudly instead of being skipped:
  README.md            release badge, the .deb and tarball Quick Start URLs and commands, the "what is new" link,
                       the green tick moves from OLD to NEW, "Latest Release"
  docs/web/index.html  the hero badge
  docs/web/readme.html the Download button, "is the current release", the underline moves to NEW, and NEW's notes block
                       (the v<NEW> section of docs/readme.html, written by the update-docs step) is inserted

Exit 0 and a one-line summary on success; any missing marker exits 1 with the marker named and NOTHING written.
"""
import re
import sys


def main(argv):
    root = '.'
    args = []
    i = 0
    while i < len(argv):
        if argv[i] == '--root':
            root = argv[i + 1]
            i += 2
        else:
            args.append(argv[i])
            i += 1
    if not args:
        print(__doc__)
        return 2
    new = args[0].lstrip('v')

    def path(p):
        return root.rstrip('/\\') + '/' + p

    def rd(p):
        return open(path(p), encoding='utf-8', newline='').read()

    readme = rd('README.md')
    old = args[1].lstrip('v') if len(args) > 1 else None
    if old is None:
        m = re.search(r'img\.shields\.io/badge/release-v([0-9.]+)-blue', readme)
        if not m:
            print('FAIL: cannot read the current version from the README release badge')
            return 1
        old = m.group(1)
    if old == new:
        print('nothing to do: the markers already name v%s' % new)
        return 0

    out = {}

    def rep(text, a, b, what, count=1):
        n = text.count(a)
        if n != count:
            raise SystemExit('FAIL: marker "%s" found %d time(s), expected %d: %s' % (what, n, count, a[:90]))
        return text.replace(a, b)

    # ---------------------------------------------------------------- README.md
    nl = '\r\n' if '\r\n' in readme else '\n'
    s = readme
    s = rep(s, 'img.shields.io/badge/release-v%s-blue' % old, 'img.shields.io/badge/release-v%s-blue' % new, 'release badge')
    for fmt, what in (('releases/download/v%s/objeck-ubuntu24.04-x64_%s.deb', 'deb url'),
                      ('releases/download/v%s/objeck-linux-x64_%s.tgz', 'tgz url')):
        s = rep(s, fmt % (old, old), fmt % (new, new), what)
    s = rep(s, 'sudo apt install ./objeck-ubuntu24.04-x64_%s.deb' % old, 'sudo apt install ./objeck-ubuntu24.04-x64_%s.deb' % new, 'apt command')
    s = rep(s, 'tar xzf objeck-linux-x64_%s.tgz' % old, 'tar xzf objeck-linux-x64_%s.tgz' % new, 'tar command')
    s = rep(s, 'what is new in v%s](#whats-new)' % old, 'what is new in v%s](#whats-new)' % new, 'what-is-new link')
    s = rep(s, '### v%s ✅' % old, '### v%s' % old, 'old tick')
    s = rep(s, '### v%s%s' % (new, nl), '### v%s ✅%s' % (new, nl), 'new heading (is the v%s notes section in README yet?)' % new)
    s = rep(s, '**Latest Release:** [v%s]' % old, '**Latest Release:** [v%s]' % new, 'latest release line')
    out['README.md'] = s

    # ---------------------------------------------------------------- website
    out['docs/web/index.html'] = rep(rd('docs/web/index.html'),
                                     'readme.html">v%s &middot; JIT-compiled' % old,
                                     'readme.html">v%s &middot; JIT-compiled' % new, 'hero badge')

    src = rd('docs/readme.html')
    head_new = '<h3><u><a href="https://github.com/objeck/objeck-lang/releases/tag/v%s"' % new
    lines = src.split('\n')
    start = next((k for k, l in enumerate(lines) if head_new in l), None)
    if start is None:
        raise SystemExit('FAIL: docs/readme.html has no v%s notes section (run the update-docs step first)' % new)
    end = next((k for k in range(start + 1, len(lines)) if lines[k].lstrip().startswith('<h3>')), None)
    if end is None:
        raise SystemExit('FAIL: could not find the end of the v%s notes section in docs/readme.html' % new)
    block = '\n'.join(lines[start:end])
    if not block.endswith('\n'):
        block += '\n'

    web = rd('docs/web/readme.html')
    web = rep(web, '>Download v%s</a>' % old, '>Download v%s</a>' % new, 'Download button')
    web = rep(web, 'v%s is the current release' % old, 'v%s is the current release' % new, '"is the current release"')
    old_head = ('<h3><u><a href="https://github.com/objeck/objeck-lang/releases/tag/v%s" '
                'style="color: inherit; text-decoration: none;">v%s</a></u></h3>') % (old, old)
    new_head = old_head.replace('<u>', '').replace('</u>', '')
    i = web.index(old_head) if old_head in web else -1
    if i < 0:
        raise SystemExit('FAIL: docs/web/readme.html has no underlined v%s heading' % old)
    line_start = web.rfind('\n', 0, i) + 1
    web = web[:line_start] + block + web[line_start:]
    web = rep(web, old_head, new_head, 'old heading underline')
    out['docs/web/readme.html'] = web

    # nothing is written until every marker has been found
    for p, text in out.items():
        open(path(p), 'w', encoding='utf-8', newline='').write(text)
    print('flipped v%s -> v%s in %s' % (old, new, ', '.join(out)))
    return 0


if __name__ == '__main__':
    try:
        sys.exit(main(sys.argv[1:]))
    except SystemExit as e:
        if isinstance(e.code, str):
            print(e.code)
            sys.exit(1)
        raise
