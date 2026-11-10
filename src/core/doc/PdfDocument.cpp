#include "doc/PdfDocument.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>

#include "common/Strings.hpp"
#include "doc/Inflate.hpp"
#include "doc/TextUtil.hpp"

namespace eden {

namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool isWhiteChar(char c) { return PdfLexer::isWhite(c); }

Result<Bytes> asciiHexDecode(std::string_view raw) {
    Bytes out;
    int pending = -1;
    for (char c : raw) {
        if (c == '>') break;
        int digit = -1;
        if (c >= '0' && c <= '9') digit = c - '0';
        else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') digit = c - 'A' + 10;
        if (digit < 0) continue;
        if (pending < 0) {
            pending = digit;
        } else {
            out.push_back(static_cast<std::uint8_t>((pending << 4) | digit));
            pending = -1;
        }
    }
    if (pending >= 0) out.push_back(static_cast<std::uint8_t>(pending << 4));
    return out;
}

Result<Bytes> ascii85Decode(std::string_view raw) {
    Bytes out;
    std::uint32_t group[5];
    int count = 0;
    std::size_t i = 0;
    if (raw.size() >= 2 && raw[0] == '<' && raw[1] == '~') i = 2;
    for (; i < raw.size(); ++i) {
        const char c = raw[i];
        if (isWhiteChar(c)) continue;
        if (c == '~') break;
        if (c == 'z' && count == 0) {
            out.insert(out.end(), 4, 0);
            continue;
        }
        if (c < '!' || c > 'u') return makeError(ErrorCode::Parse, "invalid ascii85 character");
        group[count++] = static_cast<std::uint32_t>(c - '!');
        if (count == 5) {
            std::uint32_t value = 0;
            for (int k = 0; k < 5; ++k) value = value * 85 + group[k];
            for (int k = 3; k >= 0; --k) out.push_back(static_cast<std::uint8_t>((value >> (k * 8)) & 0xFF));
            count = 0;
        }
    }
    if (count > 1) {
        for (int k = count; k < 5; ++k) group[k] = 84;
        std::uint32_t value = 0;
        for (int k = 0; k < 5; ++k) value = value * 85 + group[k];
        for (int k = 3; k > 3 - (count - 1); --k) out.push_back(static_cast<std::uint8_t>((value >> (k * 8)) & 0xFF));
    }
    return out;
}

Result<Bytes> runLengthDecode(std::string_view raw) {
    Bytes out;
    std::size_t i = 0;
    while (i < raw.size()) {
        const std::uint8_t length = static_cast<std::uint8_t>(raw[i++]);
        if (length == 128) break;
        if (length < 128) {
            const std::size_t count = static_cast<std::size_t>(length) + 1;
            if (i + count > raw.size()) return makeError(ErrorCode::Parse, "truncated run-length data");
            out.insert(out.end(), raw.begin() + static_cast<std::ptrdiff_t>(i),
                       raw.begin() + static_cast<std::ptrdiff_t>(i + count));
            i += count;
        } else {
            if (i >= raw.size()) return makeError(ErrorCode::Parse, "truncated run-length data");
            out.insert(out.end(), 257 - static_cast<std::size_t>(length), static_cast<std::uint8_t>(raw[i++]));
        }
    }
    return out;
}

std::string utf16BeToUtf8(const std::string& raw, std::size_t skip) {
    Bytes bytes(raw.begin(), raw.end());
    return utf16ToUtf8(bytes, false, skip);
}

}  // namespace

std::string pdfTextToUtf8(const std::string& raw) {
    if (raw.size() >= 2 && static_cast<unsigned char>(raw[0]) == 0xFE && static_cast<unsigned char>(raw[1]) == 0xFF) {
        return utf16BeToUtf8(raw, 2);
    }
    if (raw.size() >= 3 && static_cast<unsigned char>(raw[0]) == 0xEF && static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        return raw.substr(3);
    }
    return windows1252ToUtf8(Bytes(raw.begin(), raw.end()));
}

