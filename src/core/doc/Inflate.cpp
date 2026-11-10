#include "doc/Inflate.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace eden {

namespace {

struct InflateFailure {
    std::string message;
};

constexpr int kMaxBits = 15;
constexpr int kLiteralCodes = 288;
constexpr int kDistanceCodes = 30;

struct Huffman {
    std::array<std::uint16_t, kMaxBits + 1> count{};
    std::array<std::uint16_t, kLiteralCodes> symbol{};
};

const std::uint16_t kLengthBase[29] = {3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
                                       31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
const std::uint16_t kLengthExtra[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2,
                                        2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
const std::uint16_t kDistanceBase[30] = {1,   2,   3,   4,   5,   7,    9,    13,   17,   25,
                                         33,  49,  65,  97,  129, 193,  257,  385,  513,  769,
                                         1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
const std::uint16_t kDistanceExtra[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6,
                                          6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
const std::uint8_t kCodeLengthOrder[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};

class Inflater {
public:
    Inflater(const std::uint8_t* data, std::size_t size, std::size_t maxOutput)
        : data_(data), size_(size), maxOutput_(maxOutput) {}

    Bytes run() {
        bool last = false;
        do {
            last = bits(1) != 0;
            const unsigned type = bits(2);
            if (type == 0) {
                storedBlock();
            } else if (type == 1) {
                fixedBlock();
            } else if (type == 2) {
                dynamicBlock();
            } else {
                throw InflateFailure{"invalid deflate block type"};
            }
        } while (!last);
        return std::move(out_);
    }

private:
    unsigned bits(int need) {
        std::uint32_t value = bitBuffer_;
        while (bitCount_ < need) {
            if (position_ >= size_) throw InflateFailure{"unexpected end of deflate stream"};
            value |= static_cast<std::uint32_t>(data_[position_++]) << bitCount_;
            bitCount_ += 8;
        }
        bitBuffer_ = value >> need;
        bitCount_ -= need;
        return value & ((1u << need) - 1u);
    }

    void construct(Huffman& table, const std::uint16_t* lengths, int count) {
        table.count.fill(0);
        table.symbol.fill(0);
        for (int symbol = 0; symbol < count; ++symbol) table.count[lengths[symbol]]++;
        if (table.count[0] == count) return;
        int left = 1;
        for (int len = 1; len <= kMaxBits; ++len) {
            left <<= 1;
            left -= table.count[len];
            if (left < 0) throw InflateFailure{"over-subscribed huffman code"};
        }
        std::array<std::uint16_t, kMaxBits + 1> offsets{};
        offsets[1] = 0;
        for (int len = 1; len < kMaxBits; ++len) {
            offsets[len + 1] = static_cast<std::uint16_t>(offsets[len] + table.count[len]);
        }
        for (int symbol = 0; symbol < count; ++symbol) {
            if (lengths[symbol] != 0) table.symbol[offsets[lengths[symbol]]++] = static_cast<std::uint16_t>(symbol);
        }
    }

    int decode(const Huffman& table) {
        int code = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= kMaxBits; ++len) {
            code |= static_cast<int>(bits(1));
            const int count = table.count[len];
            if (code - count < first) return table.symbol[static_cast<std::size_t>(index + (code - first))];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateFailure{"invalid huffman code"};
    }

    void emit(std::uint8_t byte) {
        if (out_.size() >= maxOutput_) throw InflateFailure{"inflated data exceeds size limit"};
        out_.push_back(byte);
    }

    void codes(const Huffman& literals, const Huffman& distances) {
        while (true) {
            int symbol = decode(literals);
            if (symbol < 256) {
                emit(static_cast<std::uint8_t>(symbol));
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) throw InflateFailure{"invalid length symbol"};
                const int length = kLengthBase[symbol] + static_cast<int>(bits(kLengthExtra[symbol]));
                const int distSymbol = decode(distances);
                if (distSymbol >= kDistanceCodes) throw InflateFailure{"invalid distance symbol"};
                const std::size_t distance =
                    kDistanceBase[distSymbol] + bits(kDistanceExtra[distSymbol]);
                if (distance > out_.size()) throw InflateFailure{"distance beyond start of output"};
                if (out_.size() + static_cast<std::size_t>(length) > maxOutput_) {
                    throw InflateFailure{"inflated data exceeds size limit"};
                }
                std::size_t from = out_.size() - distance;
                for (int i = 0; i < length; ++i) out_.push_back(out_[from++]);
            }
        }
    }

    void storedBlock() {
        bitBuffer_ = 0;
        bitCount_ = 0;
        if (position_ + 4 > size_) throw InflateFailure{"truncated stored block header"};
        const unsigned length = data_[position_] | (static_cast<unsigned>(data_[position_ + 1]) << 8);
        const unsigned inverse = data_[position_ + 2] | (static_cast<unsigned>(data_[position_ + 3]) << 8);
        position_ += 4;
        if (length != (~inverse & 0xFFFFu)) throw InflateFailure{"stored block length mismatch"};
        if (position_ + length > size_) throw InflateFailure{"truncated stored block"};
        if (out_.size() + length > maxOutput_) throw InflateFailure{"inflated data exceeds size limit"};
        out_.insert(out_.end(), data_ + position_, data_ + position_ + length);
        position_ += length;
    }

    void fixedBlock() {
        static Huffman literals;
        static Huffman distances;
        static bool ready = false;
        if (!ready) {
            std::uint16_t lengths[kLiteralCodes];
            int symbol = 0;
            for (; symbol < 144; ++symbol) lengths[symbol] = 8;
            for (; symbol < 256; ++symbol) lengths[symbol] = 9;
            for (; symbol < 280; ++symbol) lengths[symbol] = 7;
            for (; symbol < kLiteralCodes; ++symbol) lengths[symbol] = 8;
            construct(literals, lengths, kLiteralCodes);
            for (symbol = 0; symbol < kDistanceCodes; ++symbol) lengths[symbol] = 5;
            construct(distances, lengths, kDistanceCodes);
            ready = true;
        }
        codes(literals, distances);
    }

    void dynamicBlock() {
        const int literalCount = static_cast<int>(bits(5)) + 257;
        const int distanceCount = static_cast<int>(bits(5)) + 1;
        const int codeCount = static_cast<int>(bits(4)) + 4;
        if (literalCount > 286 || distanceCount > kDistanceCodes) {
            throw InflateFailure{"too many length or distance codes"};
        }
        std::uint16_t lengths[320] = {};
        for (int i = 0; i < codeCount; ++i) lengths[kCodeLengthOrder[i]] = static_cast<std::uint16_t>(bits(3));
        Huffman lengthCode;
        construct(lengthCode, lengths, 19);
        int index = 0;
        std::uint16_t table[320] = {};
        while (index < literalCount + distanceCount) {
            int symbol = decode(lengthCode);
            if (symbol < 16) {
                table[index++] = static_cast<std::uint16_t>(symbol);
                continue;
            }
            std::uint16_t previous = 0;
            int repeat = 0;
            if (symbol == 16) {
                if (index == 0) throw InflateFailure{"repeat with no previous length"};
                previous = table[index - 1];
                repeat = 3 + static_cast<int>(bits(2));
            } else if (symbol == 17) {
                repeat = 3 + static_cast<int>(bits(3));
            } else {
                repeat = 11 + static_cast<int>(bits(7));
            }
            if (index + repeat > literalCount + distanceCount) throw InflateFailure{"too many code lengths"};
            while (repeat-- > 0) table[index++] = previous;
        }
        if (table[256] == 0) throw InflateFailure{"missing end-of-block code"};
        Huffman literals;
        Huffman distances;
        construct(literals, table, literalCount);
        construct(distances, table + literalCount, distanceCount);
        codes(literals, distances);
    }

    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t maxOutput_;
    std::size_t position_ = 0;
    std::uint32_t bitBuffer_ = 0;
    int bitCount_ = 0;
    Bytes out_;
};

const std::array<std::uint32_t, 256>& crcTable() {
    static const std::array<std::uint32_t, 256> table = [] {
        std::array<std::uint32_t, 256> built{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t value = i;
            for (int bit = 0; bit < 8; ++bit) value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            built[i] = value;
        }
        return built;
    }();
    return table;
}

}  // namespace

std::uint32_t crc32(const std::uint8_t* data, std::size_t size, std::uint32_t seed) {
    const auto& table = crcTable();
    std::uint32_t value = ~seed;
    for (std::size_t i = 0; i < size; ++i) value = table[(value ^ data[i]) & 0xFFu] ^ (value >> 8);
    return ~value;
}

std::uint32_t adler32(const std::uint8_t* data, std::size_t size) {
    std::uint32_t a = 1;
    std::uint32_t b = 0;
    for (std::size_t i = 0; i < size; ++i) {
        a = (a + data[i]) % 65521u;
        b = (b + a) % 65521u;
    }
    return (b << 16) | a;
}

Result<Bytes> inflateRaw(const std::uint8_t* data, std::size_t size, std::size_t maxOutput) {
    try {
        Inflater inflater(data, size, maxOutput);
        return inflater.run();
    } catch (const InflateFailure& failure) {
        return makeError(ErrorCode::Parse, failure.message);
    }
}

Result<Bytes> inflateZlib(const std::uint8_t* data, std::size_t size, std::size_t maxOutput) {
    if (size < 2) return makeError(ErrorCode::Parse, "zlib stream too short");
    const unsigned header = (static_cast<unsigned>(data[0]) << 8) | data[1];
    if ((data[0] & 0x0Fu) != 8 || header % 31 != 0) return makeError(ErrorCode::Parse, "invalid zlib header");
    if (data[1] & 0x20u) return makeError(ErrorCode::Unsupported, "zlib preset dictionary not supported");
    return inflateRaw(data + 2, size - 2, maxOutput);
}

Bytes deflateStored(const std::uint8_t* data, std::size_t size) {
    Bytes out;
    std::size_t offset = 0;
    do {
        const std::size_t chunk = size - offset > 65535 ? 65535 : size - offset;
        const bool last = offset + chunk >= size;
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(chunk & 0xFF));
        out.push_back(static_cast<std::uint8_t>((chunk >> 8) & 0xFF));
        out.push_back(static_cast<std::uint8_t>(~chunk & 0xFF));
        out.push_back(static_cast<std::uint8_t>((~chunk >> 8) & 0xFF));
        out.insert(out.end(), data + offset, data + offset + chunk);
        offset += chunk;
    } while (offset < size);
    return out;
}

namespace {

class BitWriter {
public:
    void put(std::uint32_t value, int count) {
        buffer_ |= static_cast<std::uint64_t>(value) << bits_;
        bits_ += count;
        while (bits_ >= 8) {
            out_.push_back(static_cast<std::uint8_t>(buffer_ & 0xFF));
            buffer_ >>= 8;
            bits_ -= 8;
        }
    }

    void putCode(std::uint32_t code, int length) {
        std::uint32_t reversed = 0;
        for (int i = 0; i < length; ++i) {
            reversed = (reversed << 1) | ((code >> i) & 1u);
        }
        put(reversed, length);
    }

    Bytes finish() {
        if (bits_ > 0) put(0, 8 - bits_);
        return std::move(out_);
    }

private:
    Bytes out_;
    std::uint64_t buffer_ = 0;
    int bits_ = 0;
};

void putLiteralLength(BitWriter& writer, int symbol) {
    if (symbol <= 143) writer.putCode(0x30u + static_cast<std::uint32_t>(symbol), 8);
    else if (symbol <= 255) writer.putCode(0x190u + static_cast<std::uint32_t>(symbol - 144), 9);
    else if (symbol <= 279) writer.putCode(static_cast<std::uint32_t>(symbol - 256), 7);
    else writer.putCode(0xC0u + static_cast<std::uint32_t>(symbol - 280), 8);
}

int lengthSymbol(int length) {
    for (int symbol = 28; symbol >= 0; --symbol) {
        if (length >= kLengthBase[symbol]) return symbol;
    }
    return 0;
}

int distanceSymbol(int distance) {
    for (int symbol = 29; symbol >= 0; --symbol) {
        if (distance >= kDistanceBase[symbol]) return symbol;
    }
    return 0;
}

}  // namespace

Bytes deflateCompress(const std::uint8_t* data, std::size_t size) {
    BitWriter writer;
    writer.put(1, 1);
    writer.put(1, 2);
    constexpr std::size_t kWindow = 32768;
    constexpr int kMaxMatch = 258;
    constexpr int kMaxChain = 48;
    constexpr std::size_t kHashSize = 1u << 15;
    std::vector<std::int64_t> head(kHashSize, -1);
    std::vector<std::int64_t> previous(size, -1);
    auto hashAt = [&](std::size_t i) {
        return ((static_cast<std::uint32_t>(data[i]) << 10) ^ (static_cast<std::uint32_t>(data[i + 1]) << 5) ^ data[i + 2]) & (kHashSize - 1);
    };
    auto insert = [&](std::size_t i) {
        if (i + 3 > size) return;
        const std::uint32_t h = hashAt(i);
        previous[i] = head[h];
        head[h] = static_cast<std::int64_t>(i);
    };
    std::size_t i = 0;
    while (i < size) {
        int bestLength = 0;
        int bestDistance = 0;
        if (i + 3 <= size) {
            std::int64_t candidate = head[hashAt(i)];
            int chain = 0;
            while (candidate >= 0 && chain++ < kMaxChain) {
                const std::size_t distance = i - static_cast<std::size_t>(candidate);
                if (distance > kWindow) break;
                int length = 0;
                const int limit = static_cast<int>(std::min<std::size_t>(kMaxMatch, size - i));
                while (length < limit && data[static_cast<std::size_t>(candidate) + static_cast<std::size_t>(length)] == data[i + static_cast<std::size_t>(length)]) ++length;
                if (length > bestLength) {
                    bestLength = length;
                    bestDistance = static_cast<int>(distance);
                    if (length == limit) break;
                }
                candidate = previous[static_cast<std::size_t>(candidate)];
            }
        }
        if (bestLength >= 3) {
            const int lengthCode = lengthSymbol(bestLength);
            putLiteralLength(writer, 257 + lengthCode);
            if (kLengthExtra[lengthCode] > 0) writer.put(static_cast<std::uint32_t>(bestLength - kLengthBase[lengthCode]), kLengthExtra[lengthCode]);
            const int distanceCode = distanceSymbol(bestDistance);
            writer.putCode(static_cast<std::uint32_t>(distanceCode), 5);
            if (kDistanceExtra[distanceCode] > 0) writer.put(static_cast<std::uint32_t>(bestDistance - kDistanceBase[distanceCode]), kDistanceExtra[distanceCode]);
            for (int k = 0; k < bestLength; ++k) insert(i + static_cast<std::size_t>(k));
            i += static_cast<std::size_t>(bestLength);
        } else {
            putLiteralLength(writer, data[i]);
            insert(i);
            ++i;
        }
    }
    putLiteralLength(writer, 256);
    return writer.finish();
}

Bytes zlibWrapCompressed(const std::uint8_t* data, std::size_t size) {
    Bytes out = {0x78, 0x9C};
    Bytes body = deflateCompress(data, size);
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t checksum = adler32(data, size);
    out.push_back(static_cast<std::uint8_t>(checksum >> 24));
    out.push_back(static_cast<std::uint8_t>(checksum >> 16));
    out.push_back(static_cast<std::uint8_t>(checksum >> 8));
    out.push_back(static_cast<std::uint8_t>(checksum));
    return out;
}

Bytes zlibWrapStored(const std::uint8_t* data, std::size_t size) {
    Bytes out = {0x78, 0x01};
    Bytes body = deflateStored(data, size);
    out.insert(out.end(), body.begin(), body.end());
    const std::uint32_t checksum = adler32(data, size);
    out.push_back(static_cast<std::uint8_t>(checksum >> 24));
    out.push_back(static_cast<std::uint8_t>(checksum >> 16));
    out.push_back(static_cast<std::uint8_t>(checksum >> 8));
    out.push_back(static_cast<std::uint8_t>(checksum));
    return out;
}

}  // namespace eden
