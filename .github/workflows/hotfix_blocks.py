#!/usr/bin/env python3
"""Writes the blocks of gmic_hotfixes.gmic that the plug-in applies to a G'MIC standard library.

For the CI's gmic command-line tests of the hot fixes (gmic_fix_tests.sh): the interpreter gets
the same fixes as the plug-in. The selection mirrors gmicGetHotfixes() in gmic_plugin.cpp (keep
them in sync), for the built-in library alone (no user file): a block is applied if the built-in
definition of its command is its <original> checksum ('-': no definition) and its callees exist
with their checksums; it is skipped if the definition is its <fixed> checksum or one of the
<other> ones; anything else is left out with a warning, as the plug-in does (where the describe
test fails on it).
The definitions and checksums are those of gmicGetDefinitions() and gmicChecksum().

usage: hotfix_blocks.py <gmic_stdlib_community.h> <gmic_hotfixes.gmic> <output .gmic>
"""
import re
import sys

import render_plugins  # (next to this script: its directory is on sys.path)

_NAME = re.compile(r'[A-Za-z_][A-Za-z0-9_]{0,254}')
_BLANKS = ''.join(chr(c) for c in range(33))  # as cimg::is_blank(): c <= ' '


def definitions(content, names):
    """Returns {name: definition} of the commands in names, as gmicGetDefinitions() reads them.

    Args:
        content: the text of a G'MIC command file.
        names: the commands to read.

    Returns:
        Each definition with comments, blank lines and leading and trailing blanks removed, each
        line followed by '\\n' ('' for a command the file doesn't define; a later definition
        replaces an earlier one).
    """
    found = dict.fromkeys(names, '')
    current = None
    for line in content.split('\n'):
        if not line or line[0] == '#':
            continue
        for i in range(1, len(line)):
            if line[i] == '#' and line[i - 1] in _BLANKS:
                line = line[:i - 1]
                break
        line = line.strip(_BLANKS)
        if not line:
            continue
        start = 1 if line[0] == '+' else 0
        name = _NAME.match(line, start)
        if name:
            rest = line[name.end():].lstrip(_BLANKS)
            if rest.startswith(':') and not rest.startswith(':='):
                current = line[:name.end()] if line[:name.end()] in found else None
                if current:
                    found[current] = ''
        if current:
            found[current] += line + '\n'
    return found


def checksum(definition):
    """The FNV-1a 64-bit hash of a definition's bytes, as 16 hexadecimal digits (gmicChecksum())."""
    value = 14695981039346656037
    for byte in definition.encode('latin-1'):  # (the text is decoded byte for byte)
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return '%016x' % value


def applied_blocks(stdlib, hotfixes):
    """Returns the text of the blocks of hotfixes that apply to the library stdlib.

    Raises:
        ValueError: a block whose definition doesn't match its <fixed> checksum (an error in
            gmic_hotfixes.gmic itself).
    """
    blocks = re.split(r'(?m)^(?=#@hotfix )', hotfixes)[1:]
    heads = [block.split('\n', 1)[0].split()[1:] for block in blocks]
    names = {head[0] for head in heads} | {c.split(':')[0] for head in heads for c in head[3:]}
    builtin = definitions(stdlib, names)
    out = []
    for block, (command, original, sums, *callees) in zip(blocks, heads):
        sums = sums.split(',')
        own = checksum(definitions(block, [command])[command])
        if own != sums[0]:
            raise ValueError('%s: the block has checksum %s, its #@hotfix line says %s' % (command, own, sums[0]))
        current = checksum(builtin[command]) if builtin[command] else None
        if current in sums:
            continue
        ok = (current is None) if original == '-' else (current == original)
        for callee in callees:
            name, _, expected = callee.partition(':')
            ok = ok and bool(builtin[name]) and (not expected or checksum(builtin[name]) == expected)
        if not ok:  # (the plug-in warns too, and leaves the block out)
            print('Warning: hot fix for %s left out: the built-in definition (checksum %s) is neither the one '
                  'it fixes nor one it leaves alone' % (command, current), file=sys.stderr)
            continue
        out.append(block)
    return ''.join(out)


def main():
    # Latin-1 throughout: one character per byte, so that the checksums are those of the bytes,
    # as the plug-in computes them
    stdlib = render_plugins.stdlib_text(sys.argv[1])
    with open(sys.argv[2], encoding='latin-1') as stream:
        hotfixes = stream.read().replace('\r', '')
    with open(sys.argv[3], 'w', encoding='latin-1') as stream:
        stream.write(applied_blocks(stdlib, hotfixes))


if __name__ == '__main__':
    main()
