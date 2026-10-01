#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>

#include "assets.hpp"

namespace er::util {

bool assetRange(Bytes bytes, size_t offset, size_t size, Bytes &result) {
    if (offset > bytes.size() || size > bytes.size() - offset)
        return false;
    result = bytes.subspan(offset, size);
    return true;
}

bool AssetReader::take(size_t size, Bytes &result) {
    if (!assetRange(bytes_, position_, size, result))
        return false;
    position_ += size;
    return true;
}

bool AssetReader::seek(size_t position) {
    if (position > bytes_.size())
        return false;
    position_ = position;
    return true;
}

bool AssetReader::text(std::string &value) {
    if (position_ > bytes_.size())
        return false;
    auto start = bytes_.begin() + position_;
    auto end = std::find(start, bytes_.end(), 0);
    if (end == bytes_.end())
        return false;
    value.assign(reinterpret_cast<const char *>(bytes_.data() + position_), static_cast<size_t>(end - start));
    position_ += value.size() + 1;
    return true;
}

bool AssetReader::text8(std::string &value) {
    uint8_t length;
    Bytes data;
    if (!read(length) || !take(length, data))
        return false;
    while (!data.empty() && data.back() == 0)
        data = data.first(data.size() - 1);
    value.assign(reinterpret_cast<const char *>(data.data()), data.size());
    return true;
}

bool assetWideString(Bytes bytes, size_t offset, std::wstring &result) {
    if (offset % 2 || offset >= bytes.size())
        return false;
    result.clear();
    AssetReader reader(bytes, offset);
    for (size_t i = 0; i < 4096; ++i) {
        uint16_t c;
        if (!reader.read(c))
            return false;
        if (!c)
            return true;
        result.push_back(static_cast<wchar_t>(c));
    }
    return false;
}

std::string assetKey(std::string_view name) {
    auto slash = name.find_last_of("/\\");
    if (slash != std::string_view::npos)
        name.remove_prefix(slash + 1);
    auto dot = name.find_last_of('.');
    return std::string(name.substr(0, dot));
}

bool parseDds(Bytes bytes, DdsImage &image) {
    image = {};
    if (bytes.size() < 128 || std::memcmp(bytes.data(), "DDS ", 4))
        return false;
    uint32_t fields[31];
    std::memcpy(fields, bytes.data() + 4, sizeof(fields));
    uint32_t height = fields[2], width = fields[3], mips = std::max(fields[6], 1u);
    if (fields[0] != 124 || fields[18] != 32 || !width || !height || width > 16384 || height > 16384 || mips > static_cast<uint32_t>(std::bit_width(std::max(width, height))) ||
        fields[5] > 1 || fields[27])
        return false;
    uint32_t fourcc = fields[20], format = 0, blockBytes = 0;
    size_t offset = 128;
    if (fourcc == 0x30315844) {
        if (bytes.size() < 148)
            return false;
        uint32_t dx10[5];
        std::memcpy(dx10, bytes.data() + 128, sizeof(dx10));
        if (dx10[1] != 3 || dx10[2] & 4 || dx10[3] != 1)
            return false;
        format = dx10[0];
        offset = 148;
    } else if (fourcc == 0x31545844)
        format = 71;
    else if (fourcc == 0x33545844)
        format = 74;
    else if (fourcc == 0x35545844)
        format = 77;
    else
        return false;
    switch (format) {
        case 71:
        case 72:
            blockBytes = 8;
            break;
        case 74:
        case 75:
        case 77:
        case 78:
        case 98:
        case 99:
            blockBytes = 16;
            break;
        default:
            return false;
    }
    image.width = width;
    image.height = height;
    image.format = format;
    for (uint32_t i = 0; i < mips; ++i) {
        uint32_t rows = std::max(1u, (height + 3) / 4), rowBytes = std::max(1u, (width + 3) / 4) * blockBytes;
        size_t size = static_cast<size_t>(rows) * rowBytes;
        if (offset > bytes.size() || size > bytes.size() - offset) {
            image = {};
            return false;
        }
        image.levels.push_back({offset, rows, rowBytes});
        offset += size;
        width = std::max(width / 2, 1u);
        height = std::max(height / 2, 1u);
    }
    if (offset != bytes.size()) {
        image = {};
        return false;
    }
    return true;
}

bool parseTpf(Bytes bytes, std::vector<TpfEntry> &entries) {
    entries.clear();
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "TPF\0", 4) || bytes[12] || bytes[13] != 3)
        return false;
    uint32_t count;
    AssetReader reader(bytes, 8);
    if (!reader.read(count) || !count || count > 4096 || !reader.seek(bytes[15] & 1 ? 48 : 16))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset, size, nameOffset, extra;
        Bytes properties, dds;
        std::wstring name;
        if (!reader.read(offset) || !reader.read(size) || !reader.take(4, properties) || !reader.read(nameOffset) || !reader.read(extra) || properties[1] || extra > 4096 ||
            !assetWideString(bytes, nameOffset, name) || !assetRange(bytes, offset, size, dds))
            return false;
        entries.push_back({std::move(name), dds});
        for (uint32_t j = 0; j < extra; ++j) {
            uint32_t kind, length;
            Bytes ignored;
            if (!reader.read(kind) || !reader.read(length) || !reader.take(length, ignored))
                return false;
        }
    }
    return true;
}

