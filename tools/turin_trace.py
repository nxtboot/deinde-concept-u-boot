#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 Simon Glass <sjg@chromium.org>
"""Compare the register writes of U-Boot and openSIL on an AMD EPYC Turin

Both firmwares can print a line for each register write and firmware message
as they bring the processor up: U-Boot with CONFIG_TURIN_REG_TRACE and
openSIL with the hooks described in doc/board/gigabyte/mz33_ar1.rst. This
tool extracts those lines from a console log and compares two of them.

Usage:
   turin_trace.py parse <console log> <trace file>
   turin_trace.py compare <trace file> <trace file> [<name map>]

The name map is a file of '<address> <name>' lines for SMN registers, which
can be generated from openSIL's headers:

   grep -rh 'define SMN_.*_ADDRESS *0x' <opensil>/xUSL/Nbio |
     sed -E 's/#define +SMN_(\\w+)_ADDRESS +(0x[0-9a-fA-F]+).*/\\2 \\1/' > names
"""

import re
import sys

# A trace line, with or without openSIL's prefix and the logger's timestamps
LINE_RE = re.compile(
    rb'(?:openSIL:xUSL:(\w+):(\d+):|^)T (S8?|P\d+|M\d+|R|Q|q|U|u) '
    rb'([0-9a-fA-F][^\r\n]*)')
STAMP_RE = re.compile(rb'\n\[ *[0-9.]+\] ')


def parse(log_path, out_path):
    """Extract the trace lines from a console log"""
    with open(log_path, 'rb') as inf:
        data = STAMP_RE.sub(b'', inf.read())
    count = 0
    with open(out_path, 'w', encoding='utf-8') as out:
        for line in data.split(b'\n'):
            mat = LINE_RE.search(line)
            if not mat:
                continue
            func, lnum, kind, rest = ((x or b'').decode(errors='replace')
                                      for x in mat.groups())
            comment = f'\t# {func}:{lnum}' if func else ''
            out.write(f'{kind} {rest.strip()}{comment}\n')
            count += 1
    print(f'{count} accesses')


def load(path):
    """Load a trace file into a dict of register -> list of values written"""
    regs = {}
    order = []
    msgs = []
    with open(path, encoding='utf-8') as inf:
        lines = inf.readlines()
    for line in lines:
        body = line.split('\t')[0].split()
        if not body:
            continue
        kind = body[0]
        try:
            key, val = decode(kind, body)
        except (IndexError, ValueError):
            continue    # a line cut short by the console
        if key is None:
            msgs.append(body)
            continue
        if key not in regs:
            order.append(key)
        regs.setdefault(key, []).append(val)
    return regs, order, msgs


def decode(kind, body):
    """Decode one trace line into a register key and the value written"""
    if kind in ('S', 'S8'):
        return ('S', int(body[2], 16)), int(body[3], 16)
    if kind.startswith('P'):
        return ('P', body[2], int(body[3], 16)), int(body[4], 16)
    if kind == 'R':
        return ('R', int(body[1], 16)), int(body[2], 16)
    if kind.startswith('M'):
        return ('M', int(body[2], 16)), int(body[3], 16)
    return None, None


def load_names(names_path):
    """Load a name map of '<address> <name>' lines for SMN registers

    Args:
        names_path (str): File to read, or None for no names

    Returns:
        dict: name (str) for each address (int)
    """
    names = {}
    if names_path:
        with open(names_path, encoding='utf-8') as inf:
            for line in inf:
                parts = line.split()
                names[int(parts[0], 16)] = parts[1]
    return names


def fmt(key, names):
    """Describe a register key from load(), with its name if known

    Args:
        key (tuple): Register key from load()
        names (dict): Name map from load_names()

    Returns:
        str: Description of the register
    """
    if key[0] == 'S':
        # the blocks of the other root complexes are at 1MB strides
        name = ''
        for inst in range(8):
            base = key[1] - (inst << 20)
            if base in names:
                name = names[base] + (f'+{inst}M' if inst else '')
                break
        return f'SMN {key[1]:08x} {name}'
    if key[0] == 'P':
        return f'PCI {key[1]} {key[2]:03x}'
    if key[0] == 'R':
        return f'MSR {key[1]:x}'
    return f'MMIO {key[1]:x}'


def show_only(label, regs, order, other, names):
    """List the registers which one trace writes and the other does not

    Args:
        label (str): Name of the trace which writes them
        regs (dict): Registers of that trace, from load()
        order (list): Its register keys in order of first write
        other (dict): Registers of the other trace
        names (dict): Name map from load_names()
    """
    only = [key for key in order if key not in other]
    print(f'== written by {label} only ({len(only)})')
    for key in only:
        print(f'  {fmt(key, names):50} last {regs[key][-1]:x} '
              f'({len(regs[key])} writes)')


def compare(path_a, path_b, names_path=None):
    """Show what each trace writes that the other does not, and what differs

    Args:
        path_a (str): First trace file, from parse()
        path_b (str): Second trace file
        names_path (str): Name map for SMN registers, or None
    """
    names = load_names(names_path)
    regs_a, order_a, msgs_a = load(path_a)
    regs_b, order_b, msgs_b = load(path_b)
    show_only(path_a, regs_a, order_a, regs_b, names)
    show_only(path_b, regs_b, order_b, regs_a, names)
    diff = [key for key in order_a
            if key in regs_b and regs_a[key][-1] != regs_b[key][-1]]
    print(f'== last values differ ({len(diff)})')
    for key in diff:
        print(f'  {fmt(key, names):50} {regs_a[key][-1]:x} vs '
              f'{regs_b[key][-1]:x}')
    print(f'== messages: {len(msgs_a)} vs {len(msgs_b)}')


def main():
    """Run the parse or compare command given on the command line"""
    if len(sys.argv) >= 4 and sys.argv[1] == 'parse':
        parse(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'compare':
        compare(sys.argv[2], sys.argv[3],
                sys.argv[4] if len(sys.argv) > 4 else None)
    else:
        print(__doc__)
        sys.exit(1)


if __name__ == '__main__':
    main()
