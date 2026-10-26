# Cross-compile for Windows x64 from Linux with clang-cl + lld-link.
#
# Needs Microsoft's CRT and Windows SDK laid out by xwin
# (https://github.com/Jake-Shadle/xwin):
#   xwin --accept-license --arch x86_64 splat --output ~/.local/share/xwin
# Override the location with -DXWIN_DIR=... or the XWIN_DIR environment
# variable. Those files are licensed by Microsoft: never copy them into the
# repo or a release.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

if(NOT XWIN_DIR)
  if(DEFINED ENV{XWIN_DIR})
    set(XWIN_DIR "$ENV{XWIN_DIR}")
  else()
    set(XWIN_DIR "$ENV{HOME}/.local/share/xwin")
  endif()
endif()
set(XWIN_DIR "${XWIN_DIR}" CACHE PATH "xwin splat output (MSVC CRT + Windows SDK)")
if(NOT EXISTS "${XWIN_DIR}/crt/include" OR NOT EXISTS "${XWIN_DIR}/sdk/include/um")
  message(FATAL_ERROR "No MSVC CRT / Windows SDK at XWIN_DIR=${XWIN_DIR}; run xwin splat first")
endif()

set(_llvm_suffix "-20")
set(CMAKE_C_COMPILER clang-cl${_llvm_suffix})
set(CMAKE_CXX_COMPILER clang-cl${_llvm_suffix})
set(CMAKE_LINKER lld-link${_llvm_suffix})
set(CMAKE_AR llvm-lib${_llvm_suffix})
set(CMAKE_RC_COMPILER llvm-rc${_llvm_suffix})
set(CMAKE_MT llvm-mt${_llvm_suffix})
set(CMAKE_C_COMPILER_TARGET x86_64-pc-windows-msvc)
set(CMAKE_CXX_COMPILER_TARGET x86_64-pc-windows-msvc)

# System headers via /imsvc so their warnings stay quiet, like real MSVC.
set(_xwin_includes
    "${XWIN_DIR}/crt/include"
    "${XWIN_DIR}/sdk/include/ucrt"
    "${XWIN_DIR}/sdk/include/um"
    "${XWIN_DIR}/sdk/include/shared"
    "${XWIN_DIR}/sdk/include/winrt")
set(_xwin_cflags "")
foreach(_dir IN LISTS _xwin_includes)
  string(APPEND _xwin_cflags " /imsvc \"${_dir}\"")
endforeach()
# rexglue_apply_target_settings only adds -msse4.1 when NOT MSVC, and CMake
# treats clang-cl as MSVC. Without it clang lowers round-to-even to a call to
# C23 roundevenf, which the MSVC CRT lacks (the upstream clang++ preset gets
# roundss instead).
string(APPEND _xwin_cflags " -msse4.1")
set(CMAKE_C_FLAGS_INIT "${_xwin_cflags}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_cflags}")
set(CMAKE_RC_FLAGS_INIT "-I \"${XWIN_DIR}/sdk/include/um\" -I \"${XWIN_DIR}/sdk/include/shared\"")

set(_xwin_ldflags
    "/libpath:\"${XWIN_DIR}/crt/lib/x86_64\" /libpath:\"${XWIN_DIR}/sdk/lib/um/x86_64\" /libpath:\"${XWIN_DIR}/sdk/lib/ucrt/x86_64\"")
set(CMAKE_EXE_LINKER_FLAGS_INIT "${_xwin_ldflags}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_ldflags}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_ldflags}")

# xwin ships release runtimes only (no msvcrtd.lib), so CMake's compiler
# checks must build Release too.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# Packages come from CMAKE_PREFIX_PATH (the Windows ReXGlue SDK, set by the
# win-cross preset), which is searched before any host location.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
