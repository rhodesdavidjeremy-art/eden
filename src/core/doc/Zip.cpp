#include "doc/Zip.hpp"

#include <fstream>

#include "common/Strings.hpp"
#include "doc/Inflate.hpp"

namespace eden {

namespace {

std::uint16_t readU16(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t readU32(const Bytes& data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

void putU16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value & 0xFF));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
}

void putU32(Bytes& out, std::uint32_t value) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::uint8_t>((value >> (i * 8)) & 0xFF));
}

}  // namespace

Result<Bytes> readFileBytes(const std::string& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return makeError(ErrorCode::Io, "cannot open file: " + path);
    input.seekg(0, std::ios::end);
    const std::streamoff length = input.tellg();
    if (length < 0) return makeError(ErrorCode::Io, "cannot determine file size: " + path);
    input.seekg(0, std::ios::beg);
    Bytes data(static_cast<std::size_t>(length));
    if (length > 0) input.read(reinterpret_cast<char*>(data.data()), length);
    if (!input) return makeError(ErrorCode::Io, "read failure: " + path);
    return data;
}

Status writeFileBytes(const std::string& path, const Bytes& data) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return makeError(ErrorCode::Io, "cannot create file: " + path);
    if (!data.empty()) output.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    if (!output) return makeError(ErrorCode::Io, "write failure: " + path);
    return Status::success();
}