Result<Bytes> lzwDecode(const std::uint8_t* data, std::size_t size, int earlyChange) {
    Bytes out;
    std::vector<std::pair<std::size_t, std::size_t>> table;
    Bytes arena;
    auto reset = [&]() {
        table.clear();
        arena.clear();
        for (int i = 0; i < 256; ++i) {
            table.push_back({arena.size(), 1});
            arena.push_back(static_cast<std::uint8_t>(i));
        }
        table.push_back({0, 0});
        table.push_back({0, 0});
    };
    reset();
    int codeBits = 9;
    std::uint32_t buffer = 0;
    int bufferBits = 0;
    std::size_t position = 0;
    long previous = -1;
    while (true) {
        while (bufferBits < codeBits && position < size) {
            buffer = (buffer << 8) | data[position++];
            bufferBits += 8;
        }
        if (bufferBits < codeBits) break;
        const std::uint32_t code = (buffer >> (bufferBits - codeBits)) & ((1u << codeBits) - 1u);
        bufferBits -= codeBits;
        if (code == 256) {
            reset();
            codeBits = 9;
            previous = -1;
            continue;
        }
        if (code == 257) break;
        Bytes entry;
        if (code < table.size()) {
            entry.assign(arena.begin() + static_cast<std::ptrdiff_t>(table[code].first),
                         arena.begin() + static_cast<std::ptrdiff_t>(table[code].first + table[code].second));
        } else if (previous >= 0 && code == table.size()) {
            const auto& prev = table[static_cast<std::size_t>(previous)];
            entry.assign(arena.begin() + static_cast<std::ptrdiff_t>(prev.first),
                         arena.begin() + static_cast<std::ptrdiff_t>(prev.first + prev.second));
            entry.push_back(entry.front());
        } else {
            return makeError(ErrorCode::Parse, "invalid lzw code");
        }
        out.insert(out.end(), entry.begin(), entry.end());
        if (previous >= 0) {
            const auto& prev = table[static_cast<std::size_t>(previous)];
            const std::size_t start = arena.size();
            arena.insert(arena.end(), arena.begin() + static_cast<std::ptrdiff_t>(prev.first),
                         arena.begin() + static_cast<std::ptrdiff_t>(prev.first + prev.second));
            arena.push_back(entry.front());
            table.push_back({start, prev.second + 1});
        }
        previous = static_cast<long>(code);
        const std::size_t next = table.size() + static_cast<std::size_t>(earlyChange);
        if (next >= 4096) codeBits = 12;
        else if (next >= 2048) codeBits = 12;
        else if (next >= 1024) codeBits = 11;
        else if (next >= 512) codeBits = 10;
        else codeBits = 9;
    }
    return out;
}

Bytes applyPredictor(const Bytes& data, int predictor, int columns, int colors, int bitsPerComponent) {
    if (predictor < 10 || columns <= 0 || colors <= 0) return data;
    const std::size_t bytesPerPixel = std::max<std::size_t>(1, static_cast<std::size_t>(colors * bitsPerComponent + 7) / 8);
    const std::size_t rowBytes = (static_cast<std::size_t>(columns) * static_cast<std::size_t>(colors) *
                                      static_cast<std::size_t>(bitsPerComponent) + 7) / 8;
    Bytes out;
    Bytes previous(rowBytes, 0);
    Bytes current(rowBytes, 0);
    std::size_t position = 0;
    while (position + 1 + rowBytes <= data.size()) {
        const std::uint8_t filter = data[position++];
        for (std::size_t i = 0; i < rowBytes; ++i) {
            const std::uint8_t raw = data[position + i];
            const std::uint8_t left = i >= bytesPerPixel ? current[i - bytesPerPixel] : 0;
            const std::uint8_t up = previous[i];
            const std::uint8_t upLeft = i >= bytesPerPixel ? previous[i - bytesPerPixel] : 0;
            std::uint8_t value = raw;
            switch (filter) {
                case 1: value = static_cast<std::uint8_t>(raw + left); break;
                case 2: value = static_cast<std::uint8_t>(raw + up); break;
                case 3: value = static_cast<std::uint8_t>(raw + ((left + up) / 2)); break;
                case 4: {
                    const int p = left + up - upLeft;
                    const int pa = std::abs(p - left);
                    const int pb = std::abs(p - up);
                    const int pc = std::abs(p - upLeft);
                    const std::uint8_t pick = (pa <= pb && pa <= pc) ? left : (pb <= pc ? up : upLeft);
                    value = static_cast<std::uint8_t>(raw + pick);
                    break;
                }
                default: break;
            }
            current[i] = value;
        }
        position += rowBytes;
        out.insert(out.end(), current.begin(), current.end());
        previous = current;
    }
    return out;
}

