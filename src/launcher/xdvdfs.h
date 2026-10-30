// veronicax - XDVDFS (Xbox / Xbox 360 disc image) reader
//
// C++ port of scripts/xiso_extract.py: lists and extracts the files of a
// full XGD1-3 dump or a trimmed game-partition ISO. Also reads Games on
// Demand packages, which hold the same game partition split over
// <header>.data/Data0000... with hash blocks interleaved.

#pragma once

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace veronicax {

class XdvdfsImage {
 public:
  struct Entry {
    std::string path;  // relative, '/'-separated
    bool is_dir = false;
    uint32_t sector = 0;
    uint32_t size = 0;
  };

  // Opens the image and walks its directory tree. `path` is an ISO, a Games
  // on Demand header file, or one of its DataNNNN files. Returns nullopt and
  // sets `error` if it is neither.
  static std::optional<XdvdfsImage> Open(const std::filesystem::path& path, std::string& error);

  bool is_games_on_demand() const { return !god_parts_.empty(); }

  const std::vector<Entry>& entries() const { return entries_; }
  uint64_t total_file_bytes() const { return total_file_bytes_; }

  // Finds a file entry by path, ignoring case like the Xbox does.
  const Entry* Find(std::string_view path) const;

  // Reads a whole file into memory.
  bool ReadFile(const Entry& entry, std::vector<uint8_t>& out);

  // Extracts every entry under `dest`. `progress` gets the bytes written so
  // far; setting `cancel` stops early. Returns false and sets `error` on
  // failure or cancellation.
  bool ExtractAll(const std::filesystem::path& dest, const std::atomic<bool>& cancel,
                  const std::function<void(uint64_t)>& progress, std::string& error);

 private:
  XdvdfsImage() = default;
  bool OpenGamesOnDemand(const std::filesystem::path& header, std::string& error);
  // `offset` is relative to the game partition (sector * 0x800).
  bool ReadAt(uint64_t offset, void* data, size_t size);
  // Reads the Games on Demand data stream, with the hash blocks skipped.
  bool ReadGodData(uint64_t offset, void* data, size_t size);
  bool WalkDirectory(uint32_t sector, uint32_t size, const std::string& prefix, int depth,
                     std::string& error);

  std::filesystem::path path_;
  std::ifstream file_;
  uint64_t partition_offset_ = 0;
  // Games on Demand: the DataNNNN files, and how far into the partition
  // their data stream starts (the package drops the unused leading sectors).
  std::vector<std::ifstream> god_parts_;
  uint64_t god_base_ = 0;
  std::vector<Entry> entries_;
  uint64_t total_file_bytes_ = 0;
};

}  // namespace veronicax