Result<ZipReader> ZipReader::open(Bytes data) {
    ZipReader reader;
    reader.data_ = std::move(data);
    const Bytes& bytes = reader.data_;
    if (bytes.size() < 22) return makeError(ErrorCode::Parse, "archive too small to be a zip file");
    std::size_t eocd = std::string::npos;
    const std::size_t searchStart = bytes.size() >= 22 + 65535 ? bytes.size() - 22 - 65535 : 0;
    for (std::size_t i = bytes.size() - 22 + 1; i-- > searchStart;) {
        if (readU32(bytes, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd == std::string::npos) return makeError(ErrorCode::Parse, "zip end-of-directory record not found");
    const std::uint16_t total = readU16(bytes, eocd + 10);
    const std::uint32_t directorySize = readU32(bytes, eocd + 12);
    const std::uint32_t directoryOffset = readU32(bytes, eocd + 16);
    if (total == 0xFFFF || directoryOffset == 0xFFFFFFFFu) {
        return makeError(ErrorCode::Unsupported, "zip64 archives are not supported");
    }
    if (static_cast<std::size_t>(directoryOffset) + directorySize > bytes.size()) {
        return makeError(ErrorCode::Parse, "zip central directory out of range");
    }
    std::size_t cursor = directoryOffset;
    for (std::uint16_t i = 0; i < total; ++i) {
        if (cursor + 46 > bytes.size() || readU32(bytes, cursor) != 0x02014b50u) {
            return makeError(ErrorCode::Parse, "corrupt zip central directory");
        }
        ZipEntry entry;
        const std::uint16_t flags = readU16(bytes, cursor + 8);
        entry.encrypted = (flags & 1u) != 0;
        entry.method = readU16(bytes, cursor + 10);
        entry.crc = readU32(bytes, cursor + 16);
        entry.compressedSize = readU32(bytes, cursor + 20);
        entry.uncompressedSize = readU32(bytes, cursor + 24);
        const std::uint16_t nameLength = readU16(bytes, cursor + 28);
        const std::uint16_t extraLength = readU16(bytes, cursor + 30);
        const std::uint16_t commentLength = readU16(bytes, cursor + 32);
        entry.localHeaderOffset = readU32(bytes, cursor + 42);
        if (cursor + 46 + nameLength + extraLength + commentLength > bytes.size()) {
            return makeError(ErrorCode::Parse, "zip directory entry overruns archive");
        }
        entry.name.assign(reinterpret_cast<const char*>(bytes.data() + cursor + 46), nameLength);
        reader.entries_.push_back(std::move(entry));
        cursor += 46u + nameLength + extraLength + commentLength;
    }
    return reader;
}

Result<ZipReader> ZipReader::openFile(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes.ok()) return bytes.error();
    return open(std::move(bytes.value()));
}

const ZipEntry* ZipReader::find(const std::string& name) const {
    for (const ZipEntry& entry : entries_) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

const ZipEntry* ZipReader::findIgnoreCase(const std::string& name) const {
    for (const ZipEntry& entry : entries_) {
        if (equalsIgnoreCase(entry.name, name)) return &entry;
    }
    return nullptr;
}

bool ZipReader::has(const std::string& name) const { return find(name) != nullptr; }

std::vector<std::string> ZipReader::namesWithPrefix(const std::string& prefix) const {
    std::vector<std::string> names;
    for (const ZipEntry& entry : entries_) {
        if (startsWith(entry.name, prefix) && !entry.isDirectory()) names.push_back(entry.name);
    }
    return names;
}

Result<Bytes> ZipReader::extract(const ZipEntry& entry) const {
    if (entry.encrypted) return makeError(ErrorCode::Unsupported, "encrypted zip entry: " + entry.name);
    const std::size_t header = entry.localHeaderOffset;
    if (header + 30 > data_.size() || readU32(data_, header) != 0x04034b50u) {
        return makeError(ErrorCode::Parse, "bad local header for " + entry.name);
    }
    const std::size_t start = header + 30u + readU16(data_, header + 26) + readU16(data_, header + 28);
    if (start + entry.compressedSize > data_.size()) {
        return makeError(ErrorCode::Parse, "zip entry data out of range: " + entry.name);
    }
    Bytes content;
    if (entry.method == 0) {
        content.assign(data_.begin() + static_cast<std::ptrdiff_t>(start),
                       data_.begin() + static_cast<std::ptrdiff_t>(start + entry.compressedSize));
    } else if (entry.method == 8) {
        auto inflated = inflateRaw(data_.data() + start, entry.compressedSize,
                                   static_cast<std::size_t>(entry.uncompressedSize) + 1024);
        if (!inflated.ok()) return makeError(ErrorCode::Parse, entry.name + ": " + inflated.error().message);
        content = std::move(inflated.value());
    } else {
        return makeError(ErrorCode::Unsupported, "unsupported zip compression method for " + entry.name);
    }
    if (crc32(content.data(), content.size()) != entry.crc) {
        return makeError(ErrorCode::Parse, "crc mismatch in " + entry.name);
    }
    return content;
}

Result<Bytes> ZipReader::read(const std::string& name) const {
    const ZipEntry* entry = find(name);
    if (entry == nullptr) entry = findIgnoreCase(name);
    if (entry == nullptr) return makeError(ErrorCode::NotFound, "zip entry not found: " + name);
    return extract(*entry);
}

Result<std::string> ZipReader::readText(const std::string& name) const {
    auto bytes = read(name);
    if (!bytes.ok()) return bytes.error();
    return std::string(bytes.value().begin(), bytes.value().end());
}

void ZipWriter::add(const std::string& name, const Bytes& content) { items_.push_back({name, content}); }

void ZipWriter::addText(const std::string& name, const std::string& content) {
    items_.push_back({name, Bytes(content.begin(), content.end())});
}

Bytes ZipWriter::finish() const {
    Bytes out;
    struct Record {
        std::uint32_t offset;
        std::uint32_t crc;
        std::uint16_t method;
        std::uint32_t compressedSize;
    };
    std::vector<Record> records;
    auto alreadyCompressed = [](const std::string& name) {
        const std::string lower = toLowerAscii(name);
        for (const char* suffix : {".png", ".jpg", ".jpeg", ".gif", ".webp", ".mp4", ".mov", ".webm", ".mp3", ".m4a", ".ogg", ".flac", ".zip", ".docx", ".pptx", ".epub"}) {
            if (endsWith(lower, suffix)) return true;
        }
        return false;
    };
    for (const Item& item : items_) {
        Record record{static_cast<std::uint32_t>(out.size()), crc32(item.content.data(), item.content.size()), 0,
                      static_cast<std::uint32_t>(item.content.size())};
        Bytes packed;
        if (item.content.size() >= 64 && !alreadyCompressed(item.name)) {
            packed = deflateCompress(item.content.data(), item.content.size());
            if (packed.size() < item.content.size()) {
                record.method = 8;
                record.compressedSize = static_cast<std::uint32_t>(packed.size());
            }
        }
        const Bytes& payload = record.method == 8 ? packed : item.content;
        records.push_back(record);
        putU32(out, 0x04034b50u);
        putU16(out, 20);
        putU16(out, 0x0800);
        putU16(out, record.method);
        putU16(out, 0);
        putU16(out, 0x0021);
        putU32(out, record.crc);
        putU32(out, record.compressedSize);
        putU32(out, static_cast<std::uint32_t>(item.content.size()));
        putU16(out, static_cast<std::uint16_t>(item.name.size()));
        putU16(out, 0);
        out.insert(out.end(), item.name.begin(), item.name.end());
        out.insert(out.end(), payload.begin(), payload.end());
    }
    const std::uint32_t directoryStart = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < items_.size(); ++i) {
        const Item& item = items_[i];
        putU32(out, 0x02014b50u);
        putU16(out, 20);
        putU16(out, 20);
        putU16(out, 0x0800);
        putU16(out, records[i].method);
        putU16(out, 0);
        putU16(out, 0x0021);
        putU32(out, records[i].crc);
        putU32(out, records[i].compressedSize);
        putU32(out, static_cast<std::uint32_t>(item.content.size()));
        putU16(out, static_cast<std::uint16_t>(item.name.size()));
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU16(out, 0);
        putU32(out, 0);
        putU32(out, records[i].offset);
        out.insert(out.end(), item.name.begin(), item.name.end());
    }
    const std::uint32_t directorySize = static_cast<std::uint32_t>(out.size()) - directoryStart;
    putU32(out, 0x06054b50u);
    putU16(out, 0);
    putU16(out, 0);
    putU16(out, static_cast<std::uint16_t>(items_.size()));
    putU16(out, static_cast<std::uint16_t>(items_.size()));
    putU32(out, directorySize);
    putU32(out, directoryStart);
    putU16(out, 0);
    return out;
}

Status ZipWriter::writeToFile(const std::string& path) const { return writeFileBytes(path, finish()); }

}  // namespace eden
