#include "shared_preview.h"
#include <iostream>
#include <cstring>
#include <chrono>

#if defined(__APPLE__) || defined(__linux__)
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
    #define POSIX_SHM 1
#elif defined(_WIN32)
    #include <windows.h>
    #define WIN32_SHM 1
#endif

namespace vivid {

SharedPreview::SharedPreview() = default;

SharedPreview::~SharedPreview() {
    close();
}

bool SharedPreview::create(const std::string& name) {
    if (memory_) {
        close();
    }

#if defined(POSIX_SHM)
    // POSIX shared memory names must start with /
    name_ = "/" + name;
    isCreator_ = true;

    // Remove any existing segment
    shm_unlink(name_.c_str());

    // Create shared memory
    int fd = shm_open(name_.c_str(), O_CREAT | O_RDWR, 0666);
    if (fd < 0) {
        std::cerr << "[SharedPreview] Failed to create shared memory: " << name_ << "\n";
        return false;
    }

    // Set size
    if (ftruncate(fd, SHARED_PREVIEW_SIZE) < 0) {
        std::cerr << "[SharedPreview] Failed to set shared memory size\n";
        ::close(fd);
        shm_unlink(name_.c_str());
        return false;
    }

    // Map into address space
    void* ptr = mmap(nullptr, SHARED_PREVIEW_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    ::close(fd);  // Can close fd after mmap

    if (ptr == MAP_FAILED) {
        std::cerr << "[SharedPreview] Failed to map shared memory\n";
        shm_unlink(name_.c_str());
        return false;
    }

    memory_ = static_cast<SharedPreviewMemory*>(ptr);
    handle_ = reinterpret_cast<void*>(static_cast<intptr_t>(1));  // Just mark as valid

    // Initialize header
    std::memset(memory_, 0, SHARED_PREVIEW_SIZE);
    memory_->header.magic = PREVIEW_MAGIC;
    memory_->header.version = PREVIEW_VERSION;
    memory_->header.operatorCount = 0;
    memory_->header.frameNumber = 0;
    setTimestamp();

    std::cout << "[SharedPreview] Created shared memory: " << name_
              << " (" << SHARED_PREVIEW_SIZE << " bytes)\n";
    return true;

#elif defined(WIN32_SHM)
    name_ = name;
    isCreator_ = true;

    // Create file mapping
    HANDLE hMapFile = CreateFileMappingA(
        INVALID_HANDLE_VALUE,
        nullptr,
        PAGE_READWRITE,
        0,
        static_cast<DWORD>(SHARED_PREVIEW_SIZE),
        name_.c_str()
    );

    if (hMapFile == nullptr) {
        std::cerr << "[SharedPreview] Failed to create file mapping\n";
        return false;
    }

    // Map view
    void* ptr = MapViewOfFile(hMapFile, FILE_MAP_ALL_ACCESS, 0, 0, SHARED_PREVIEW_SIZE);
    if (ptr == nullptr) {
        std::cerr << "[SharedPreview] Failed to map view of file\n";
        CloseHandle(hMapFile);
        return false;
    }

    memory_ = static_cast<SharedPreviewMemory*>(ptr);
    handle_ = hMapFile;

    // Initialize header
    std::memset(memory_, 0, SHARED_PREVIEW_SIZE);
    memory_->header.magic = PREVIEW_MAGIC;
    memory_->header.version = PREVIEW_VERSION;

    std::cout << "[SharedPreview] Created shared memory: " << name_ << "\n";
    return true;

#else
    std::cerr << "[SharedPreview] Shared memory not supported on this platform\n";
    return false;
#endif
}

bool SharedPreview::open(const std::string& name) {
    if (memory_) {
        close();
    }

#if defined(POSIX_SHM)
    name_ = "/" + name;
    isCreator_ = false;

    int fd = shm_open(name_.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }

    void* ptr = mmap(nullptr, SHARED_PREVIEW_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    ::close(fd);

    if (ptr == MAP_FAILED) {
        return false;
    }

    memory_ = static_cast<SharedPreviewMemory*>(ptr);

    // Validate magic
    if (memory_->header.magic != PREVIEW_MAGIC) {
        munmap(memory_, SHARED_PREVIEW_SIZE);
        memory_ = nullptr;
        return false;
    }

    handle_ = reinterpret_cast<void*>(static_cast<intptr_t>(1));
    return true;

#elif defined(WIN32_SHM)
    name_ = name;
    isCreator_ = false;

    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, name_.c_str());
    if (hMapFile == nullptr) {
        return false;
    }

    void* ptr = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, SHARED_PREVIEW_SIZE);
    if (ptr == nullptr) {
        CloseHandle(hMapFile);
        return false;
    }

    memory_ = static_cast<SharedPreviewMemory*>(ptr);

    // Validate magic
    if (memory_->header.magic != PREVIEW_MAGIC) {
        UnmapViewOfFile(memory_);
        CloseHandle(hMapFile);
        memory_ = nullptr;
        return false;
    }

    handle_ = hMapFile;
    return true;

#else
    return false;
#endif
}

void SharedPreview::close() {
    if (!memory_) return;

#if defined(POSIX_SHM)
    munmap(memory_, SHARED_PREVIEW_SIZE);
    if (isCreator_ && !name_.empty()) {
        shm_unlink(name_.c_str());
        std::cout << "[SharedPreview] Removed shared memory: " << name_ << "\n";
    }

#elif defined(WIN32_SHM)
    UnmapViewOfFile(memory_);
    if (handle_) {
        CloseHandle(static_cast<HANDLE>(handle_));
    }
#endif

    memory_ = nullptr;
    handle_ = nullptr;
    name_.clear();
    isCreator_ = false;
}

void SharedPreview::setOperatorCount(uint32_t count) {
    if (memory_) {
        memory_->header.operatorCount = std::min(count, static_cast<uint32_t>(PREVIEW_MAX_OPERATORS));
    }
}

void SharedPreview::incrementFrame() {
    if (memory_) {
        memory_->header.frameNumber++;
        setTimestamp();
    }
}

void SharedPreview::setTimestamp() {
    if (memory_) {
        auto now = std::chrono::system_clock::now();
        auto us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count();
        memory_->header.timestampUs = static_cast<uint64_t>(us);
    }
}

void SharedPreview::updateTextureSlot(int index, const std::string& operatorId, int sourceLine,
                                       int origWidth, int origHeight,
                                       const uint8_t* rgbPixels, int thumbWidth, int thumbHeight) {
    if (!memory_ || index < 0 || index >= PREVIEW_MAX_OPERATORS) return;

    SharedPreviewSlot& slot = memory_->slots[index];

    // Copy operator ID (truncate if needed)
    std::strncpy(slot.operatorId, operatorId.c_str(), sizeof(slot.operatorId) - 1);
    slot.operatorId[sizeof(slot.operatorId) - 1] = '\0';

    slot.sourceLine = sourceLine;
    slot.frameNumber = memory_->header.frameNumber;
    slot.width = origWidth;
    slot.height = origHeight;
    slot.kind = static_cast<uint8_t>(PreviewKind::Texture);

    // Copy/scale pixels to fixed thumbnail size
    if (thumbWidth == PREVIEW_THUMB_WIDTH && thumbHeight == PREVIEW_THUMB_HEIGHT) {
        // Direct copy
        std::memcpy(slot.data.pixels, rgbPixels, PREVIEW_THUMB_SIZE);
    } else {
        // Scale to fixed size using nearest neighbor
        for (int y = 0; y < PREVIEW_THUMB_HEIGHT; y++) {
            int srcY = y * thumbHeight / PREVIEW_THUMB_HEIGHT;
            for (int x = 0; x < PREVIEW_THUMB_WIDTH; x++) {
                int srcX = x * thumbWidth / PREVIEW_THUMB_WIDTH;
                int srcIdx = (srcY * thumbWidth + srcX) * 3;
                int dstIdx = (y * PREVIEW_THUMB_WIDTH + x) * 3;
                slot.data.pixels[dstIdx] = rgbPixels[srcIdx];
                slot.data.pixels[dstIdx + 1] = rgbPixels[srcIdx + 1];
                slot.data.pixels[dstIdx + 2] = rgbPixels[srcIdx + 2];
            }
        }
    }

    // Mark as ready (do this last for memory ordering)
    slot.ready = 1;
}

void SharedPreview::updateValueSlot(int index, const std::string& operatorId, int sourceLine, float value) {
    if (!memory_ || index < 0 || index >= PREVIEW_MAX_OPERATORS) return;

    SharedPreviewSlot& slot = memory_->slots[index];

    std::strncpy(slot.operatorId, operatorId.c_str(), sizeof(slot.operatorId) - 1);
    slot.operatorId[sizeof(slot.operatorId) - 1] = '\0';

    slot.sourceLine = sourceLine;
    slot.frameNumber = memory_->header.frameNumber;
    slot.width = 0;
    slot.height = 0;
    slot.kind = static_cast<uint8_t>(PreviewKind::Value);
    slot.data.valueData.value = value;
    slot.ready = 1;
}

void SharedPreview::updateValueArraySlot(int index, const std::string& operatorId, int sourceLine,
                                          const float* values, uint32_t count) {
    if (!memory_ || index < 0 || index >= PREVIEW_MAX_OPERATORS) return;

    SharedPreviewSlot& slot = memory_->slots[index];

    std::strncpy(slot.operatorId, operatorId.c_str(), sizeof(slot.operatorId) - 1);
    slot.operatorId[sizeof(slot.operatorId) - 1] = '\0';

    slot.sourceLine = sourceLine;
    slot.frameNumber = memory_->header.frameNumber;
    slot.width = 0;
    slot.height = 0;
    slot.kind = static_cast<uint8_t>(PreviewKind::ValueArray);

    // Limit to array capacity
    uint32_t copyCount = std::min(count, static_cast<uint32_t>(12287));
    slot.data.arrayData.count = copyCount;
    std::memcpy(slot.data.arrayData.values, values, copyCount * sizeof(float));
    slot.ready = 1;
}

void SharedPreview::clearSlot(int index) {
    if (!memory_ || index < 0 || index >= PREVIEW_MAX_OPERATORS) return;
    memory_->slots[index].ready = 0;
}

} // namespace vivid
