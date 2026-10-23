#!/usr/bin/env python3
"""Print the layout of an .otab table file, byte by byte.

An independent reader of the format, written from docs/OTAB_FORMAT.md rather than from the
library, so it doubles as a check that the two agree: every byte of a well formed file is
accounted for, every checksum is verified, and anything else is reported as an error.

usage: otab_dump.py FILE [--rows N]

  --rows N   decoded values to show per block (default 8; 0 shows none)

Exit status is 0 for a well formed file and 1 otherwise.
"""
import argparse
import struct
import sys
import zlib

MAGIC = b'OTAB'
VERSION = 1
HEADER_SIZE = 12
TRAILER_SIZE = 28
TYPES = {1: 'INT', 2: 'FLOAT', 3: 'STRING', 4: 'DATE', 5: 'BOOL', 6: 'OBJECT'}
CODECS = {0: 'none', 1: 'zlib level 1', 2: 'zlib level 9'}


class Malformed(Exception):
    pass


class Reader:
    """Little-endian fields from a byte string; a read past the end raises Malformed."""

    def __init__(self, data, what):
        self.data, self.pos, self.what = data, 0, what

    def take(self, n):
        if n < 0 or self.pos + n > len(self.data):
            raise Malformed('%s ends early (at byte %d of %d)' % (self.what, self.pos, len(self.data)))
        part = self.data[self.pos:self.pos + n]
        self.pos += n
        return part

    def int(self):
        return struct.unpack('<q', self.take(8))[0]

    def float(self):
        return struct.unpack('<d', self.take(8))[0]

    def ints(self, n):
        return list(struct.unpack('<%dq' % n, self.take(8 * n)))

    def floats(self, n):
        return list(struct.unpack('<%dd' % n, self.take(8 * n)))

    def text(self):
        size = self.int()
        if size == -1:
            return None
        if size < -1:
            raise Malformed('%s has a text of length %d' % (self.what, size))
        return self.take(size).decode('utf-8')

    def done(self):
        if self.pos != len(self.data):
            raise Malformed('%s has %d bytes left over' % (self.what, len(self.data) - self.pos))


def hexed(data, offset, size):
    shown = data[offset:offset + min(size, 16)].hex(' ')
    return shown + (' ...' if size > 16 else '')


def line(data, offset, size, note):
    print('%6d  %-52s %s' % (offset, hexed(data, offset, size), note))


def preview(values, rows):
    if len(values) <= rows:
        return repr(values)
    return repr(values[:rows])[:-1] + ', ... %d more]' % (len(values) - rows)


def is_set(bits, i):
    return (bits[i >> 6] >> (i & 63)) & 1 == 1


def dump_block(data, info, name, kind, rows, show):
    """Decode one block, printing where each field lies in the file."""
    stored = data[info['offset']:info['offset'] + info['stored']]
    if len(stored) != info['stored']:
        raise Malformed('column %s: the block runs past the end of the file' % name)
    if zlib.crc32(stored) != info['crc']:
        raise Malformed('column %s: the block is damaged (checksum mismatch)' % name)
    if info['codec'] == 0:
        raw = stored
    elif info['codec'] in (1, 2):
        try:
            raw = zlib.decompress(stored)
        except zlib.error as e:
            raise Malformed('column %s: the block cannot be decompressed (%s)' % (name, e))
    else:
        raise Malformed('column %s: unknown codec %d' % (name, info['codec']))
    if len(raw) != info['raw']:
        raise Malformed('column %s: decompressed to %d bytes, not %d' % (name, len(raw), info['raw']))

    # field offsets are only meaningful in the file when the block is stored as it is
    base = info['offset'] if info['codec'] == 0 else None
    r = Reader(raw, 'column %s block' % name)

    def field(start, note):
        if base is not None:
            line(data, base + start, r.pos - start, note)
        else:
            print('%6s  %-52s %s' % ('', '(compressed)', note))

    print('        -- column %s (%s): %d bytes stored, codec %d (%s), %d raw'
          % (name, kind, info['stored'], info['codec'], CODECS[info['codec']], info['raw']))
    at = r.pos
    words = r.int()
    field(at, 'bitmap words = %d' % words)
    if words != (rows + 63) // 64:
        raise Malformed('column %s: %d bitmap words for %d rows' % (name, words, rows))
    at = r.pos
    nulls = r.ints(words)
    empty = [is_set(nulls, i) for i in range(rows)]
    field(at, 'empty cells: %d' % sum(empty))

    at = r.pos
    if kind in ('INT', 'BOOL'):
        values = r.ints(rows)
        field(at, 'values')
    elif kind == 'FLOAT':
        values = r.floats(rows)
        field(at, 'values')
    elif kind == 'DATE':
        values = r.ints(rows)
        field(at, 'milliseconds')
        at = r.pos
        gmt = r.ints(words)
        field(at, 'GMT cells: %d' % sum(is_set(gmt, i) for i in range(rows)))
    elif kind == 'STRING':
        encoding = r.int()
        field(at, 'encoding = %d (%s)' % (encoding, {1: 'dictionary', 0: 'plain'}.get(encoding, '?')))
        if encoding == 1:
            at = r.pos
            size = r.int()
            field(at, 'dictionary size = %d' % size)
            at = r.pos
            words_list = [r.text() for _ in range(size)]
            field(at, 'dictionary %s' % preview(words_list, show))
            at = r.pos
            codes = r.ints(rows)
            field(at, 'codes')
            values = []
            for i in range(rows):
                if empty[i]:
                    values.append(None)
                elif 0 <= codes[i] < size:
                    values.append(words_list[codes[i]])
                else:
                    raise Malformed('column %s: row %d has code %d of %d' % (name, i, codes[i], size))
        elif encoding == 0:
            values = [r.text() for _ in range(rows)]
            field(at + 8, 'texts')
        else:
            raise Malformed('column %s: unknown STRING encoding %d' % (name, encoding))
    elif kind == 'OBJECT':
        values = []
        for i in range(rows):
            tag = r.int()
            if tag == 0:
                values.append(None)
            elif tag == 1:
                values.append(r.int())
            elif tag == 2:
                values.append(r.float())
            elif tag == 3:
                values.append(r.text())
            elif tag == 4:
                values.append(('date', r.int(), 'gmt' if r.int() == 1 else 'local'))
            elif tag == 5:
                values.append(r.int() == 1)
            else:
                raise Malformed('column %s: row %d has unknown cell tag %d' % (name, i, tag))
        field(at, 'tagged cells')
    r.done()

    cells = [None if empty[i] else values[i] for i in range(rows)]
    if show:
        print('%6s  %-52s => %s' % ('', '', preview(cells, show)))


