// veronicax - XDVDFS (Xbox / Xbox 360 disc image) reader

#include "launcher/xdvdfs.h"

#include "launcher/game_files.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>

namespace veronicax {

namespace {

constexpr uint64_t kSectorSize = 0x800;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
// Trimmed/XISO, XGD3, XGD2, XGD1 game partition offsets.
constexpr std::array<uint64_t, 4> kPartitionOffsets = {0x0, 0x2080000, 0xFDA0000, 0x18300000};
constexpr uint8_t kAttrDirectory = 0x10;
constexpr int kMaxDepth = 32;

uint16_t ReadU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
         });
}

}  // namespace

std::optional<XdvdfsImage> XdvdfsImage::Open(const std::filesystem::path& iso,
                                             std::string& error) {
  XdvdfsImage image;
  image.path_ = iso;
  image.file_.open(iso, std::ios::binary);
  if (!image.file_) {
    error = "Could not open " + PathToUtf8(iso);
    return std::nullopt;
  }

  bool found = false;
  for (uint64_t offset : kPartitionOffsets) {
    char magic[kMagicSize];
    if (image.ReadAt(offset + 32 * kSectorSize, magic, kMagicSize) &&
        std::memcmp(magic, kMagic, kMagicSize) == 0) {
      image.partition_offset_ = offset;
      found = true;
      break;
    }
  }
  if (!found) {
    error = "This file is not an Xbox 360 disc image.";
    return std::nullopt;
  }

  uint8_t root[8];
  if (!image.ReadAt(image.partition_offset_ + 32 * kSectorSize + kMagicSize, root, sizeof(root))) {
    error = "The disc image is truncated.";
    return std::nullopt;
  }
  if (!image.WalkDirectory(ReadU32(root), ReadU32(root + 4), "", 0, error)) {
    return std::nullopt;
  }
  return image;
}

bool XdvdfsImage::ReadAt(uint64_t offset, void* data, size_t size) {
  file_.clear();
  file_.seekg(std::streamoff(offset));
  file_.read(static_cast<char*>(data), std::streamsize(size));
  return size_t(file_.gcount()) == size;
}

bool XdvdfsImage::WalkDirectory(uint32_t sector, uint32_t size, const std::string& prefix,
                                int depth, std::string& error) {
  if (depth > kMaxDepth) {
    error = "The disc image's directory tree is corrupt.";
    return false;
  }
  if (size == 0) {
    return true;
  }
  std::vector<uint8_t> table(size);
  if (!ReadAt(partition_offset_ + uint64_t(sector) * kSectorSize, table.data(), size)) {
    error = "The disc image is truncated.";
    return false;
  }

  // Each directory is a binary tree of entries; offsets are in dwords.
  std::vector<uint32_t> stack = {0};
  std::vector<bool> seen(size / 4 + 1, false);
  while (!stack.empty()) {
    uint32_t off = stack.back();
    stack.pop_back();
    if (off + 14 > size || seen[off / 4]) {
      continue;
    }
    seen[off / 4] = true;
    const uint8_t* p = table.data() + off;
    uint16_t left = ReadU16(p);
    uint16_t right = ReadU16(p + 2);
    if (left == 0xFFFF) {  // padding / empty table
      continue;
    }
    uint8_t name_len = p[13];
    if (off + 14 + name_len > size) {
      continue;
    }
    std::string name(reinterpret_cast<const char*>(p + 14), name_len);
    Entry entry;
    entry.path = prefix + name;
    entry.sector = ReadU32(p + 4);
    entry.size = ReadU32(p + 8);
    entry.is_dir = (p[12] & kAttrDirectory) != 0;
    // Names come from the disc: refuse anything that could escape the
    // destination folder.
    if (name.empty() || name == "." || name == ".." ||
        name.find_first_of("/\\") != std::string::npos) {
      error = "The disc image contains an invalid file name.";
      return false;
    }
    entries_.push_back(entry);
    if (entry.is_dir) {
      if (!WalkDirectory(entry.sector, entry.size, entry.path + "/", depth + 1, error)) {
        return false;
      }
    } else {
      total_file_bytes_ += entry.size;
    }
    if (left) stack.push_back(uint32_t(left) * 4);
    if (right) stack.push_back(uint32_t(right) * 4);
  }
  return true;
}

const XdvdfsImage::Entry* XdvdfsImage::Find(std::string_view path) const {
  for (const Entry& entry : entries_) {
    if (!entry.is_dir && EqualsIgnoreCase(entry.path, path)) {
      return &entry;
    }
  }
  return nullptr;
}

bool XdvdfsImage::ReadFile(const Entry& entry, std::vector<uint8_t>& out) {
  out.resize(entry.size);
  return ReadAt(partition_offset_ + uint64_t(entry.sector) * kSectorSize, out.data(), entry.size);
}

bool XdvdfsImage::ExtractAll(const std::filesystem::path& dest, const std::atomic<bool>& cancel,
                             const std::function<void(uint64_t)>& progress, std::string& error) {
  std::error_code ec;
  std::filesystem::create_directories(dest, ec);
  std::vector<char> buffer(4 << 20);
  uint64_t written = 0;
  for (const Entry& entry : entries_) {
    if (cancel.load()) {
      error = "Extraction cancelled.";
      return false;
    }
    std::filesystem::path out_path = dest / std::filesystem::path(entry.path);
    if (entry.is_dir) {
      std::filesystem::create_directories(out_path, ec);
      continue;
    }
    std::filesystem::create_directories(out_path.parent_path(), ec);
    std::ofstream out(out_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      error = "Could not write " + PathToUtf8(out_path);
      return false;
    }
    uint64_t offset = partition_offset_ + uint64_t(entry.sector) * kSectorSize;
    uint64_t remaining = entry.size;
    while (remaining) {
      if (cancel.load()) {
        error = "Extraction cancelled.";
        return false;
      }
      size_t chunk = size_t(std::min<uint64_t>(remaining, buffer.size()));
      if (!ReadAt(offset, buffer.data(), chunk)) {
        error = "The disc image is truncated (while reading " + entry.path + ").";
        return false;
      }
      out.write(buffer.data(), std::streamsize(chunk));
      if (!out) {
        error = "Could not write " + PathToUtf8(out_path) + " (disk full?)";
        return false;
      }
      offset += chunk;
      remaining -= chunk;
      written += chunk;
      progress(written);
    }
  }
  return true;
}

}  // namespace veronicax
