#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace er::util {

using Bytes = std::span<const uint8_t>;

class AssetReader {
public:
    explicit AssetReader(Bytes bytes, size_t position = 0) : bytes_(bytes), position_(position) {}
    [[nodiscard]] bool take(size_t size, Bytes &result);
    template<typename T>
    [[nodiscard]] bool read(T &value) {
        Bytes data;
        if (!take(sizeof(T), data))
            return false;
        std::memcpy(&value, data.data(), sizeof(T));
        return true;
    }
    [[nodiscard]] bool text(std::string &value);
    [[nodiscard]] bool text8(std::string &value);
    [[nodiscard]] size_t position() const { return position_; }
    [[nodiscard]] bool seek(size_t position);
    [[nodiscard]] Bytes bytes() const { return bytes_; }
    [[nodiscard]] bool finished() const { return position_ == bytes_.size(); }

private:
    Bytes bytes_;
    size_t position_;
};

struct DdsLevel {
    size_t offset;
    uint32_t rows;
    uint32_t rowBytes;
};

struct DdsImage {
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t format = 0; // DXGI_FORMAT; never the TPF format byte.
    std::vector<DdsLevel> levels;
};

struct TpfEntry {
    std::wstring name;
    Bytes dds;
};

struct BinderEntry {
    std::wstring name;
    uint32_t id;
    Bytes bytes; // Empty for a BHF4 index; offsets refer to its companion BDT.
};

[[nodiscard]] bool parseDds(Bytes bytes, DdsImage &image);
[[nodiscard]] bool parseTpf(Bytes bytes, std::vector<TpfEntry> &entries);
[[nodiscard]] bool findTpfDds(Bytes bytes, std::wstring_view name, Bytes &dds);
[[nodiscard]] bool parseBinder(Bytes bytes, std::vector<BinderEntry> &entries);
[[nodiscard]] bool assetRange(Bytes bytes, size_t offset, size_t size, Bytes &result);
[[nodiscard]] bool assetWideString(Bytes bytes, size_t offset, std::wstring &result);
[[nodiscard]] std::string assetKey(std::string_view name);

} // namespace er::util
