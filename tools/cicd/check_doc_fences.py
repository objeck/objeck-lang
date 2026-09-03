#!/usr/bin/env python3
"""Fail when a library doc comment puts a code fence anywhere but last.

`core/lib/code_doc/doc_parser.obs` accepts a doc comment in exactly one order:

    #~
    description
    @param / @return tags
    ```
    example
    ```
    ~#

The fence is parsed last, and the parser then requires the comment to end. If
anything follows it -- another paragraph, a `##` heading, or a `@param` written
after the example -- `MatchCommentEnd()` fails and the parser returns false for
the whole member.

Nothing reports that. The build exits 0, the page is still generated, and the
member is simply absent from the API reference. Found 2026-09-03 while adding
`TCPSocket->CloseGracefully()`, which documented itself on the TLS twin and
vanished on the plain one for exactly this reason. The same sweep found 19 more
in `sdl_gl.obs`, including `GL->GetLastError()`, `Mesh->GetRadius()` and
`Texture->Checker()` -- all missing from the shipped v2026.8.4 reference, along
with the truncated tail of a dozen class descriptions.

This is the same failure family as the hard-coded library lists that
`check_doc_lists.py` guards: the doc build reports success while silently
dropping content, so the only way to know is to check the real property.

Usage:
    check_doc_fences.py [paths...]      # defaults to core/compiler/lib_src/*.obs

Exit 0 when every fenced doc comment ends with its fence, 1 otherwise.
"""

import glob
import io
import os
import re
import sys

COMMENT = re.compile(r'#~(.*?)~#', re.S)


def offenders(path):
    """Yield (line, trailing_text, following_declaration) for each bad comment."""
    text = io.open(path, encoding='utf-8', errors='replace').read()
    for match in COMMENT.finditer(text):
        body = match.group(1)
        if '```' not in body:
            continue

        trailing = body[body.rindex('```') + 3:]
        if not trailing.strip():
            continue

        line = text.count('\n', 0, match.start()) + 1
        following = text[match.end():match.end() + 200].strip().split('\n')[0].strip()
        yield line, trailing.strip().split('\n')[0], following


def main(argv):
    paths = argv[1:] or sorted(glob.glob(
        os.path.join('core', 'compiler', 'lib_src', '*.obs')))
    if not paths:
        print('no sources to check', file=sys.stderr)
        return 1

    total = 0
    for path in paths:
        for line, trailing, following in offenders(path):
            total += 1
            print('%s:%d: content after the code fence is discarded' % (path, line))
            print('    first dropped line: %s' % trailing[:90])
            print('    declaration:        %s' % following[:90])

    if total:
        print()
        print('%d doc comment(s) put a code fence before other content.' % total)
        print('Move the ``` block to the end, after the @param/@return tags, so the')
        print('comment ends with it -- everything after a fence is silently dropped,')
        print('and for a method or function that drops the member from the API docs.')
        return 1

    print('%d source file(s): every fenced doc comment ends with its fence.' % len(paths))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
