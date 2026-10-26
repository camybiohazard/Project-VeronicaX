# Function notes

How each entry in `config/functions.toml` was found, plus names worked out
for functions.

## Boundary overrides

| Address | End | Name | Found because | Code |
|---|---|---|---|---|
| 0x823D01B8 | 0x823D01E0 | – | `b` from 0x82540930 / 0x82540940; gap after `sub_823D00B8` | compares `248(r3)` to r4, stores it, sets bit 0x40 in a flags word |
| 0x827E8580 | 0x827E8588 | ReturnZero | ~206 tail branches; gap after `sub_827E8560` | `li r3,0; blr` |
| 0x8281BAF8 | 0x8281BB00 | – | `b` from 0x8287C0A4; gap after `sub_8281BA10` | `stw r4,132(r3); blr` (setter) |
| 0x827F3088 | 0x827F30C8 | – | runtime: "Call to invalid or unregistered function" during boot, right after `NtCreateFile(\Device\Image)`; only reached through a pointer | repacks flag bits in r5, shifts args (r3=r4, r4=-1), tail-calls 0x82BF1348; probably a file-open wrapper; last 4 bytes are padding |

## Pointer-only functions (config/gaps.toml)

Code that is only reached through vtables or function pointers is invisible
to analysis and crashes at runtime with "Call to invalid or unregistered
function". `scripts/scan_gaps.sh` finds it automatically: it loops codegen
and `scripts/refine_gaps.py` until nothing changes. A real pointer-only
function must have its address stored somewhere, so only **referenced**
addresses are candidates: a word in data (vtables, function-pointer tables)
or a `lis`/`addi` pair in code, read from `game/image.bin`
(`scripts/dump_image.sh`). Each pass:

- measures exactly what every generated function covers (labels plus
  instruction comments; see `scripts/find_gaps.py`) and proposes every
  referenced address no function covers
- shrinks entries to the code that actually decoded, because codegen stops
  at the first `b`. Packed this-adjust thunks (`addi r3,r3,-4; b method`,
  8 bytes each) are then found one per pass. The third boot crash,
  0x82B7F520, was the fifth thunk in a 40-byte gap that had been declared
  as one function.
- rejects candidates (in `notes/gaps_rejected.txt`, never proposed again)
  that are:
  - outside the code section, or decode to nothing (data or padding)
  - inside another function's span: embedded jump tables (they decode as
    repeated `lwz r16,...(r23)`) or unreached switch cases; declaring one
    splits the real function
  - branching outside themselves, or the target of another function's
    branch (e.g. `bdzf`-chain switch cases): pieces of a real function
  - producing `REX_FATAL` stubs, or lying between a branch and its target

The first version proposed *every* uncovered gap. 222 of its 355 entries
were never referenced: they were fragments of real functions. Codegen does
not chain fall-through into the next entry, so a function cut that way
silently returns early, and can skip its epilogue's register restores. That
corrupted game state, and is the prime suspect for the stairs and barracks
window crashes (notes/logs/crash-*-qi-null.txt): a list walker
(sub_8239F3B8) reached an object whose vtable (0x8204A3A8, a 9-slot
"type id" class) has no slot 10, so it called the next vtable's
QueryInterface with a null out-pointer.

Rejection errs on the safe side. A pointer-only function it drops wrongly
just shows up as a clear runtime crash at a known address. Add that one to
`config/functions.toml` by hand.

Rerun `scripts/scan_gaps.sh` after changing `config/functions.toml`.

## Switch tables (config/switch_tables.toml)

The analyzer sometimes builds a `bctr` switch on the wrong register (the
jump-table address instead of the index) or reads too few entries. Either
way the generated `default:` hits `__builtin_trap(); // Switch case out of
range`, which raises SIGILL. A normal run hides it: the thread just dies,
and the game hangs on a black screen. `scripts/debug-run.sh` stops on it.

| Function | bctr | Index | Table | Entries | Symptom |
|---|---|---|---|---|---|
| sub_822E45A8 | 0x822E45CC | r8 = u16 at 4(r3) - 40 | 0x82CB4990 | 150 (opcodes 40-189, 12 handlers) | black screen after New Game; probably the script opcode dispatcher |
| sub_8225CC00 | 0x8225CCF0 | r11 = u8 at 14(r3) | 0x82CA8DA8 | 4 | found by scanning for the same bug |

Reading a table: guest memory is mapped at host 0x100000000, so under gdb
`dump binary memory out.bin 0x1<table> 0x1<table+size>` gives the
big-endian entries. The table ends where entries stop pointing into the
function's code.

## Named functions

| Address | Name | Evidence |
|---|---|---|
