#!/usr/bin/env python3
"""One refinement pass over config/gaps.toml. Run after each codegen.

Usage: refine_gaps.py

Pointer-only functions (vtable slots, function-pointer tables) are invisible
to analysis. A real one must have its address stored somewhere, so only
uncovered addresses that are referenced count as candidates: a 32-bit word
outside the code section, or a lis/addi (ori) pair in code. Needs
game/image.bin (scripts/dump_image.sh).

1. Rejects entries codegen could not use: outside the code section, no
   blocks (data/padding), REX_FATAL stubs, branches that leave the function,
   inside another function's span, or never referenced. Rejections are kept
   in notes/gaps_rejected.txt and never proposed again.
2. Shrinks each kept entry to the code codegen actually decoded from its
   start, clamped so it never runs into the next function or candidate.
3. Adds every referenced address that no function covers.

Prints how many candidates were added; 0 means the scan has converged.
"""
import bisect
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from find_gaps import contiguous_end, coverage, find_gaps  # noqa: E402

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
GEN = os.path.join(ROOT, "generated", "default")
GAPS = os.path.join(ROOT, "config", "gaps.toml")
REJECTED = os.path.join(ROOT, "notes", "gaps_rejected.txt")
LOG = os.path.join(ROOT, "notes", "logs", "codegen.stdout")
IMAGE = os.path.join(ROOT, "game", "image.bin")
IMAGE_BASE = 0x82000000
CODE_LO, CODE_HI = 0x82170000, 0x82C7ACCC  # from the run log's function table line
MIN_GAP = 8

HEADER = (
    "# Pointer-only functions (vtable / function-pointer targets) found by\n"
    "# scripts/refine_gaps.py. Do not edit by hand; see notes/functions.md.\n"
)
ENTRY_RE = re.compile(r'\[functions\."0x([0-9A-F]{8})"\]\nend = 0x([0-9A-F]{8})')
FUNC_RE = re.compile(r"^DEFINE_REX_FUNC\((\w+)\)")
TABLE_RE = re.compile(r"\{ 0x([0-9A-F]{8}), (\w+) \}")


def load_entries():
    if not os.path.exists(GAPS):
        return {}
    return {int(s, 16): int(e, 16) for s, e in ENTRY_RE.findall(open(GAPS).read())}


def load_rejected():
    rejected = {}
    if os.path.exists(REJECTED):
        for line in open(REJECTED):
            m = re.match(r"0x([0-9A-F]{8}) (.*)", line)
            if m:
                rejected[int(m.group(1), 16)] = m.group(2).strip()
    return rejected


def fatal_functions():
    """Addresses of generated functions that contain REX_FATAL stubs."""
    addr_of = {}
    for p in glob.glob(os.path.join(GEN, "*_init.cpp")):
        for a, name in TABLE_RE.findall(open(p, errors="replace").read()):
            addr_of[name] = int(a, 16)
    bad = set()
    for p in glob.glob(os.path.join(GEN, "*_recomp.*.cpp")):
        cur = None
        for line in open(p, errors="replace"):
            m = FUNC_RE.match(line)
            if m:
                cur = addr_of.get(m.group(1))
            elif cur is not None and "REX_FATAL" in line:
                bad.add(cur)
    return bad


