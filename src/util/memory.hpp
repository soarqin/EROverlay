#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <type_traits>
#include <cstdint>

namespace er::util {

class MemoryHandle {
public:
    /**
     * \brief Constructs the object with a pointer
     * \param p The pointer to initialize the object with, optional
     */
    constexpr MemoryHandle(void *p = nullptr) :
        ptr_(p) {}

    /**
     * \brief Constructs the object with an integral pointer
     * \param p The integral pointer to initialize the object with
     */
    explicit MemoryHandle(std::uintptr_t p) :
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
    constexpr explicit MemoryRegion(MemoryHandle base, std::size_t size) :
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

class Module : public MemoryRegion {
public:
    /**
     * \brief Constructs the class with the main module
     */
    explicit Module(std::nullptr_t) :
        Module(static_cast<char *>(nullptr)) {}

    /**
     * \brief Constructs the class with a module name
     * \param name The name of the module
     */
    explicit Module(const char *name) :
        Module(GetModuleHandleA(name)) {
    }

    /**
     * \brief Constructs the class with a module base
     */
    Module(HMODULE hmod) :
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

}