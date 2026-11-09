// =============================================================================
//  Abrasion Labs // OMNIS
//  link/SharedRegion.cpp
// =============================================================================
#include "SharedRegion.h"

#if defined(_WIN32)
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <fcntl.h>
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace omnis::link {

#if defined(_WIN32)

bool SharedRegion::open(const std::string& name, std::size_t size) {
    close();
    const std::wstring wide = L"Local\\" + std::wstring(name.begin(), name.end());
    const auto high = static_cast<DWORD>(static_cast<unsigned long long>(size) >> 32);
    const auto low = static_cast<DWORD>(size & 0xFFFFFFFFull);
    // Mémoire du fichier d'échange : remplie de zéros à la création.
    HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, high, low, wide.c_str());
    if (!mapping) return false;
    void* view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, size);
    if (!view) {
        CloseHandle(mapping);
        return false;
    }
    mapping_ = mapping;
    data_ = view;
    size_ = size;
    return true;
}

void SharedRegion::close() noexcept {
    if (data_) UnmapViewOfFile(data_);
    if (mapping_) CloseHandle(static_cast<HANDLE>(mapping_));
    data_ = nullptr;
    mapping_ = nullptr;
    size_ = 0;
}

void SharedRegion::unlink(const std::string&) noexcept {}

#else

bool SharedRegion::open(const std::string& name, std::size_t size) {
    close();
    const std::string path = "/" + name;
    const int fd = shm_open(path.c_str(), O_RDWR | O_CREAT, 0600);
    if (fd < 0) return false;
    struct stat st {};
    bool ok = fstat(fd, &st) == 0;
    // Une région neuve (taille 0) est dimensionnée, donc remplie de zéros.
    // Deux créateurs simultanés : macOS refuse le second ftruncate, la
    // taille relue suffit alors.
    if (ok && static_cast<std::size_t>(st.st_size) < size) {
        if (ftruncate(fd, static_cast<off_t>(size)) != 0) ok = fstat(fd, &st) == 0 && static_cast<std::size_t>(st.st_size) >= size;
    }
    void* view = ok ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
    ::close(fd); // le mappage garde la région
    if (view == MAP_FAILED) return false;
    data_ = view;
    size_ = size;
    return true;
}

void SharedRegion::close() noexcept {
    if (data_) munmap(data_, size_);
    data_ = nullptr;
    size_ = 0;
}

void SharedRegion::unlink(const std::string& name) noexcept { shm_unlink(("/" + name).c_str()); }

#endif

} // namespace omnis::link
