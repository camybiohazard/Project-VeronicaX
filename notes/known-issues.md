# Known issues

## Stairs / barracks window freeze (fixed 2026-10-26)

Intermittent. A list walker (sub_8239F3B8) reached an object of the wrong
class and called a QueryInterface with a null out-pointer. Captures are in
`notes/logs/crash-*-qi-null.txt`. Cause: gap entries that cut real
functions into fragments. Fixed by only declaring referenced addresses (see
functions.md). Confirmed past the barracks window. If either scene freezes
again, compare with those captures.

## Lighter light and shadows broken in the jail cell (fixed 2026-10-26)

Fixed by `snorm16_render_target_full_range=false`, now our default (set in
`VeronicaxApp::OnPostSetup`; a flag or config file still overrides it). The
emulator otherwise maps the Xbox's -32..32 SNORM16 render targets in a way
that distorts the lighting the game accumulates in them. The setting came
from VeronicaRecomp (https://github.com/psxrestore/VeronicaRecomp,
BSD-3-Clause), which sets it with the comment "It breaks lighting when
enabled!". It was isolated by testing each of their settings in turn. None
of the others were needed; `clear_memory_page_state=false` causes texture
corruption.

Xenia Canary fixed the same symptom differently in d119505 (texture integer
scaling fetches). ReXGlue doesn't have that yet; porting it is an option if
lighting looks wrong elsewhere.

GPU cvars are registered by the GPU plugin, so code can only set them from
`OnPostSetup` onwards. Use `GetFlagSource` to respect user overrides
(`HasNonDefaultValue` can't tell an explicit `true` from the default).

## Null write to guest 0x00000200 under X11 (open)

With `SDL_VIDEO_DRIVER=x11`, boot hits `Unhandled guest access violation:
write of guest 0x00000200` every time, before the device selector. It never
happens on Wayland. Something in the X11 path, perhaps the window or video
mode size, changes what the game does. Use Wayland. In a shell where
libdecor's GTK plugin aborts, set `SDL_VIDEO_WAYLAND_ALLOW_LIBDECOR=0`
(scripts/smoke-test.sh does).

## Testing

`scripts/smoke-test.sh` boots for 30 s and checks the game reaches the
storage device selector with no fatal errors. Run it after every codegen
change. It caught a scanner bug that shrank 114 functions to one
instruction, which produced a black screen with no error at all.

## "Disc read error" when opening doors (fixed 2026-10-26)

The game exited with the dirty-disc dialog, twice while opening a door. The
fatal error hook (`src/game_hooks.cpp`) logged `Failed open file.
d:\BHCV\nativeXenon\system\filter\TVMask_LIN.tex 998`: a synchronous
ReadFile (sub_827F7588) failed with ERROR_NOACCESS because its buffer, a
0x56000-byte XPhysicalAlloc block at 0xEF5xxxxx, was free in every physical
view. Tracing XPhysicalAlloc/XPhysicalFree showed two loader threads given
the same address within a millisecond, then one freeing it under the other.

Cause: the runtime's `PhysicalHeap::Release` frees the parent (physical)
pages, gives up the global lock in `TriggerCallbacks`, and only then frees
the 0xE0000000 view's page table. An allocation in that window gets the same
physical pages, and its `AllocFixed` pin succeeds over the still-committed
pages. Fixed on our side by serialising the game's XPhysicalAlloc
(sub_82BF1348) and XPhysicalFree (sub_82BF13E8) with one mutex. The D3D
paths (sub_829A00D0/0258/0150, and the free in sub_8298C4E0, which is
Present) are not locked; they are rare. Worth reporting upstream to ReXGlue.

It also reproduces with no input: before the fix, 1 in 2-4 unattended
150 s boots hit it in the title loop; after, 0 in 6.

The fatal error handler (sub_82829640) formats its message, passes it to the
callback at 0x82CD1D18 (sub_823B42A8, the dirty-disc dialog) and exits. The
hook logs it as `Game fatal error: "..."`. Other messages it can show:
"LZX decompress failed : %d", "MtFile::readEnd", "Shader compile failed. %s",
"ERR08 : Memory overrun.", "NYI.". The ReadFile hook logs the buffer and its
heap state for any failed synchronous read. Both only log with
`--veronicax_debug_hooks=true` (or `veronicax_debug_hooks = true` in
`veronicax.toml`); the allocation lock is always on.

## Leaked /dev/shm guest memory files (fixed 2026-10-26)

The runtime only unlinks `/dev/shm/xenia_memory_*` on a clean shutdown and it
always hard-exits, so each run leaked one: 49 files, 6 GB of RAM, then bus
errors at boot once /dev/shm filled. `VeronicaxApp::UnlinkGuestMemoryFile`
unlinks it right after setup (the mapping only uses the descriptor). If old
files remain from before this fix: `rm /dev/shm/xenia_memory_*` with no game
running.