Result<PdfDocument> PdfDocument::parse(Bytes data) {
    if (data.size() < 8) return makeError(ErrorCode::Parse, "file too small to be a pdf");
    PdfDocument doc;
    doc.data_ = std::move(data);
    const std::size_t headerWindow = std::min<std::size_t>(doc.data_.size(), 1024);
    const std::string head(reinterpret_cast<const char*>(doc.data_.data()), headerWindow);
    if (head.find("%PDF-") == std::string::npos) return makeError(ErrorCode::Parse, "missing pdf header");
    doc.scanObjects();
    if (doc.objects_.empty()) return makeError(ErrorCode::Parse, "no pdf objects found");
    doc.expandObjectStreams();
    doc.locateTrailer();
    if (doc.encrypted_) return makeError(ErrorCode::Unsupported, "encrypted pdf files are not supported");
    doc.collectPages();
    if (doc.pages_.empty()) return makeError(ErrorCode::Parse, "pdf contains no pages");
    return doc;
}

void PdfDocument::scanObjects() {
    const std::string_view view(reinterpret_cast<const char*>(data_.data()), data_.size());
    std::size_t position = 0;
    while ((position = view.find("obj", position)) != std::string_view::npos) {
        const std::size_t keyword = position;
        position += 3;
        if (keyword >= 3 && view.compare(keyword - 3, 3, "end") == 0) continue;
        if (position < view.size() && std::isalpha(static_cast<unsigned char>(view[position]))) continue;
        std::size_t i = keyword;
        while (i > 0 && isWhiteChar(view[i - 1])) --i;
        const std::size_t genEnd = i;
        while (i > 0 && isDigit(view[i - 1])) --i;
        if (i == genEnd) continue;
        while (i > 0 && isWhiteChar(view[i - 1])) --i;
        const std::size_t numberEnd = i;
        while (i > 0 && isDigit(view[i - 1])) --i;
        if (i == numberEnd) continue;
        const int number = std::atoi(std::string(view.substr(i, numberEnd - i)).c_str());

        PdfLexer lexer(view, keyword + 3);
        Indirect indirect;
        if (!lexer.parseObject(indirect.value, true)) continue;
        lexer.skipSpace();
        std::size_t resume = lexer.position();
        if (indirect.value.isDict() && lexer.startsWithKeyword("stream")) {
            std::size_t start = lexer.position() + 6;
            if (start < view.size() && view[start] == '\r' && start + 1 < view.size() && view[start + 1] == '\n') start += 2;
            else if (start < view.size() && (view[start] == '\n' || view[start] == '\r')) start += 1;
            std::size_t end = std::string_view::npos;
            const PdfObject* length = indirect.value.get("Length");
            if (length != nullptr && length->type == PdfObject::Type::Int && length->integer >= 0) {
                const std::size_t candidate = start + static_cast<std::size_t>(length->integer);
                if (candidate <= view.size()) {
                    std::size_t check = candidate;
                    while (check < view.size() && isWhiteChar(view[check])) ++check;
                    if (view.compare(check, 9, "endstream") == 0) end = candidate;
                }
            }
            if (end == std::string_view::npos) {
                const std::size_t marker = view.find("endstream", start);
                if (marker == std::string_view::npos) {
                    end = view.size();
                } else {
                    end = marker;
                    if (end > start && view[end - 1] == '\n') --end;
                    if (end > start && view[end - 1] == '\r') --end;
                }
            }
            indirect.hasStream = true;
            indirect.streamStart = start;
            indirect.streamEnd = end;
            const std::size_t marker = view.find("endstream", end);
            resume = marker == std::string_view::npos ? end : marker + 9;
        }
        objects_[number] = std::move(indirect);
        position = std::max(position, resume);
    }
}

void PdfDocument::expandObjectStreams() {
    std::vector<int> streams;
    for (const auto& [number, indirect] : objects_) {
        if (indirect.hasStream && indirect.value.get("Type") != nullptr && indirect.value.get("Type")->isName("ObjStm")) {
            streams.push_back(number);
        }
    }
    for (int number : streams) {
        auto decoded = streamData(number);
        if (!decoded.ok()) {
            warnings_.push_back("object stream " + std::to_string(number) + " unreadable: " + decoded.error().message);
            continue;
        }
        const PdfObject dict = objects_[number].value;
        const std::int64_t count = resolve(*dict.get("N")).asInt();
        const std::int64_t first = resolve(*dict.get("First")).asInt();
        const std::string_view body(reinterpret_cast<const char*>(decoded.value().data()), decoded.value().size());
        PdfLexer header(body, 0);
        std::vector<std::pair<int, std::size_t>> entries;
        for (std::int64_t i = 0; i < count; ++i) {
            PdfObject objectNumber;
            PdfObject offset;
            if (!header.parseObject(objectNumber, false) || !header.parseObject(offset, false)) break;
            entries.push_back({static_cast<int>(objectNumber.asInt()), static_cast<std::size_t>(offset.asInt())});
        }
        for (const auto& [objectNumber, offset] : entries) {
            if (objects_.count(objectNumber) != 0) continue;
            const std::size_t at = static_cast<std::size_t>(first) + offset;
            if (at >= body.size()) continue;
            PdfLexer lexer(body, at);
            Indirect indirect;
            if (lexer.parseObject(indirect.value, true)) objects_[objectNumber] = std::move(indirect);
        }
    }
}

