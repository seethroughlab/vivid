#include <napi.h>
#include <string>
#include <cstring>

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

// Mirror the shared memory structures from runtime
// Must match runtime/src/shared_preview.h exactly!

constexpr int PREVIEW_THUMB_WIDTH = 128;
constexpr int PREVIEW_THUMB_HEIGHT = 128;
constexpr int PREVIEW_THUMB_CHANNELS = 3;
constexpr int PREVIEW_THUMB_SIZE = PREVIEW_THUMB_WIDTH * PREVIEW_THUMB_HEIGHT * PREVIEW_THUMB_CHANNELS;
constexpr int PREVIEW_MAX_OPERATORS = 64;
constexpr uint32_t PREVIEW_MAGIC = 0x56495644;  // 'VIVD'

struct SharedPreviewHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t operatorCount;
    uint32_t frameNumber;
    uint64_t timestampUs;
    uint32_t padding[2];
};

struct SharedPreviewSlot {
    char operatorId[64];
    int32_t sourceLine;
    uint32_t frameNumber;
    uint32_t width;
    uint32_t height;
    uint8_t kind;
    uint8_t ready;
    uint8_t padding[2];

    union {
        uint8_t pixels[PREVIEW_THUMB_SIZE];
        struct {
            float value;
            float padding[12287];
        } valueData;
        struct {
            uint32_t count;
            float values[12287];
        } arrayData;
    } data;
};

struct SharedPreviewMemory {
    SharedPreviewHeader header;
    SharedPreviewSlot slots[PREVIEW_MAX_OPERATORS];
};

constexpr size_t SHARED_PREVIEW_SIZE = sizeof(SharedPreviewMemory);

// Global state
static SharedPreviewMemory* g_memory = nullptr;
static void* g_handle = nullptr;
static std::string g_name;

Napi::Value Open(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() < 1 || !info[0].IsString()) {
        Napi::TypeError::New(env, "String argument expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    std::string name = info[0].As<Napi::String>().Utf8Value();

    // Close existing if any
    if (g_memory) {
#if defined(POSIX_SHM)
        munmap(g_memory, SHARED_PREVIEW_SIZE);
#elif defined(WIN32_SHM)
        UnmapViewOfFile(g_memory);
        if (g_handle) CloseHandle(static_cast<HANDLE>(g_handle));
#endif
        g_memory = nullptr;
        g_handle = nullptr;
    }

#if defined(POSIX_SHM)
    g_name = "/" + name;

    int fd = shm_open(g_name.c_str(), O_RDONLY, 0);
    if (fd < 0) {
        return Napi::Boolean::New(env, false);
    }

    void* ptr = mmap(nullptr, SHARED_PREVIEW_SIZE, PROT_READ, MAP_SHARED, fd, 0);
    close(fd);

    if (ptr == MAP_FAILED) {
        return Napi::Boolean::New(env, false);
    }

    g_memory = static_cast<SharedPreviewMemory*>(ptr);

    if (g_memory->header.magic != PREVIEW_MAGIC) {
        munmap(g_memory, SHARED_PREVIEW_SIZE);
        g_memory = nullptr;
        return Napi::Boolean::New(env, false);
    }

    g_handle = reinterpret_cast<void*>(1);  // Mark as valid
    return Napi::Boolean::New(env, true);

#elif defined(WIN32_SHM)
    g_name = name;

    HANDLE hMapFile = OpenFileMappingA(FILE_MAP_READ, FALSE, g_name.c_str());
    if (hMapFile == nullptr) {
        return Napi::Boolean::New(env, false);
    }

    void* ptr = MapViewOfFile(hMapFile, FILE_MAP_READ, 0, 0, SHARED_PREVIEW_SIZE);
    if (ptr == nullptr) {
        CloseHandle(hMapFile);
        return Napi::Boolean::New(env, false);
    }

    g_memory = static_cast<SharedPreviewMemory*>(ptr);

    if (g_memory->header.magic != PREVIEW_MAGIC) {
        UnmapViewOfFile(g_memory);
        CloseHandle(hMapFile);
        g_memory = nullptr;
        return Napi::Boolean::New(env, false);
    }

    g_handle = hMapFile;
    return Napi::Boolean::New(env, true);

#else
    return Napi::Boolean::New(env, false);
#endif
}

Napi::Value Close(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (g_memory) {
#if defined(POSIX_SHM)
        munmap(g_memory, SHARED_PREVIEW_SIZE);
#elif defined(WIN32_SHM)
        UnmapViewOfFile(g_memory);
        if (g_handle) CloseHandle(static_cast<HANDLE>(g_handle));
#endif
        g_memory = nullptr;
        g_handle = nullptr;
        g_name.clear();
    }

    return env.Undefined();
}

Napi::Value IsOpen(const Napi::CallbackInfo& info) {
    return Napi::Boolean::New(info.Env(), g_memory != nullptr);
}

Napi::Value GetHeader(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!g_memory) {
        return env.Null();
    }

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("magic", Napi::Number::New(env, g_memory->header.magic));
    obj.Set("version", Napi::Number::New(env, g_memory->header.version));
    obj.Set("operatorCount", Napi::Number::New(env, g_memory->header.operatorCount));
    obj.Set("frameNumber", Napi::Number::New(env, g_memory->header.frameNumber));
    obj.Set("timestampUs", Napi::Number::New(env, static_cast<double>(g_memory->header.timestampUs)));

    return obj;
}

