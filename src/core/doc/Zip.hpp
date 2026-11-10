#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "common/Result.hpp"
#include "common/Types.hpp"

namespace eden {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t crc = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
    bool encrypted = false;

    bool isDirectory() const { return !name.empty() && name.back() == '/'; }
};

class ZipReader {
public:
    static Result<ZipReader> open(Bytes data);
    static Result<ZipReader> openFile(const std::string& path);

    const std::vector<ZipEntry>& entries() const { return entries_; }
    bool has(const std::string& name) const;
    const ZipEntry* find(const std::string& name) const;
    const ZipEntry* findIgnoreCase(const std::string& name) const;
    Result<Bytes> read(const std::string& name) const;
    Result<std::string> readText(const std::string& name) const;
    std::vector<std::string> namesWithPrefix(const std::string& prefix) const;

private:
    Result<Bytes> extract(const ZipEntry& entry) const;

    Bytes data_;
    std::vector<ZipEntry> entries_;
};

class ZipWriter {
public:
    void add(const std::string& name, const Bytes& content);
    void addText(const std::string& name, const std::string& content);
    std::size_t entryCount() const { return items_.size(); }
    Bytes finish() const;
    Status writeToFile(const std::string& path) const;

private:
    struct Item {
        std::string name;
        Bytes content;
    };
    std::vector<Item> items_;
};

Result<Bytes> readFileBytes(const std::string& path);
Status writeFileBytes(const std::string& path, const Bytes& data);

}  // namespace eden
