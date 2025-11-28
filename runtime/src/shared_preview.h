#pragma once
#include <string>
#include <cstdint>

namespace vivid {

// Fixed thumbnail size for predictable memory layout
constexpr int PREVIEW_THUMB_WIDTH = 128;
constexpr int PREVIEW_THUMB_HEIGHT = 128;
constexpr int PREVIEW_THUMB_CHANNELS = 3;  // RGB
constexpr int PREVIEW_THUMB_SIZE = PREVIEW_THUMB_WIDTH * PREVIEW_THUMB_HEIGHT * PREVIEW_THUMB_CHANNELS;
constexpr int PREVIEW_MAX_OPERATORS = 64;
constexpr uint32_t PREVIEW_MAGIC = 0x56495644;  // 'VIVD'
constexpr uint32_t PREVIEW_VERSION = 1;

// Output kind enum (must match vivid::OutputKind)
enum class PreviewKind : uint8_t {
    Texture = 0,
    Value = 1,
    ValueArray = 2,
    Geometry = 3
};

// Header at start of shared memory
struct SharedPreviewHeader {
    uint32_t magic;              // 'VIVD' for validation
    uint32_t version;            // Protocol version
    uint32_t operatorCount;      // Number of active operators
    uint32_t frameNumber;        // Current frame for sync
    uint64_t timestampUs;        // Microseconds since epoch
    uint32_t padding[2];         // Alignment padding
};

// Per-operator slot in shared memory (fixed size for indexing)
struct SharedPreviewSlot {
    char operatorId[64];         // Operator name (null-terminated)
    int32_t sourceLine;          // Source file line number
    uint32_t frameNumber;        // Frame when this was captured
    uint32_t width;              // Original texture width
    uint32_t height;             // Original texture height
    uint8_t kind;                // PreviewKind enum
    uint8_t ready;               // 1 if data is valid, 0 if stale
    uint8_t padding[2];          // Alignment

    // Data union - only one is valid based on kind
    union {
        uint8_t pixels[PREVIEW_THUMB_SIZE];   // RGB thumbnail (49152 bytes)
        struct {
            float value;                       // Single value
            float padding[12287];              // Pad to same size as pixels
        } valueData;
        struct {
            uint32_t count;                    // Number of values
            float values[12287];               // Value array (up to 12287 floats)
        } arrayData;
    } data;
};

// Total shared memory layout
struct SharedPreviewMemory {
    SharedPreviewHeader header;
    SharedPreviewSlot slots[PREVIEW_MAX_OPERATORS];
};

// Calculate total size
constexpr size_t SHARED_PREVIEW_SIZE = sizeof(SharedPreviewMemory);

class SharedPreview {
public:
    SharedPreview();
    ~SharedPreview();

    // Non-copyable
    SharedPreview(const SharedPreview&) = delete;
    SharedPreview& operator=(const SharedPreview&) = delete;

    // Create shared memory (runtime calls this)
    bool create(const std::string& name);

    // Open existing shared memory (extension calls this)
    bool open(const std::string& name);

    // Close and cleanup
    void close();

    // Check if open
    bool isOpen() const { return memory_ != nullptr; }

    // Access the shared memory directly
    SharedPreviewMemory* memory() { return memory_; }
    const SharedPreviewMemory* memory() const { return memory_; }

    // Convenience methods for runtime
    void setOperatorCount(uint32_t count);
    void incrementFrame();
    void setTimestamp();

    // Update a texture slot with RGB thumbnail data
    void updateTextureSlot(int index, const std::string& operatorId, int sourceLine,
                           int origWidth, int origHeight,
                           const uint8_t* rgbPixels, int thumbWidth, int thumbHeight);

    // Update a value slot
    void updateValueSlot(int index, const std::string& operatorId, int sourceLine, float value);

    // Update a value array slot
    void updateValueArraySlot(int index, const std::string& operatorId, int sourceLine,
                              const float* values, uint32_t count);

    // Mark a slot as not ready (stale)
    void clearSlot(int index);

    // Get the shared memory name
    const std::string& name() const { return name_; }

private:
    void* handle_ = nullptr;            // Platform-specific handle (fd on POSIX)
    SharedPreviewMemory* memory_ = nullptr;
    std::string name_;
    bool isCreator_ = false;
};

} // namespace vivid
