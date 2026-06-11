#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <system_error>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace maritime {

// ---------------------------------------------------------------------------
// MmapRegion
//
// Rule of Five: the ONE type in this codebase that cannot use Rule of Zero.
// mmap() returns a void* that is NOT heap memory — delete/free must never
// be called on it. No standard smart pointer models this correctly, so we
// own the resource explicitly.
//
// Invariant: ptr_ == nullptr IFF the region is in the moved-from state.
//            MAP_FAILED is never stored; the constructor throws instead.
// ---------------------------------------------------------------------------
class MmapRegion {
public:
    // Opens path, stats its size, maps it MAP_SHARED | MAP_POPULATE read-only.
    // Throws std::system_error on any OS failure.
    explicit MmapRegion(const std::string& path)
    {
        int fd = ::open(path.c_str(), O_RDONLY);
        if (fd < 0)
            throw std::system_error(errno, std::system_category(),
                                    "open: " + path);

        // RAII for the fd: always closed before this constructor returns,
        // whether by success or exception.
        struct FdGuard {
            int fd;
            ~FdGuard() { ::close(fd); }
        } guard{fd};

        struct stat st{};
        if (::fstat(fd, &st) < 0)
            throw std::system_error(errno, std::system_category(),
                                    "fstat: " + path);

        length_ = static_cast<std::size_t>(st.st_size);
        if (length_ == 0)
            throw std::invalid_argument("empty file: " + path);

        int map_flags = MAP_SHARED;
    #ifdef MAP_POPULATE
        map_flags |= MAP_POPULATE;
    #endif

        void* p = ::mmap(nullptr, length_, PROT_READ, map_flags, fd, 0);
        if (p == MAP_FAILED)
            throw std::system_error(errno, std::system_category(),
                                    "mmap: " + path);

        ptr_ = p;   // committed — destructor now owns this
    }

    // Rule of Five — explicit, move-only
    ~MmapRegion()
    {
        if (ptr_)
            ::munmap(ptr_, length_);
    }

    MmapRegion(const MmapRegion&)            = delete;
    MmapRegion& operator=(const MmapRegion&) = delete;

    MmapRegion(MmapRegion&& other) noexcept
        : ptr_(other.ptr_), length_(other.length_)
    {
        other.ptr_    = nullptr;
        other.length_ = 0;
    }

    MmapRegion& operator=(MmapRegion&& other) noexcept
    {
        if (this != &other) {
            if (ptr_)
                ::munmap(ptr_, length_);
            ptr_          = other.ptr_;
            length_       = other.length_;
            other.ptr_    = nullptr;
            other.length_ = 0;
        }
        return *this;
    }

    // ---------------------------------------------------------------------------
    // Typed view helpers — non-owning spans derived from the mapped region.
    // The span lifetime is bounded by this MmapRegion; callers must not
    // outlive it.
    // ---------------------------------------------------------------------------
    [[nodiscard]] const void*  data()   const noexcept { return ptr_;    }
    [[nodiscard]] std::size_t  size()   const noexcept { return length_; }

    template <typename T>
    [[nodiscard]] std::span<const T> as_span(
        std::size_t byte_offset = 0,
        std::size_t count       = std::dynamic_extent) const
    {
        const auto* base =
            reinterpret_cast<const T*>(
                static_cast<const std::byte*>(ptr_) + byte_offset);

        const std::size_t available = (length_ - byte_offset) / sizeof(T);
        const std::size_t n =
            (count == std::dynamic_extent) ? available : count;

        if (n > available)
            throw std::out_of_range("MmapRegion::as_span: count exceeds region");

        return std::span<const T>{base, n};
    }

private:
    void*       ptr_    = nullptr;
    std::size_t length_ = 0;
};

} // namespace maritime
