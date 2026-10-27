// veronicax - extracted game folder checks

#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace veronicax {

// This build is recompiled from one specific Default.xex, so only a disc with
// exactly that executable works. FNV-1a 64 identifies the version (it is not
// a security check).
inline constexpr char kXexName[] = "Default.xex";
inline constexpr uint64_t kXexFnv1a64 = 0xAFE5D903662EF1E5ull;
inline constexpr uint64_t kXexSize = 13774848;
// Written into the game folder once extraction and verification succeed.
inline constexpr char kVerifiedMarker[] = ".veronicax-verified";

inline uint64_t Fnv1a64(std::span<const uint8_t> data) {
  uint64_t hash = 0xCBF29CE484222325ull;
  for (uint8_t byte : data) {
    hash = (hash ^ byte) * 0x100000001B3ull;
  }
  return hash;
}

inline std::string HexU64(uint64_t value) {
  static constexpr char kDigits[] = "0123456789ABCDEF";
  std::string out(16, '0');
  for (int i = 15; i >= 0; --i, value >>= 4) {
    out[size_t(i)] = kDigits[value & 0xF];
  }
  return out;
}

// True when `game_dir` holds a completed, verified extraction.
inline bool IsVerifiedGameFolder(const std::filesystem::path& game_dir) {
  std::ifstream marker(game_dir / kVerifiedMarker);
  std::string recorded;
  return marker >> recorded && recorded == HexU64(kXexFnv1a64) &&
         std::filesystem::is_regular_file(game_dir / kXexName);
}

}  // namespace veronicax
