#pragma once

#include <windows.h>
#include <type_traits>
#include <vector>
#include <cstdint>

namespace er {
class MemoryHandle {
public:
    /**
     * \brief Constructs the object with a pointer
     * \param p The pointer to initialize the object with, optional
     */
    constexpr MemoryHandle(void *p = nullptr):
        ptr_(p) {}

    /**
     * \brief Constructs the object with an integral pointer
     * \param p The integral pointer to initialize the object with
     */
    explicit MemoryHandle(std::uintptr_t p):
        ptr_(reinterpret_cast<void *>(p)) {}

    /**
     * \brief Retrieves the pointer as a pointer
     * \return A pointer
     */
    template<typename T>
    constexpr std::enable_if_t<std::is_pointer_v<T>, T> as() {
        return static_cast<T>(ptr_);
    }

    /**
     * \brief Retrieves the pointer as a reference
     * \return A pointer
     */
    template<typename T>
    constexpr std::enable_if_t<std::is_lvalue_reference_v<T>, T> as() {
        return *static_cast<std::add_pointer_t<std::remove_reference_t<T>>>(ptr_);
    }

    /**
     * \brief Retrieves the pointer as an integral pointer
     * \return An integral pointer
     */
    template<typename T>
    constexpr std::enable_if_t<std::is_same_v<T, std::uintptr_t>, T> as() {
        return reinterpret_cast<T>(ptr_);
    }

    /**
     * \brief Adds an offset to the pointer
     * \param offset The offset to be added
     * \return MemoryHandle
     */
    template<typename T>
    constexpr MemoryHandle add(T offset) {
        return MemoryHandle(as<std::uintptr_t>() + offset);
    }

    /**
     * \brief Subtracted an offset to the pointer
     * \param offset The offset to be subtracted
     * \return MemoryHandle
     */
    template<typename T>
    constexpr MemoryHandle sub(T offset) {
        return MemoryHandle(as<std::uintptr_t>() - offset);
    }

    /**
     * \brief Gets the relative displacement of the pointer
     * \return MemoryHandle
     */
    constexpr MemoryHandle rip() {
        if (!ptr_)
            return nullptr;
        return add(as<std::int32_t &>()).add(4U);
    }

    /**
     * \brief Checks if the stored pointer is valid
     * \return bool
     */
    constexpr explicit operator bool() noexcept {
        return ptr_;
    }
protected:
    void *ptr_;
};

class MemoryRegion {
public:
    /**
     * \brief Constructs the region
     * \param base The base of the region
     * \param size The size of the region
     */
    constexpr explicit MemoryRegion(MemoryHandle base, std::size_t size):
        base_(base),
        size_(size) {}

    /**
     * \brief Gets the base of the region
     * \return MemoryHandle
     */
    constexpr MemoryHandle base() {
        return base_;
    }

    /**
     * \brief Gets the end of the region
     * \return MemoryHandle
     */
    constexpr MemoryHandle end() {
        return base_.add(size_);
    }

    /**
     * \brief Gets the size of the region
     * \return std::size_t
     */
    constexpr std::size_t size() {
        return size_;
    }

    /**
     * \brief Checks if p is within this region
     * \param p The pointer
     * \return bool
     */
    constexpr bool contains(MemoryHandle p) {
        if (p.as<std::uintptr_t>() < base_.as<std::uintptr_t>())
            return false;
        if (p.as<std::uintptr_t>() > base_.add(size_).as<std::uintptr_t>())
            return false;

        return true;
    }
protected:
    MemoryHandle base_;
    std::size_t size_;
};

class Module: public MemoryRegion {
public:
    /**
     * \brief Constructs the class with the main module
     */
    explicit Module(std::nullptr_t):
        Module(static_cast<char *>(nullptr)) {}

    /**
     * \brief Constructs the class with a module name
     * \param name The name of the module
     */
    explicit Module(const char *name):
        Module(GetModuleHandleA(name)) {
    }

    /**
     * \brief Constructs the class with a module base
     */
    Module(HMODULE hmod):
        MemoryRegion(hmod, 0) {
        auto dosHeader = base_.as<IMAGE_DOS_HEADER *>();
        auto ntHeader = base_.add(dosHeader->e_lfanew).as<IMAGE_NT_HEADERS64 *>();
        size_ = ntHeader->OptionalHeader.SizeOfImage;
    }

    /**
     * \brief Gets the DOS headers for the module
     */
    IMAGE_DOS_HEADER *getDosHeaders() {
        return base_.as<IMAGE_DOS_HEADER *>();
    }

    /**
     * \brief Gets the NT headers for the module
     */
    IMAGE_NT_HEADERS64 *GetNtHeaders() {
        return base_.add(base_.as<IMAGE_DOS_HEADER *>()->e_lfanew).as<IMAGE_NT_HEADERS64 *>();
    }
private:
    /**
     * \brief Transforms a relative virtual address to a virtual address
     */
    template<typename TReturn, typename TOffset>
    TReturn GetRVA(TOffset rva) {
        return base_.add(rva).template as<TReturn>();
    }
};

class Signature {
public:
    struct Element {
        std::uint8_t m_Data{};
        bool m_Wildcard{};
    };

    /**
     * \brief Constructs the signature with an IDA pattern
     * \param pattern The IDA pattern string
     */
    explicit Signature(const char *pattern) {
        auto toUpper = [](char c) -> char {
            return c >= 'a' && c <= 'z' ? static_cast<char>(c + ('A' - 'a')) : static_cast<char>(c);
        };

        auto isHex = [&toUpper](char c) -> bool {
            switch (toUpper(c)) {
            case '0':
            case '1':
            case '2':
            case '3':
            case '4':
            case '5':
            case '6':
            case '7':
            case '8':
            case '9':
            case 'A':
            case 'B':
            case 'C':
            case 'D':
            case 'E':
            case 'F':
                return true;
            default:
                return false;
            }
        };

        do {
            if (*pattern == ' ')
                continue;
            if (*pattern == '?') {
                elements_.push_back(Element{{}, true});
                if (*(pattern + 1) == '?')
                    pattern++;
                continue;
            }

            if (*(pattern + 1) && isHex(*pattern) && isHex(*(pattern + 1))) {
                char str[3] = {*pattern, *(pattern + 1), '\0'};
                auto data = std::strtol(str, nullptr, 16);

                elements_.push_back(Element{static_cast<std::uint8_t>(data), false});
                pattern++;
            }
        } while (*(pattern++));
    }

    /**
     * \brief Scans for the pattern in a memory region
     * \param region The region to search in, default is the main module
     * \return MemoryHandle
     */
    MemoryHandle scan(MemoryRegion region = Module(nullptr)) {
        auto compareMemory = [](std::uint8_t *data, Element *elem, std::size_t num) -> bool {
            for (std::size_t i = 0; i < num; ++i) {
                if (!elem[i].m_Wildcard)
                    if (data[i] != elem[i].m_Data)
                        return false;
            }

            return true;
        };

        for (std::uintptr_t i = region.base().as<std::uintptr_t>(), end = region.end().as<std::uintptr_t>(); i != end;
             ++i) {
            if (compareMemory(reinterpret_cast<std::uint8_t *>(i), elements_.data(), elements_.size())) {
                return MemoryHandle(i);
            }
        }

        return {};
    }
private:
    std::vector<Element> elements_;
};
}