// veronicax - XDVDFS (Xbox / Xbox 360 disc image) reader

#include "launcher/xdvdfs.h"

#include "launcher/game_files.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace veronicax {

namespace {

constexpr uint64_t kSectorSize = 0x800;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";
constexpr size_t kMagicSize = sizeof(kMagic) - 1;
// Trimmed/XISO, XGD3, XGD2, XGD1 game partition offsets.
constexpr std::array<uint64_t, 4> kPartitionOffsets = {0x0, 0x2080000, 0xFDA0000, 0x18300000};
constexpr uint8_t kAttrDirectory = 0x10;
// Games on Demand (SVOD) layout: each DataNNNN file is a master hash block,
// then 0xCB groups of one sub-hash block and 0xCC data blocks.
constexpr uint64_t kGodBlockSize = 0x1000;
constexpr uint64_t kGodGroupData = 0xCC * kGodBlockSize;
constexpr uint64_t kGodFileData = 0xCB * kGodGroupData;
constexpr uint32_t kGodContentType = 0x7000;
constexpr int kGodMaxParts = 10000;
constexpr int kMaxDepth = 32;

uint16_t ReadU16(const uint8_t* p) { return uint16_t(p[0] | (p[1] << 8)); }
uint32_t ReadU32(const uint8_t* p) {
  return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint32_t ReadU32BE(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
  return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
           return std::tolower(uint8_t(x)) == std::tolower(uint8_t(y));
         });
}

// True for an STFS/SVOD package header whose content is a Games on Demand
// disc.
bool IsGodHeader(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  uint8_t header[0x348];
  if (!file.read(reinterpret_cast<char*>(header), sizeof(header))) {
    return false;
  }
  bool package = std::memcmp(header, "LIVE", 4) == 0 || std::memcmp(header, "PIRS", 4) == 0 ||
                 std::memcmp(header, "CON ", 4) == 0;
  return package && ReadU32BE(header + 0x344) == kGodContentType;
}

}  // namespace

std::optional<XdvdfsImage> XdvdfsImage::Open(const std::filesystem::path& path,
                                             std::string& error) {
  XdvdfsImage image;
  image.path_ = path;

  // A DataNNNN file stands for the package whose header sits next to its
  // "<header>.data" folder.
  std::filesystem::path header = path;
  std::filesystem::path parent = path.parent_path();
  if (EqualsIgnoreCase(PathToUtf8(parent.extension()), ".data")) {
    header = parent;
    header.replace_extension();
  }
  if (std::filesystem::is_regular_file(header) && IsGodHeader(header)) {
    if (!image.OpenGamesOnDemand(header, error)) {
      return std::nullopt;
    }
  } else {
    image.file_.open(path, std::ios::binary);
    if (!image.file_) {
      error = "Could not open " + PathToUtf8(path);
      return std::nullopt;
    }
    bool found = false;
    for (uint64_t offset : kPartitionOffsets) {
      image.partition_offset_ = offset;
      char magic[kMagicSize];
      if (image.ReadAt(32 * kSectorSize, magic, kMagicSize) &&
          std::memcmp(magic, kMagic, kMagicSize) == 0) {
        found = true;
        break;
      }
    }
    if (!found) {
      error = "This file is not an Xbox 360 disc image or Games on Demand package.";
      return std::nullopt;
    }
  }

  // The package keeps the volume descriptor at the start of its data
  // stream, ahead of the sectors it maps.
  uint8_t root[8];
  bool read = image.is_games_on_demand()
                  ? image.ReadGodData(kMagicSize, root, sizeof(root))
                  : image.ReadAt(32 * kSectorSize + kMagicSize, root, sizeof(root));
  if (!read) {
    error = "The disc image is truncated.";
    return std::nullopt;
  }
  if (!image.WalkDirectory(ReadU32(root), ReadU32(root + 4), "", 0, error)) {
    return std::nullopt;
  }
  return image;
}

bool XdvdfsImage::OpenGamesOnDemand(const std::filesystem::path& header, std::string& error) {
  uint8_t descriptor[3];
  {
    std::ifstream file(header, std::ios::binary);
    file.seekg(0x395);
    if (!file.read(reinterpret_cast<char*>(descriptor), sizeof(descriptor))) {
      error = "The Games on Demand header is truncated.";
      return false;
    }
  }
  uint32_t data_block_offset = descriptor[0] | (descriptor[1] << 8) | (descriptor[2] << 16);
  god_base_ = uint64_t(data_block_offset) * kGodBlockSize - kGodBlockSize;

  std::filesystem::path data_dir = header;
  data_dir += ".data";
  for (int i = 0; i < kGodMaxParts; ++i) {
    char name[16];
    std::snprintf(name, sizeof(name), "Data%04d", i);
    std::ifstream part(data_dir / name, std::ios::binary);
    if (!part) {
      break;
    }
    god_parts_.push_back(std::move(part));
  }
  if (god_parts_.empty()) {
    error = "The Games on Demand package has no data files. Keep the " +
            PathToUtf8(data_dir.filename()) + " folder next to " +
            PathToUtf8(header.filename()) + ".";
    return false;
  }

  char magic[kMagicSize];
  if (!ReadGodData(0, magic, kMagicSize) || std::memcmp(magic, kMagic, kMagicSize) != 0) {
    error = "This Games on Demand package does not hold an Xbox 360 disc.";
    return false;
  }
  return true;
}

bool XdvdfsImage::ReadAt(uint64_t offset, void* data, size_t size) {
  if (is_games_on_demand()) {
    return offset >= god_base_ && ReadGodData(offset - god_base_, data, size);
  }
  file_.clear();
  file_.seekg(std::streamoff(partition_offset_ + offset));
  file_.read(static_cast<char*>(data), std::streamsize(size));
  return size_t(file_.gcount()) == size;
}

bool XdvdfsImage::ReadGodData(uint64_t offset, void* data, size_t size) {
  auto* out = static_cast<char*>(data);
  while (size) {
    uint64_t part = offset / kGodFileData;
    uint64_t group = offset % kGodFileData / kGodGroupData;
    uint64_t in_group = offset % kGodGroupData;
    if (part >= god_parts_.size()) {
      return false;
    }
    // Skip the master hash block, then whole groups, then this group's
    // sub-hash block.
    uint64_t file_offset =
        kGodBlockSize + group * (kGodBlockSize + kGodGroupData) + kGodBlockSize + in_group;
    size_t chunk = size_t(std::min<uint64_t>(size, kGodGroupData - in_group));
    std::ifstream& file = god_parts_[size_t(part)];
    file.clear();
    file.seekg(std::streamoff(file_offset));
    file.read(out, std::streamsize(chunk));
    if (size_t(file.gcount()) != chunk) {
      return false;
    }
    out += chunk;
    offset += chunk;
    size -= chunk;
  }
  return true;
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
  if (!ReadAt(uint64_t(sector) * kSectorSize, table.data(), size)) {
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
  return ReadAt(uint64_t(entry.sector) * kSectorSize, out.data(), entry.size);
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
    uint64_t offset = uint64_t(entry.sector) * kSectorSize;
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
