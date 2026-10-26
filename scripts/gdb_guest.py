"""gdb helpers for inspecting guest (Xbox 360) state in a running build.

Loaded by scripts/debug-run.sh. Guest memory is mapped at host 0x100000000
and is big-endian. Commands:

  guest-ctx [EXPR]   guest registers from a PPCContext (default: $rbx)
  guest-obj ADDR     words of a guest object, its vtable and vtable entries
                     (ADDR may be ctx:rN to take it from a guest register)
  guest-stack [CTX]  return addresses from the guest stack back-chain
"""
import gdb

GUEST_BASE = 0x100000000
# PPCContext starts r3, r0, r1, r2, r4..r31, lr (8 bytes each).
CTX_ORDER = ["r3", "r0", "r1", "r2"] + [f"r{i}" for i in range(4, 32)] + ["lr"]


def read_u32(guest_addr):
    data = gdb.selected_inferior().read_memory(GUEST_BASE + (guest_addr & 0xFFFFFFFF), 4)
    return int.from_bytes(bytes(data), "big")


def read_ctx(expr):
    base = int(gdb.parse_and_eval(expr))
    raw = bytes(gdb.selected_inferior().read_memory(base, 8 * len(CTX_ORDER)))
    return {name: int.from_bytes(raw[i * 8:i * 8 + 4], "little") for i, name in enumerate(CTX_ORDER)}


def symbol_for(guest_addr):
    # Recompiled functions only have minimal symbols, which parse_and_eval finds.
    try:
        gdb.parse_and_eval(f"&sub_{guest_addr:08X}")
        return f"sub_{guest_addr:08X}"
    except gdb.error:
        return ""


class GuestCtx(gdb.Command):
    def __init__(self):
        super().__init__("guest-ctx", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        ctx = read_ctx(arg or "$rbx")
        names = CTX_ORDER
        for i in range(0, len(names), 4):
            print("  ".join(f"{n:>3}={ctx[n]:08X}" for n in names[i:i + 4]))


class GuestObj(gdb.Command):
    def __init__(self):
        super().__init__("guest-obj", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        # "ctx:r3" reads the address from a guest register in the $rbx context.
        if arg.startswith("ctx:"):
            addr = read_ctx("$rbx")[arg[4:]]
        else:
            addr = int(gdb.parse_and_eval(arg)) & 0xFFFFFFFF
        try:
            words = [read_u32(addr + 4 * i) for i in range(16)]
        except gdb.MemoryError:
            print(f"object {addr:08X}: unreadable")
            return
        print(f"object {addr:08X}: " + " ".join(f"{w:08X}" for w in words))
        vtable = words[0]
        print(f"vtable {vtable:08X}:")
        for i in range(16):
            try:
                entry = read_u32(vtable + 4 * i)
            except gdb.MemoryError:
                print(f"  [{i:2d}] +{4 * i:<3d} unreadable")
                break
            print(f"  [{i:2d}] +{4 * i:<3d} {entry:08X} {symbol_for(entry)}")


class GuestStack(gdb.Command):
    def __init__(self):
        super().__init__("guest-stack", gdb.COMMAND_DATA)

    def invoke(self, arg, from_tty):
        ctx = read_ctx(arg or "$rbx")
        print(f"lr={ctx['lr']:08X} {symbol_for(ctx['lr'])}")
        sp = ctx["r1"]
        for _ in range(24):
            try:
                back = read_u32(sp)
                saved_lr = read_u32(back - 8) if back else 0
            except gdb.MemoryError:
                break
            if not back or back <= sp:
                break
            print(f"  frame {back:08X}  saved lr {saved_lr:08X}")
            sp = back


GuestCtx()
GuestObj()
GuestStack()
