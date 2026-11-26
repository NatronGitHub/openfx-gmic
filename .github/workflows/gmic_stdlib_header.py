#!/usr/bin/env python3
"""Writes a G'MIC standard library header (gmic_stdlib_community.h) from a command file.

gmic.cpp embeds its standard library as the array data_gmic, a CImg-serialized, zlib-compressed
image of the library's text followed by a null byte. G'MIC's Makefile (target
do_gmic_stdlib_community.h) writes it with the gmic command line:
'gmic it update<version>.gmic "(0)" a y serialize uint8,1,0 o -.h'. This script writes the same
array without a G'MIC interpreter, so that one can be built from G'MIC's develop branch, whose
sources have no header (G'MIC's Makefile downloads it from gmic.eu).

usage: gmic_stdlib_header.py <command file> <output header> [--note <line>]...
"""
import argparse
import zlib

_VALUES_PER_LINE = 16


def serialize(text):
    """Returns text as CImg serializes a one-image list of uint8, compressed (serialize uint8,1,0).

    Args:
        text: the bytes of the command file.

    Returns:
        The serialized buffer: a list header, the image's size and compressed size, then the
        zlib-compressed bytes of text and a null byte, as a 1xN column (G'MIC's 'a y').
    """
    data = text + b'\0'
    compressed = zlib.compress(data, 9)
    return (b'1 uint8 little_endian\n'
            + b'1 %d 1 1 #%d\n' % (len(data), len(compressed))
            + compressed)


def header(buffer, notes):
    """Returns the C++ header that defines data_gmic and size_data_gmic for a serialized buffer.

    Args:
        buffer: the serialized standard library (serialize()).
        notes: lines for the header's leading comment.

    Returns:
        The text of the header.
    """
    lines = ['/*', ' #', ' #  File        : gmic_stdlib_community.h', ' #']
    lines += [' #  ' + n for n in notes]
    lines += [' #', '*/',
              "/* Define image 'gmic' of size 1x%dx1x1 and type 'const unsigned char' */" % len(buffer),
              'const unsigned char data_gmic[] = {']
    rows = [', '.join(str(b) for b in buffer[i:i + _VALUES_PER_LINE])
            for i in range(0, len(buffer), _VALUES_PER_LINE)]
    lines.append(',\n'.join('  ' + r for r in rows) + ' };')
    lines += ['', 'const unsigned long size_data_gmic = (unsigned long)sizeof(data_gmic);', '']
    return '\n'.join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('input')
    parser.add_argument('output')
    parser.add_argument('--note', action='append', default=[],
                        help="a line of the header's leading comment (repeatable)")
    args = parser.parse_args()
    with open(args.input, 'rb') as f:
        text = f.read()
    with open(args.output, 'w', encoding='ascii') as f:
        f.write(header(serialize(text), args.note))


if __name__ == '__main__':
    main()
