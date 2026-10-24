"""A small lexical pass over Objeck source.

The playground's checks (which libraries does this program use, does it touch
something the sandbox forbids) must not be fooled by a comment, and must not
fire on a `#` inside a string. A regex over raw text cannot tell the difference,
so this walks the text once and returns two views of it:

  * `code`     the source with comments blanked out (strings kept, so a blocked
               call hidden inside a `{$ ... }` interpolation is still seen), and
               every newline preserved so offsets still map to line numbers;
  * `strings`  the contents of each string literal, with its line.

It is deliberately not a parser. Anything it misreads errs toward keeping more
text in `code`, which can only make a check stricter, never blind.
"""

from dataclasses import dataclass


@dataclass(frozen=True)
class Scanned:
    code: str
    strings: tuple[tuple[int, str], ...]  # (1-based line, literal contents)

    def line_of(self, offset: int) -> int:
        return self.code.count("\n", 0, offset) + 1


def scan(source: str, blank_literals: bool = False) -> Scanned:
    """`blank_literals` also replaces the inside of strings and character literals
    with spaces (newlines kept), for callers that count braces."""
    out: list[str] = []
    strings: list[tuple[int, str]] = []
    i, n, line = 0, len(source), 1

    while i < n:
        c = source[i]

        if c == "#":
            if source.startswith("#~", i):
                end = source.find("~#", i + 2)
                end = n if end < 0 else end + 2
            else:
                end = source.find("\n", i)
                end = n if end < 0 else end
            chunk = source[i:end]
            out.append("".join("\n" if ch == "\n" else " " for ch in chunk))
            line += chunk.count("\n")
            i = end
            continue

        if c == '"':
            j = i + 1
            while j < n:
                if source[j] == "\\":
                    j += 2
                    continue
                if source[j] == '"':
                    break
                j += 1
            end = min(j + 1, n)
            chunk = source[i:end]
            strings.append((line, source[i + 1:j]))
            if blank_literals:
                chunk = '"' + "".join("\n" if ch == "\n" else " " for ch in source[i + 1:j]) + '"'
            out.append(chunk)
            line += chunk.count("\n")
            i = end
            continue

        if c == "'":
            # a character literal: 'a' or '\n'. Anything else is a stray quote.
            if i + 2 < n and source[i + 1] != "\\" and source[i + 2] == "'":
                out.append("' '" if blank_literals else source[i:i + 3])
                i += 3
                continue
            if i + 3 < n and source[i + 1] == "\\" and source[i + 3] == "'":
                out.append("'  '" if blank_literals else source[i:i + 4])
                i += 4
                continue

        if c == "\n":
            line += 1
        out.append(c)
        i += 1

    return Scanned("".join(out), tuple(strings))
