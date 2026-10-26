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