void PdfDocument::locateTrailer() {
    const std::string_view view(reinterpret_cast<const char*>(data_.data()), data_.size());
    std::size_t position = 0;
    while ((position = view.find("trailer", position)) != std::string_view::npos) {
        position += 7;
        PdfLexer lexer(view, position);
        PdfObject trailer;
        if (!lexer.parseObject(trailer, true) || !trailer.isDict()) continue;
        if (const PdfObject* root = trailer.get("Root")) root_ = *root;
        if (const PdfObject* info = trailer.get("Info")) info_ = *info;
        if (trailer.get("Encrypt") != nullptr) encrypted_ = true;
    }
    for (const auto& [number, indirect] : objects_) {
        if (!indirect.hasStream || indirect.value.get("Type") == nullptr || !indirect.value.get("Type")->isName("XRef")) continue;
        if (const PdfObject* root = indirect.value.get("Root")) root_ = *root;
        if (const PdfObject* info = indirect.value.get("Info")) info_ = *info;
        if (indirect.value.get("Encrypt") != nullptr) encrypted_ = true;
    }
    if (root_.isNull()) {
        for (const auto& [number, indirect] : objects_) {
            const PdfObject* type = indirect.value.get("Type");
            if (type != nullptr && type->isName("Catalog")) {
                PdfObject ref;
                ref.type = PdfObject::Type::Ref;
                ref.refNumber = number;
                root_ = ref;
            }
        }
    }
}

bool PdfDocument::hasObject(int number) const { return objects_.count(number) != 0; }

PdfObject PdfDocument::object(int number) const {
    auto it = objects_.find(number);
    return it == objects_.end() ? PdfObject() : it->second.value;
}

PdfObject PdfDocument::resolve(const PdfObject& object) const {
    PdfObject current = object;
    for (int hops = 0; hops < 16 && current.isRef(); ++hops) current = this->object(current.refNumber);
    return current.isRef() ? PdfObject() : current;
}

Result<Bytes> PdfDocument::decodeStreamBytes(const PdfObject& dict, std::string_view raw) const {
    std::vector<std::string> filters;
    std::vector<PdfObject> parameters;
    const PdfObject filterObject = dict.get("Filter") != nullptr ? resolve(*dict.get("Filter")) : PdfObject();
    const PdfObject parmsObject = dict.get("DecodeParms") != nullptr ? resolve(*dict.get("DecodeParms"))
                                  : dict.get("DP") != nullptr        ? resolve(*dict.get("DP"))
                                                                     : PdfObject();
    if (filterObject.isName()) {
        filters.push_back(filterObject.text);
        parameters.push_back(parmsObject);
    } else if (filterObject.isArray()) {
        for (std::size_t i = 0; i < filterObject.array->size(); ++i) {
            const PdfObject item = resolve((*filterObject.array)[i]);
            filters.push_back(item.text);
            parameters.push_back(parmsObject.isArray() && i < parmsObject.array->size() ? resolve((*parmsObject.array)[i])
                                                                                       : PdfObject());
        }
    }
    Bytes current(raw.begin(), raw.end());
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const std::string& filter = filters[i];
        const PdfObject& params = parameters[i];
        const std::string_view view(reinterpret_cast<const char*>(current.data()), current.size());
        Result<Bytes> next = makeError(ErrorCode::Unsupported, "unsupported stream filter: " + filter);
        if (filter == "FlateDecode" || filter == "Fl") {
            next = inflateZlib(current.data(), current.size());
            if (!next.ok() && current.size() > 2) {
                auto lenient = inflateRaw(current.data() + 2, current.size() - 2);
                if (lenient.ok()) next = std::move(lenient);
            }
        } else if (filter == "ASCIIHexDecode" || filter == "AHx") {
            next = asciiHexDecode(view);
        } else if (filter == "ASCII85Decode" || filter == "A85") {
            next = ascii85Decode(view);
        } else if (filter == "RunLengthDecode" || filter == "RL") {
            next = runLengthDecode(view);
        } else if (filter == "LZWDecode" || filter == "LZW") {
            int early = 1;
            if (const PdfObject* value = params.get("EarlyChange")) early = static_cast<int>(value->asInt(1));
            next = lzwDecode(current.data(), current.size(), early);
        }
        if (!next.ok()) return next.error();
        current = std::move(next.value());
        if ((filter == "FlateDecode" || filter == "Fl" || filter == "LZWDecode" || filter == "LZW") && params.isDict()) {
            const int predictor = params.get("Predictor") ? static_cast<int>(resolve(*params.get("Predictor")).asInt(1)) : 1;
            if (predictor >= 10) {
                const int columns = params.get("Columns") ? static_cast<int>(resolve(*params.get("Columns")).asInt(1)) : 1;
                const int colors = params.get("Colors") ? static_cast<int>(resolve(*params.get("Colors")).asInt(1)) : 1;
                const int bits = params.get("BitsPerComponent") ? static_cast<int>(resolve(*params.get("BitsPerComponent")).asInt(8)) : 8;
                current = applyPredictor(current, predictor, columns, colors, bits);
            }
        }
    }
    return current;
}

