#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace er::util {

// PARAM row directories have 12- or 24-byte entries. A readable memory page
// is not a row boundary: use the next data/name offset or string section.
class ParamRows {
public:
    struct Row {
        uint32_t id;
        uintptr_t address;
        size_t size;
    };
    [[nodiscard]] bool open(uintptr_t table) {
        rows_.clear();
        snapshot_.clear();
        table_ = table;
        std::array<uint8_t, 48> header{};
        if (!copy(table, header.data(), header.size()) || header[0x2C])
            return false;
        uint16_t count;
        uint32_t strings;
        std::memcpy(&count, header.data() + 0xA, 2);
        std::memcpy(&strings, header.data(), 4);
        uint8_t format = header[0x2D] & 0x7F;
        if (!count || count > 20000 || format < 2 || format > 5)
            return false;
        size_t start = format == 2 ? 0x30 : 0x40;
        bool wide = format >= 4 && (header[0x2E] & 2) && (format == 4 || (header[0x2D] & 0x80));
        size_t stride = wide ? 24 : 12, end = start + size_t(count) * stride;
        if (end > MAX_OFFSET || table > UINTPTR_MAX - MAX_OFFSET)
            return false;
        // Native accessors append a sorted ID index at align16(table[-16]);
        // the stored file length also bounds a nameless final PARAM row.
        uint32_t fileSize = 0;
        bool sized = table >= 16 && copy(table - 16, &fileSize, sizeof(fileSize)) && fileSize >= end && fileSize <= MAX_OFFSET;
        // One copy of a bounded table replaces a system call per row read.
        // Rows outside an unavailable or partial snapshot are still copied live.
        if (sized && fileSize <= SNAPSHOT_BYTES) {
            snapshot_.resize(fileSize);
            if (!copy(table, snapshot_.data(), fileSize))
                snapshot_.clear();
        }
        std::vector<uint8_t> directory(size_t(count) * stride);
        if (!load(table + start, directory.data(), directory.size()))
            return false;
        struct Entry {
            uint32_t id;
            uint64_t offset, name;
        };
        std::vector<Entry> entries;
        std::vector<uint64_t> boundaries;
        if (strings >= end && strings <= MAX_OFFSET)
            boundaries.push_back(strings);
        if (sized)
            boundaries.push_back(fileSize);
        for (size_t i = 0; i < count; ++i) {
            const auto *data = directory.data() + i * stride;
            Entry entry{};
            std::memcpy(&entry.id, data, 4);
            std::memcpy(&entry.offset, data + (wide ? 8 : 4), wide ? 8 : 4);
            std::memcpy(&entry.name, data + (wide ? 16 : 8), wide ? 8 : 4);
            if (entry.offset < end || entry.offset > MAX_OFFSET || entry.name > MAX_OFFSET || (entry.name && entry.name < end))
                return false;
            entries.push_back(entry);
            boundaries.push_back(entry.offset);
            if (entry.name)
                boundaries.push_back(entry.name);
        }
        std::sort(boundaries.begin(), boundaries.end());
        for (const auto &entry: entries) {
            auto next = std::upper_bound(boundaries.begin(), boundaries.end(), entry.offset);
            size_t size = next == boundaries.end() ? 0 : static_cast<size_t>(*next - entry.offset);
            rows_.push_back({entry.id, table + static_cast<uintptr_t>(entry.offset), size});
        }
        return true;
    }
    [[nodiscard]] const std::vector<Row> &rows() const { return rows_; }
    template<typename T>
    [[nodiscard]] bool read(const Row &row, T &value, size_t minimum = sizeof(T)) const {
        value = {};
        return minimum <= sizeof(T) && row.size >= minimum && load(row.address, &value, std::min(row.size, sizeof(T)));
    }
    template<typename T>
    [[nodiscard]] bool field(const Row &row, size_t offset, T &value) const {
        return offset <= row.size && sizeof(T) <= row.size - offset && row.address <= UINTPTR_MAX - offset && load(row.address + offset, &value, sizeof(T));
    }

private:
    static constexpr size_t MAX_OFFSET = 0x10000000;
    static constexpr size_t SNAPSHOT_BYTES = 4 * 1024 * 1024;
    [[nodiscard]] static bool copy(uintptr_t address, void *value, size_t size) {
        SIZE_T copied = 0;
        return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address), value, size, &copied) && copied == size;
    }
    [[nodiscard]] bool load(uintptr_t address, void *value, size_t size) const {
        size_t offset = address - table_;
        if (address >= table_ && offset <= snapshot_.size() && size <= snapshot_.size() - offset) {
            std::memcpy(value, snapshot_.data() + offset, size);
            return true;
        }
        return copy(address, value, size);
    }
    std::vector<Row> rows_;
    std::vector<uint8_t> snapshot_;
    uintptr_t table_ = 0;
};

} // namespace er::util
