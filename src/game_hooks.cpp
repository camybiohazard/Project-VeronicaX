// veronicax - hooks on recompiled game functions
//
// A strong definition here replaces the weak generated one; the original is
// still callable as __imp__<name>.

#include <cstring>
#include <mutex>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

// The fatal error and ReadFile hooks below only log; when this is off they
// pass straight through to the game's functions.
REXCVAR_DEFINE_BOOL(veronicax_debug_hooks, false, "VeronicaX",
                    "Log the game's fatal error messages and failed file reads");

extern "C" void __imp__sub_823B42A8(PPCContext& __restrict ctx, uint8_t* base);

// The game's fatal error handler (sub_82829640) formats a message such as
// "Failed open file. %s %d" into a 1024-byte stack buffer, passes it to this
// callback, then exits. The callback only shows the dirty-disc dialog, so the
// message would otherwise be lost. sub_82829640 saves its caller's return
// address at r1 + 1136 - 8 (its frame is 1136 bytes) and this callback has no
// frame of its own.
REX_HOOK_RAW(sub_823B42A8) {
  if (!REXCVAR_GET(veronicax_debug_hooks)) {
    __imp__sub_823B42A8(ctx, base);
    return;
  }
  const char* message = rex::memory::GuestPtr<const char*>(base, ctx.r3.u32);
  std::string_view text(message, strnlen(message, 1023));
  while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) {
    text.remove_suffix(1);
  }
  uint32_t caller = __builtin_bswap32(
      *rex::memory::GuestPtr<const uint32_t*>(base, ctx.r1.u32 + 1136 - 8));
  REXLOG_ERROR("Game fatal error: \"{}\" (raised before guest {:08X})", text, caller);
  __imp__sub_823B42A8(ctx, base);
}

extern "C" void __imp__sub_82BEED88(PPCContext& __restrict ctx, uint8_t* base);

namespace {

void LogGuestRange(const char* label, uint32_t address) {
  rex::memory::BaseHeap* heap = rex::system::kernel_memory()->LookupHeap(address);
  if (!heap) {
    REXLOG_ERROR("  {} {:08X}: no heap", label, address);
    return;
  }
  rex::memory::HeapAllocationInfo info{};
  heap->QueryRegionInfo(address & ~(heap->page_size() - 1), &info);
  REXLOG_ERROR(
      "  {} {:08X}: heap {:08X} ({}, page {:X}), allocation {:08X}+{:X}, region {:08X}+{:X}, "
      "state {:X}, protect {:X}",
      label, address, heap->heap_base(),
      heap->heap_type() == rex::memory::HeapType::kGuestPhysical ? "physical" : "virtual",
      heap->page_size(), info.allocation_base, info.allocation_size, info.base_address,
      info.region_size, info.state, info.protect);
}

}  // namespace

// The game's ReadFile (kernel32, statically linked). A texture read once
// failed with ERROR_NOACCESS after many room loads: the runtime refuses reads
// into buffers that cross heaps or include pages that aren't read-write. Log
// the buffer so the next failure says which.
REX_HOOK_RAW(sub_82BEED88) {
  if (!REXCVAR_GET(veronicax_debug_hooks)) {
    __imp__sub_82BEED88(ctx, base);
    return;
  }
  uint32_t handle = ctx.r3.u32;
  uint32_t buffer = ctx.r4.u32;
  uint32_t length = ctx.r5.u32;
  // Overlapped reads return 0 with ERROR_IO_PENDING; only synchronous ones
  // (no OVERLAPPED, as in the failing texture read) have really failed.
  bool synchronous = ctx.r7.u32 == 0;
  __imp__sub_82BEED88(ctx, base);
  if (synchronous && ctx.r3.u32 == 0 && length) {
    REXLOG_ERROR("ReadFile failed: handle {:08X}, buffer {:08X}, length {:X}", handle, buffer,
                 length);
    LogGuestRange("start", buffer);
    LogGuestRange("end  ", buffer + length - 1);
    // Physical memory is visible at 0xA0000000, 0xC0000000 and 0xE0000000
    // (+0x1000); each view has its own page table here.
    if (buffer >= 0xA0000000) {
      uint32_t physical = rex::system::kernel_memory()->GetPhysicalAddress(buffer);
      REXLOG_ERROR("  physical {:08X}", physical);
      LogGuestRange("as A0", 0xA0000000 + physical);
      LogGuestRange("as C0", 0xC0000000 + physical);
      if (physical >= 0x1000) {
        LogGuestRange("as E0", 0xE0000000 + physical - 0x1000);
      }
    }
  }
}


extern "C" void __imp__sub_82BF1348(PPCContext& __restrict ctx, uint8_t* base);
extern "C" void __imp__sub_82BF13E8(PPCContext& __restrict ctx, uint8_t* base);

namespace {
std::mutex physical_alloc_mutex;
}  // namespace

// XPhysicalAlloc / XPhysicalFree (statically linked), used by the game's
// texture and buffer loaders on several threads at once. The runtime's
// PhysicalHeap::Release frees the physical pages, drops its lock, and only
// then frees the 0xE0000000 view's pages. An allocation on another thread in
// that window gets the same address, and the release then frees it under
// that thread. The loader's ReadFile into it fails with ERROR_NOACCESS and the
// game exits with "Failed open file. ...TVMask_LIN.tex 998" (door loads, the
// title loop). Serialising these two closes the window. Both only wrap the
// kernel call, so the lock can't deadlock.
REX_HOOK_RAW(sub_82BF1348) {
  std::lock_guard lock(physical_alloc_mutex);
  __imp__sub_82BF1348(ctx, base);
}

REX_HOOK_RAW(sub_82BF13E8) {
  std::lock_guard lock(physical_alloc_mutex);
  __imp__sub_82BF13E8(ctx, base);
}