Napi::Value GetSlot(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!g_memory) {
        return env.Null();
    }

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number argument expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    int index = info[0].As<Napi::Number>().Int32Value();
    if (index < 0 || index >= PREVIEW_MAX_OPERATORS) {
        return env.Null();
    }

    const SharedPreviewSlot& slot = g_memory->slots[index];

    Napi::Object obj = Napi::Object::New(env);
    obj.Set("operatorId", Napi::String::New(env, slot.operatorId));
    obj.Set("sourceLine", Napi::Number::New(env, slot.sourceLine));
    obj.Set("frameNumber", Napi::Number::New(env, slot.frameNumber));
    obj.Set("width", Napi::Number::New(env, slot.width));
    obj.Set("height", Napi::Number::New(env, slot.height));
    obj.Set("kind", Napi::Number::New(env, slot.kind));
    obj.Set("ready", Napi::Boolean::New(env, slot.ready != 0));

    // Include pixel data for textures
    if (slot.kind == 0 && slot.ready) {  // Texture
        // Create a Buffer containing the RGB pixel data
        Napi::Buffer<uint8_t> pixelBuffer = Napi::Buffer<uint8_t>::Copy(
            env, slot.data.pixels, PREVIEW_THUMB_SIZE
        );
        obj.Set("pixels", pixelBuffer);
    } else if (slot.kind == 1 && slot.ready) {  // Value
        obj.Set("value", Napi::Number::New(env, slot.data.valueData.value));
    } else if (slot.kind == 2 && slot.ready) {  // ValueArray
        uint32_t count = slot.data.arrayData.count;
        Napi::Array arr = Napi::Array::New(env, count);
        for (uint32_t i = 0; i < count; i++) {
            arr.Set(i, Napi::Number::New(env, slot.data.arrayData.values[i]));
        }
        obj.Set("values", arr);
    }

    return obj;
}

Napi::Value GetSlotPixels(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (!g_memory) {
        return env.Null();
    }

    if (info.Length() < 1 || !info[0].IsNumber()) {
        Napi::TypeError::New(env, "Number argument expected").ThrowAsJavaScriptException();
        return env.Null();
    }

    int index = info[0].As<Napi::Number>().Int32Value();
    if (index < 0 || index >= PREVIEW_MAX_OPERATORS) {
        return env.Null();
    }

    const SharedPreviewSlot& slot = g_memory->slots[index];

    if (slot.kind != 0 || !slot.ready) {
        return env.Null();
    }

    // Return just the pixel buffer for efficiency
    return Napi::Buffer<uint8_t>::Copy(env, slot.data.pixels, PREVIEW_THUMB_SIZE);
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("open", Napi::Function::New(env, Open));
    exports.Set("close", Napi::Function::New(env, Close));
    exports.Set("isOpen", Napi::Function::New(env, IsOpen));
    exports.Set("getHeader", Napi::Function::New(env, GetHeader));
    exports.Set("getSlot", Napi::Function::New(env, GetSlot));
    exports.Set("getSlotPixels", Napi::Function::New(env, GetSlotPixels));

    // Export constants
    exports.Set("THUMB_WIDTH", Napi::Number::New(env, PREVIEW_THUMB_WIDTH));
    exports.Set("THUMB_HEIGHT", Napi::Number::New(env, PREVIEW_THUMB_HEIGHT));
    exports.Set("THUMB_CHANNELS", Napi::Number::New(env, PREVIEW_THUMB_CHANNELS));
    exports.Set("THUMB_SIZE", Napi::Number::New(env, PREVIEW_THUMB_SIZE));
    exports.Set("MAX_OPERATORS", Napi::Number::New(env, PREVIEW_MAX_OPERATORS));

    return exports;
}

NODE_API_MODULE(shared_preview, Init)