Result<Bytes> PdfDocument::streamData(int number) const {
    auto it = objects_.find(number);
    if (it == objects_.end() || !it->second.hasStream) {
        return makeError(ErrorCode::NotFound, "pdf stream not found: " + std::to_string(number));
    }
    const Indirect& indirect = it->second;
    const std::string_view raw(reinterpret_cast<const char*>(data_.data()) + indirect.streamStart,
                               indirect.streamEnd - indirect.streamStart);
    return decodeStreamBytes(indirect.value, raw);
}

Result<Bytes> PdfDocument::streamData(const PdfObject& referenceOrObject) const {
    if (referenceOrObject.isRef()) return streamData(referenceOrObject.refNumber);
    return makeError(ErrorCode::InvalidArgument, "stream reference expected");
}

std::string PdfDocument::infoString(const std::string& key) const {
    const PdfObject info = resolve(info_);
    if (!info.isDict()) return std::string();
    const PdfObject* value = info.get(key);
    if (value == nullptr) return std::string();
    const PdfObject resolved = resolve(*value);
    return resolved.isString() ? trim(pdfTextToUtf8(resolved.text)) : std::string();
}

void PdfDocument::walkPageTree(const PdfObject& node, const PdfObject& inheritedResources, int depth,
                               std::set<int>& visited) {
    if (depth > 48) return;
    int number = node.isRef() ? node.refNumber : 0;
    if (number != 0 && !visited.insert(number).second) return;
    const PdfObject resolved = resolve(node);
    if (!resolved.isDict()) return;
    PdfObject resources = inheritedResources;
    if (const PdfObject* own = resolved.get("Resources")) resources = *own;
    const PdfObject* kids = resolved.get("Kids");
    const PdfObject* type = resolved.get("Type");
    if (kids != nullptr && (type == nullptr || !type->isName("Page"))) {
        const PdfObject list = resolve(*kids);
        if (list.isArray()) {
            for (const PdfObject& kid : *list.array) walkPageTree(kid, resources, depth + 1, visited);
        }
        return;
    }
    PdfPage page;
    page.objectNumber = number;
    page.dict = resolved;
    page.resources = resources;
    pages_.push_back(std::move(page));
}

void PdfDocument::collectPages() {
    const PdfObject root = resolve(root_);
    std::set<int> visited;
    if (root.isDict()) {
        if (const PdfObject* pages = root.get("Pages")) walkPageTree(*pages, PdfObject(), 0, visited);
    }
    if (!pages_.empty()) return;
    warnings_.push_back("page tree unreadable; falling back to scanning for page objects");
    for (const auto& [number, indirect] : objects_) {
        const PdfObject* type = indirect.value.get("Type");
        if (type == nullptr || !type->isName("Page")) continue;
        PdfPage page;
        page.objectNumber = number;
        page.dict = indirect.value;
        if (const PdfObject* resources = indirect.value.get("Resources")) page.resources = *resources;
        pages_.push_back(std::move(page));
    }
}

}  // namespace eden
