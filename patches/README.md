# ReXGlue patches

Both builds use ReXGlue built from source at bd833a2, the same commit as the
0.10.0.24 SDK, with this patch applied. `scripts/build-rexglue.sh` clones it,
applies the patch, builds the Linux SDK and the cross-built Windows SDK
(with Vulkan, which the official Windows SDK leaves out), and points `sdk`
and `sdk-win` at the installs.

`rexglue-sdk-bd833a2-veronicax.patch` applies to ReXGlue at bd833a2 (the
same commit as the SDK), and contains these changes:

- `PhysicalHeap::Alloc/AllocFixed/AllocRange` drop any GPU-held copy of a
  newly committed range. A GPU write (a resolve) that lands after the guest
  freed that memory is never write-watched, so the GPU kept its stale copy
  and missed every CPU write once the range was reallocated. This was the
  corrupted second intro FMV and textures (notes/known-issues.md).
- `XThread::Resume` on Windows no longer takes the suspend count below zero.
  It would wrap to 255, and the next Suspend would then not suspend. Linux
  already clamps it.

## Changing the patch

Edit the source in `rexglue-sdk-bd833a2/`, run `scripts/build-rexglue.sh`, then
save the patch:

```sh
git -C rexglue-sdk-bd833a2 diff -- include src > patches/rexglue-sdk-bd833a2-veronicax.patch
```

When ReXGlue takes these fixes upstream, move to that release and drop the
patch.
