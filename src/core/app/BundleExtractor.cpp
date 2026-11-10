#include "app/BundleExtractor.hpp"

#include <filesystem>

#include "doc/Zip.hpp"

namespace eden {

namespace {

bool safeRelativeName(const std::string& name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos) return false;
    if (name.size() > 1 && name[1] == ':') return false;
    std::size_t start = 0;
    while (start <= name.size()) {
        std::size_t end = name.find('/', start);
        if (end == std::string::npos) end = name.size();
        if (name.compare(start, end - start, "..") == 0) return false;
        start = end + 1;
    }
    return true;
}

}  // namespace

Status extractZipBundle(const Bytes& archive, const std::string& destination, const ExtractProgress& progress) {
    auto reader = ZipReader::open(archive);
    if (!reader.ok()) return reader.error();
    std::error_code error;
    std::filesystem::create_directories(destination, error);
    if (error) return makeError(ErrorCode::Io, "cannot create " + destination + ": " + error.message());
    const auto& entries = reader.value().entries();
    std::size_t done = 0;
    for (const ZipEntry& entry : entries) {
        if (!safeRelativeName(entry.name)) {
            return makeError(ErrorCode::InvalidArgument, "unsafe path in bundle: " + entry.name);
        }
        const std::filesystem::path target = std::filesystem::path(destination) / entry.name;
        if (entry.isDirectory()) {
            std::filesystem::create_directories(target, error);
            if (error) return makeError(ErrorCode::Io, "cannot create " + target.string() + ": " + error.message());
        } else {
            std::filesystem::create_directories(target.parent_path(), error);
            if (error) return makeError(ErrorCode::Io, "cannot create " + target.parent_path().string() + ": " + error.message());
            auto content = reader.value().read(entry.name);
            if (!content.ok()) return makeError(ErrorCode::Io, "cannot read " + entry.name + ": " + content.error().message);
            Status written = writeFileBytes(target.string(), content.value());
            if (!written.ok()) return written;
        }
        ++done;
        if (progress) progress(done, entries.size());
    }
    return Status::success();
}

}  // namespace eden