def referenced_addresses():
    """Code addresses the image refers to: data words, or lis+addi/ori in code."""
    if not os.path.exists(IMAGE):
        sys.exit("game/image.bin missing; run scripts/dump_image.sh")
    data = open(IMAGE, "rb").read()
    words = struct.unpack(f">{len(data) // 4}I", data[: len(data) // 4 * 4])
    lo_i, hi_i = (CODE_LO - IMAGE_BASE) // 4, (CODE_HI - IMAGE_BASE) // 4
    in_code = lambda a: CODE_LO <= a < CODE_HI and a % 4 == 0
    refs = {w for i, w in enumerate(words) if not (lo_i <= i < hi_i) and in_code(w)}
    lis = {}
    for i in range(lo_i, hi_i):
        w = words[i]
        op = w >> 26
        if op == 15 and (w >> 16) & 0x1F == 0:  # lis rD,imm
            lis[(w >> 21) & 0x1F] = (i, (w & 0xFFFF) << 16)
        elif op in (14, 24):  # addi / ori rD,rA,imm
            ra = (w >> 16) & 0x1F
            if ra in lis and i - lis[ra][0] < 16:
                imm = w & 0xFFFF
                if op == 14 and imm & 0x8000:
                    imm -= 0x10000
                v = (lis[ra][1] + imm) & 0xFFFFFFFF
                if in_code(v):
                    refs.add(v)
    return refs


def holes_in_functions(cov):
    """Map address -> enclosing function for every gap start or function
    start that lies before the last instruction of an earlier function."""
    spans = sorted((start, addrs[-1] + 4) for start, addrs in cov.items() if addrs)
    points = sorted(set(cov) | {s + 4 * len(a) for s, a in cov.items()} | {g for g, _, _ in find_gaps(cov, MIN_GAP)})
    inside = {}
    best_end, best_fn, i = 0, None, 0
    for pt in points:
        while i < len(spans) and spans[i][0] < pt:
            if spans[i][1] > best_end:
                best_end, best_fn = spans[i][1], spans[i][0]
            i += 1
        if pt < best_end:
            inside[pt] = best_fn
    return inside


_SPANS = None


def inside_points(cov, inside, addr):
    """{addr: function} if addr lies inside an earlier function's span."""
    global _SPANS
    if _SPANS is None:
        spans = sorted((st, a[-1] + 4) for st, a in cov.items() if a)
        best, out_starts, out_ends, out_fns = 0, [], [], []
        for st, en in spans:
            out_starts.append(st)
            if en > best:
                best, fn = en, st
            out_ends.append((best, fn))
        _SPANS = (out_starts, out_ends)
    starts, ends = _SPANS
    i = bisect.bisect_left(starts, addr) - 1
    if i >= 0 and ends[i][0] > addr:
        return {addr: ends[i][1]}
    return {}


def main():
    entries = load_entries()
    rejected = load_rejected()
    log = open(LOG, errors="replace").read() if os.path.exists(LOG) else ""

    reasons = {}
    for a in re.findall(r"function (0x[0-9A-F]{8}) not in any code region", log):
        reasons[int(a, 16)] = "not in code region"
    for a in re.findall(r"Function (0x[0-9A-F]{8}) has no blocks", log):
        reasons[int(a, 16)] = "no blocks (data/padding)"
    # Kernel import thunks are referenced by the import table but are the
    # runtime's to provide; declaring them leaves undefined sub_ symbols.
    for a in re.findall(r"\[functions\] (0x[0-9A-F]{8}) outranked by import", log):
        reasons[int(a, 16)] = "kernel import thunk"
    sources = re.findall(r"at ([0-9A-F]{8}) branches outside function", log)
    sources += re.findall(r"from 0x([0-9A-F]{8}) - target not in any function", log)
    sources += re.findall(r"Unresolved conditional branch to 0x[0-9A-F]{8} from 0x([0-9A-F]{8})", log)
    for a in sources:
        src = int(a, 16)
        for s, e in entries.items():
            if s <= src < e:
                reasons[s] = "branches outside itself (part of previous function?)"
    # ...and the other side: an entry another function branches into is
    # one of that function's blocks (e.g. bdzf-chain switch cases).
    for a in re.findall(r"branches outside function to ([0-9A-F]{8})", log):
        tgt = int(a, 16)
        for s, e in entries.items():
            if s <= tgt < e:
                reasons[s] = "branch target of another function (one of its blocks)"
    for s in fatal_functions():
        if s in entries:
            reasons[s] = "unresolved branch stub (part of previous function?)"
    # An entry between a branch and its target cut a real function in two.
    pairs = re.findall(r"Unresolved conditional branch to 0x([0-9A-F]{8}) from 0x([0-9A-F]{8})", log)
    for p in glob.glob(os.path.join(GEN, "*_recomp.*.cpp")):
        text = open(p, errors="replace").read()
        pairs += [(t, s) for s, t in re.findall(r"Unresolved branch from 0x([0-9A-F]{8}) to 0x([0-9A-F]{8})", text)]
        pairs += re.findall(r"Jump target 0x([0-9A-F]{8}) unresolved at bctr 0x([0-9A-F]{8})", text)
    starts = sorted(entries)
    for t, s in pairs:
        lo, hi = sorted((int(t, 16), int(s, 16)))
        for i in range(bisect.bisect_right(starts, lo), bisect.bisect_right(starts, hi)):
            reasons[starts[i]] = "splits a real function (between a branch and its target)"
    cov = coverage(GEN)
    refs = referenced_addresses()
    for s in entries:
        if s not in refs:
            reasons[s] = "never referenced (not a pointer-only function)"
    # A gap inside another function's span is a hole in that function (an
    # embedded jump table or unreached cases); declaring it splits the function.
    inside = holes_in_functions(cov)
    for s in entries:
        if s in inside:
            reasons[s] = f"inside sub_{inside[s]:08X} (jump table or unreached code)"
    for s, why in reasons.items():
        if s in entries:
            rejected[s] = why
            del entries[s]

    for s in list(entries):
        if cov.get(s):
            entries[s] = contiguous_end(cov[s], s)

    covered = set(cov)
    for addrs in cov.values():
        covered.update(addrs)
    added = 0
    for r in sorted(refs):
        if r in covered or r in entries or r in rejected:
            continue
        enclosing = inside_points(cov, inside, r)
        if enclosing:
            rejected[r] = f"referenced but inside sub_{enclosing[r]:08X}"
            continue
        entries[r] = r + 4  # clamped to the next boundary below
        added += 1

    # Each entry runs to the next function start, or to the next instruction
    # another function covers. Its own decoded instructions never bound it.
    starts = sorted(set(cov) | set(entries))
    other_covered = {}
    for st, addrs in cov.items():
        for a in addrs:
            other_covered.setdefault(a, st)
    for s in entries:
        i = bisect.bisect_right(starts, s)
        nxt = starts[i] if i < len(starts) else CODE_HI
        if s in cov and cov[s]:
            end = contiguous_end(cov[s], s)
        else:
            end = s + 4
            while end < nxt and end not in other_covered:
                end += 4
        entries[s] = min(end, nxt)

    with open(GAPS, "w") as f:
        f.write(HEADER)
        for s in sorted(entries):
            f.write(f'\n[functions."0x{s:08X}"]\nend = 0x{entries[s]:08X}\n')
    with open(REJECTED, "w") as f:
        f.write("# Gap candidates refine_gaps.py rejected; never proposed again.\n")
        for s in sorted(rejected):
            f.write(f"0x{s:08X} {rejected[s]}\n")

    print(f"kept {len(entries)}, rejected {len(rejected)} (+{len(reasons)} this pass), added {added}")


if __name__ == "__main__":
    main()
