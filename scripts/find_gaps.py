#!/usr/bin/env python3
"""List code regions that no generated function covers.

Usage: find_gaps.py [generated_dir] [--min BYTES]

Codegen does not record function sizes, so coverage is rebuilt from the
generated bodies: each `loc_XXXXXXXX:` label sets the current address and
every disassembly comment after it is one 4-byte PPC instruction. A gap is
any run of addresses no function covers. Gaps smaller than --min (default 8)
are usually alignment padding and are skipped.
"""
import glob
import os
import re
import sys

FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
TABLE_RE = re.compile(r"\{ 0x([0-9A-F]{8}), (\w+) \}")
LABEL_RE = re.compile(r"^loc_([0-9A-F]{8}):")
# A disassembly comment: "// mnemonic operands" (mnemonics are lowercase).
INSN_RE = re.compile(r"^\s*// [a-z][a-z0-9.+-]* ?")
NOT_INSN = ("// FATAL", "// PATCH", "// TODO", "// NOTE")


def coverage(gen_dir):
    """Return {function start: sorted list of instruction addresses}."""
    # Map symbol -> address from the function table, so named overrides count.
    addr_of = {}
    for path in glob.glob(os.path.join(gen_dir, "*_init.cpp")):
        with open(path, encoding="utf-8", errors="replace") as f:
            for m in TABLE_RE.finditer(f.read()):
                addr_of[m.group(2)] = int(m.group(1), 16)
    cov = {}
    for path in glob.glob(os.path.join(gen_dir, "*_recomp.*.cpp")):
        start = pc = None
        with open(path, encoding="utf-8", errors="replace") as f:
            for line in f:
                m = FUNC_RE.match(line)
                if m:
                    start = pc = addr_of.get(m.group(1))
                    if start is not None:
                        cov[start] = []
                elif start is not None:
                    if line.startswith("}"):
                        start = None
                    elif (m := LABEL_RE.match(line)):
                        pc = int(m.group(1), 16)
                    elif INSN_RE.match(line) and not line.lstrip().startswith(NOT_INSN):
                        cov[start].append(pc)
                        pc += 4
    for start in cov:
        cov[start].sort()
    return cov


def contiguous_end(addrs, start):
    """End of the run of instructions beginning at start."""
    end = start
    for a in addrs:
        if a == end:
            end += 4
        elif a > end:
            break
    return end


def find_gaps(cov, min_gap=8):
    """Return [(gap_start, gap_end, start of function before the gap)]."""
    owner = {}
    for start, addrs in cov.items():
        owner[start] = start
        for a in addrs:
            owner.setdefault(a, start)
    covered = sorted(owner)
    gaps = []
    for a, b in zip(covered, covered[1:]):
        if b - (a + 4) >= min_gap:
            gaps.append((a + 4, b, owner[a]))
    return gaps


def main():
    args = sys.argv[1:]
    min_gap = 8
    if "--min" in args:
        i = args.index("--min")
        min_gap = int(args[i + 1], 0)
        del args[i:i + 2]
    gen_dir = args[0] if args else os.path.join(os.path.dirname(__file__), "..", "generated", "default")

    cov = coverage(gen_dir)
    gaps = find_gaps(cov, min_gap)
    for start, end, prev in gaps:
        print(f"0x{start:08X}-0x{end:08X}  {end - start:6d} bytes  after sub_{prev:08X}")
    print(f"{len(gaps)} gaps >= {min_gap} bytes across {len(cov)} functions", file=sys.stderr)


if __name__ == "__main__":
    main()