def dump(data, show):
    if len(data) < HEADER_SIZE + TRAILER_SIZE:
        raise Malformed('too short to be an .otab file')
    if data[:4] != MAGIC:
        raise Malformed('not an .otab file')
    version = struct.unpack_from('<q', data, 4)[0]
    line(data, 0, 4, 'magic "OTAB"')
    line(data, 4, 8, 'format version = %d' % version)
    if not 1 <= version <= VERSION:
        raise Malformed('format version %d; this reader knows %d' % (version, VERSION))

    end = len(data) - TRAILER_SIZE
    footer_offset, footer_size, footer_crc = struct.unpack_from('<qqq', data, end)
    if data[-4:] != MAGIC or footer_offset < HEADER_SIZE or footer_size < 0 \
            or footer_offset + footer_size + TRAILER_SIZE != len(data):
        raise Malformed('the end of the file is damaged or missing; it may be truncated')
    footer = data[footer_offset:footer_offset + footer_size]
    if zlib.crc32(footer) != footer_crc:
        raise Malformed('the footer is damaged (checksum mismatch)')

    f = Reader(footer, 'footer')
    name, next_key, count = f.text(), f.int(), f.int()
    if count < 1:
        raise Malformed('the footer lists %d columns' % count)
    columns = []
    for _ in range(count):
        column_name, code, declared = f.text(), f.int(), f.int()
        if code not in TYPES:
            raise Malformed('column %s has unknown type code %d' % (column_name, code))
        columns.append((column_name, TYPES[code], declared == 1))
    groups = []
    for _ in range(f.int()):
        rows = f.int()
        blocks = []
        for _ in range(count):
            blocks.append(dict(offset=f.int(), stored=f.int(), raw=f.int(), codec=f.int(), crc=f.int(),
                               nulls=f.int(), has_zone=f.int(), low=f.float(), high=f.float()))
        groups.append((rows, blocks))
    f.done()

    for g, (rows, blocks) in enumerate(groups):
        print('        == row group %d: %d rows' % (g, rows))
        for c, info in enumerate(blocks):
            dump_block(data, info, columns[c][0], columns[c][1], rows, show)

    line(data, footer_offset, footer_size, 'footer, %d bytes' % footer_size)
    print('%6s  %-52s name %r, next key %d' % ('', '', name, next_key))
    for column_name, kind, declared in columns:
        print('%6s  %-52s column %s %s%s' % ('', '', column_name, kind, ', declared' if declared else ''))
    for g, (rows, blocks) in enumerate(groups):
        for c, info in enumerate(blocks):
            zone = ', zone %r..%r' % (info['low'], info['high']) if info['has_zone'] else ''
            print('%6s  %-52s group %d %s: offset %d, %d empty%s'
                  % ('', '', g, columns[c][0], info['offset'], info['nulls'], zone))
    line(data, end, 8, 'footer offset = %d' % footer_offset)
    line(data, end + 8, 8, 'footer size = %d' % footer_size)
    line(data, end + 16, 8, 'footer CRC32 = %d' % footer_crc)
    line(data, end + 24, 4, 'magic "OTAB"')
    print('%d rows in %d row groups, %d columns, %d bytes'
          % (sum(g[0] for g in groups), len(groups), count, len(data)))


def main():
    parser = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    parser.add_argument('file')
    parser.add_argument('--rows', type=int, default=8)
    args = parser.parse_args()
    with open(args.file, 'rb') as stream:
        data = stream.read()
    try:
        dump(data, args.rows)
    except Malformed as e:
        print('error: %s' % e, file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