bool findTpfDds(Bytes bytes, std::wstring_view name, Bytes &dds) {
    dds = {};
    if (bytes.size() < 16 || std::memcmp(bytes.data(), "TPF\0", 4) || bytes[12] || bytes[13] != 3 || name.size() > 4096)
        return false;
    uint32_t count;
    AssetReader reader(bytes, 8);
    if (!reader.read(count) || !count || count > 4096 || !reader.seek(bytes[15] & 1 ? 48 : 16))
        return false;
    bool found = false;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t offset, size, nameOffset, extra;
        Bytes properties, data;
        if (!reader.read(offset) || !reader.read(size) || !reader.take(4, properties) || !reader.read(nameOffset) || !reader.read(extra) || properties[1] || extra > 4096 ||
            nameOffset % 2 || nameOffset >= bytes.size() || !assetRange(bytes, offset, size, data))
            return false;
        AssetReader text(bytes, nameOffset);
        bool equal = true, terminated = false;
        for (size_t character = 0; character <= 4096; ++character) {
            uint16_t value;
            if (!text.read(value))
                return false;
            if (!value) {
                equal = equal && character == name.size();
                terminated = true;
                break;
            }
            if (character >= name.size() || name[character] != value)
                equal = false;
        }
        if (!terminated)
            return false;
        if (equal) {
            if (found)
                return false;
            dds = data;
            found = true;
        }
        for (uint32_t j = 0; j < extra; ++j) {
            uint32_t kind, length;
            Bytes ignored;
            if (!reader.read(kind) || !reader.read(length) || !reader.take(length, ignored))
                return false;
        }
    }
    return found;
}

bool parseBinder(Bytes bytes, std::vector<BinderEntry> &entries) {
    entries.clear();
    const uint8_t flags[8] = {0, 0, 0, 0, 0, 0, 1, 0};
    bool index = bytes.size() >= 64 && !std::memcmp(bytes.data(), "BHF4", 4);
    if (bytes.size() < 64 || (!index && std::memcmp(bytes.data(), "BND4", 4)) || std::memcmp(bytes.data() + 4, flags, 8))
        return false;
    uint32_t count;
    uint64_t start, stride;
    AssetReader reader(bytes, 12);
    if (!reader.read(count) || !reader.read(start) || !reader.seek(32) || !reader.read(stride) || start != 64 || stride != 36 || count > 65536 ||
        count > (bytes.size() - 64) / 36 || !reader.seek(64))
        return false;
    for (uint32_t i = 0; i < count; ++i) {
        uint64_t compressed, uncompressed;
        uint32_t offset, id, nameOffset;
        Bytes ignored, data;
        std::wstring name;
        if (!reader.take(8, ignored) || !reader.read(compressed) || !reader.read(uncompressed) || !reader.read(offset) || !reader.read(id) || !reader.read(nameOffset) ||
            !assetWideString(bytes, nameOffset, name))
            return false;
        if (!index && (compressed != uncompressed || compressed > SIZE_MAX || !assetRange(bytes, offset, static_cast<size_t>(compressed), data)))
            return false;
        entries.push_back({std::move(name), id, data});
    }
    return true;
}

} // namespace er::util
