#include <vivid/context.h>
#include "renderer.h"
#include "window.h"
#include "image_loader.h"
#include "video_loader.h"
#include "camera_capture.h"
#include "mesh.h"
#include "model_loader.h"
#include "pipeline3d.h"
#include "pipeline3d_instanced.h"
#include "pipeline3d_skinned.h"
#include "pipeline2d.h"
#include "ozz_animation.h"
#include <unordered_set>
#include <iostream>
#include <algorithm>
#include <filesystem>

namespace fs = std::filesystem;

namespace vivid {

Context::Context(Renderer& renderer, int width, int height)
    : renderer_(renderer), width_(width), height_(height) {}

Context::Context(Renderer& renderer, Window& window, int width, int height)
    : renderer_(renderer), window_(&window), width_(width), height_(height) {}

void Context::setVSync(bool enabled) {
    renderer_.setVSync(enabled);
}

bool Context::vsyncEnabled() const {
    return renderer_.vsyncEnabled();
}

void Context::setProjectPath(const std::string& projectPath) {
    projectPath_ = projectPath;
    // Normalize to remove trailing slashes
    while (!projectPath_.empty() && projectPath_.back() == '/') {
        projectPath_.pop_back();
    }
}

void Context::setSharedAssetsPath(const std::string& sharedPath) {
    sharedAssetsPath_ = sharedPath;
    while (!sharedAssetsPath_.empty() && sharedAssetsPath_.back() == '/') {
        sharedAssetsPath_.pop_back();
    }
}

std::string Context::resolvePath(const std::string& relativePath) const {
    // If already absolute, return as-is
    if (!relativePath.empty() && (relativePath[0] == '/' ||
        (relativePath.length() > 1 && relativePath[1] == ':'))) {
        return relativePath;
    }

    // Check project-relative path first
    if (!projectPath_.empty()) {
        std::string projectRelative = projectPath_ + "/" + relativePath;
        if (fs::exists(projectRelative)) {
            return projectRelative;
        }
    }

    // Fallback to shared assets
    if (!sharedAssetsPath_.empty()) {
        std::string sharedRelative = sharedAssetsPath_ + "/" + relativePath;
        if (fs::exists(sharedRelative)) {
            return sharedRelative;
        }
    }

    // Return original path (caller will handle missing file error)
    return relativePath;
}

Texture Context::createTexture(int width, int height) {
    return renderer_.createTexture(width, height);
}

Texture Context::createTexture() {
    return renderer_.createTexture(width_, height_);
}

Texture Context::createTextureMatching(const Texture& matchTexture) {
    if (matchTexture.valid()) {
        return renderer_.createTexture(matchTexture.width, matchTexture.height);
    }
    return renderer_.createTexture(width_, height_);
}

Texture Context::createTextureMatching(const std::string& nodeId, const std::string& output) {
    Texture* input = getInputTexture(nodeId, output);
    if (input && input->valid()) {
        return renderer_.createTexture(input->width, input->height);
    }
    return renderer_.createTexture(width_, height_);
}

Texture Context::loadImageAsTexture(const std::string& path) {
    std::string resolvedPath = resolvePath(path);
    ImageLoader loader;
    return loader.loadAsTexture(resolvedPath, renderer_);
}

void Context::uploadTexturePixels(Texture& texture, const uint8_t* pixels, int width, int height) {
    renderer_.uploadTexturePixels(texture, pixels, width, height);
}

bool Context::isImageSupported(const std::string& path) {
    return ImageLoader::isSupported(path);
}

// Video playback methods
VideoPlayer Context::createVideoPlayer(const std::string& path) {
    std::string resolvedPath = resolvePath(path);
    VideoPlayer player;
    auto loader = createVideoLoaderForPath(resolvedPath);  // Auto-detects HAP
    if (loader && loader->open(resolvedPath)) {
        player.handle = loader.release();  // Transfer ownership
    }
    return player;
}

void Context::destroyVideoPlayer(VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        loader->close();
        delete loader;
        player.handle = nullptr;
    }
}

VideoInfo Context::getVideoInfo(const VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->info();
    }
    return VideoInfo{};
}

bool Context::videoSeek(VideoPlayer& player, double timeSeconds) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->seek(timeSeconds);
    }
    return false;
}

bool Context::videoGetFrame(VideoPlayer& player, Texture& output) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->getFrame(output, renderer_);
    }
    return false;
}

double Context::videoGetTime(const VideoPlayer& player) {
    if (player.handle) {
        auto* loader = static_cast<VideoLoader*>(player.handle);
        return loader->currentTime();
    }
    return 0.0;
}

bool Context::isVideoSupported(const std::string& path) {
    return VideoLoader::isSupported(path);
}

// Camera capture methods
std::vector<CameraDevice> Context::enumerateCameras() {
    auto capture = CameraCapture::create();
    if (!capture) return {};

    auto devices = capture->enumerateDevices();
    std::vector<CameraDevice> result;
    result.reserve(devices.size());

    for (const auto& dev : devices) {
        CameraDevice d;
        d.deviceId = dev.deviceId;
        d.name = dev.name;
        d.isDefault = dev.isDefault;
        result.push_back(d);
    }
    return result;
}

Camera Context::createCamera(int width, int height, float frameRate) {
    Camera camera;
    auto capture = CameraCapture::create();
    if (capture) {
        CameraConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = frameRate;

        if (capture->open(config) && capture->startCapture()) {
            camera.handle = capture.release();
        }
    }
    return camera;
}

Camera Context::createCamera(const std::string& deviceId, int width, int height, float frameRate) {
    Camera camera;
    auto capture = CameraCapture::create();
    if (capture) {
        CameraConfig config;
        config.width = width;
        config.height = height;
        config.frameRate = frameRate;

        if (capture->open(deviceId, config) && capture->startCapture()) {
            camera.handle = capture.release();
        }
    }
    return camera;
}

void Context::destroyCamera(Camera& camera) {
    if (camera.handle) {
        auto* capture = static_cast<CameraCapture*>(camera.handle);
        capture->stopCapture();
        capture->close();
        delete capture;
        camera.handle = nullptr;
    }
}

CameraInfo Context::getCameraInfo(const Camera& camera) {
    if (camera.handle) {
        auto* capture = static_cast<CameraCapture*>(camera.handle);
        auto info = capture->info();
        CameraInfo result;
        result.width = info.width;
        result.height = info.height;
        result.frameRate = info.frameRate;
        result.deviceName = info.deviceName;
        result.isCapturing = info.isCapturing;
        return result;
    }
    return CameraInfo{};
}

bool Context::cameraGetFrame(Camera& camera, Texture& output) {
    if (camera.handle) {
        auto* capture = static_cast<CameraCapture*>(camera.handle);
        return capture->getFrame(output, renderer_);
    }
    return false;
}

bool Context::cameraStart(Camera& camera) {
    if (camera.handle) {
        auto* capture = static_cast<CameraCapture*>(camera.handle);
        return capture->startCapture();
    }
    return false;
}

void Context::cameraStop(Camera& camera) {
    if (camera.handle) {
        auto* capture = static_cast<CameraCapture*>(camera.handle);
        capture->stopCapture();
    }
}

void Context::runShader(const std::string& shaderPath, Texture& output) {
    runShader(shaderPath, nullptr, output);
}

void Context::runShader(const std::string& shaderPath, const Texture* input, Texture& output) {
    ShaderParams defaultParams;
    runShader(shaderPath, input, output, defaultParams);
}

void Context::runShader(const std::string& shaderPath, const Texture* input,
                        Texture& output, const ShaderParams& params) {
    // Get shader from cache (or load and cache it)
    Shader* shader = getCachedShader(shaderPath);
    if (!shader || !shader->valid()) {
        return;
    }

    // Set up uniforms with core values
    Uniforms uniforms;
    uniforms.time = time_;
    uniforms.deltaTime = dt_;
    uniforms.resolutionX = static_cast<float>(output.width);
    uniforms.resolutionY = static_cast<float>(output.height);
    uniforms.frame = frame_;
    uniforms.mode = params.mode;

    // Copy operator parameters
    uniforms.param0 = params.param0;
    uniforms.param1 = params.param1;
    uniforms.param2 = params.param2;
    uniforms.param3 = params.param3;
    uniforms.param4 = params.param4;
    uniforms.param5 = params.param5;
    uniforms.param6 = params.param6;
    uniforms.param7 = params.param7;
    uniforms.vec0X = params.vec0X;
    uniforms.vec0Y = params.vec0Y;
    uniforms.vec1X = params.vec1X;
    uniforms.vec1Y = params.vec1Y;

    renderer_.runShader(*shader, output, input, uniforms);
}

void Context::runShader(const std::string& shaderPath, const Texture* input1,
                        const Texture* input2, Texture& output, const ShaderParams& params) {
    // Get shader from cache (or load and cache it)
    Shader* shader = getCachedShader(shaderPath);
    if (!shader || !shader->valid()) {
        return;
    }

    // Set up uniforms with core values
    Uniforms uniforms;
    uniforms.time = time_;
    uniforms.deltaTime = dt_;
    uniforms.resolutionX = static_cast<float>(output.width);
    uniforms.resolutionY = static_cast<float>(output.height);
    uniforms.frame = frame_;
    uniforms.mode = params.mode;

    // Copy operator parameters
    uniforms.param0 = params.param0;
    uniforms.param1 = params.param1;
    uniforms.param2 = params.param2;
    uniforms.param3 = params.param3;
    uniforms.param4 = params.param4;
    uniforms.param5 = params.param5;
    uniforms.param6 = params.param6;
    uniforms.param7 = params.param7;
    uniforms.vec0X = params.vec0X;
    uniforms.vec0Y = params.vec0Y;
    uniforms.vec1X = params.vec1X;
    uniforms.vec1Y = params.vec1Y;

    renderer_.runShader(*shader, output, input1, input2, uniforms);
}

void Context::runShaderMulti(const std::string& shaderPath,
                             const std::vector<const Texture*>& inputs,
                             Texture& output, const ShaderParams& params) {
    // Get shader from cache (or load and cache it)
    Shader* shader = getCachedShader(shaderPath);
    if (!shader || !shader->valid()) {
        return;
    }

    // Set up uniforms with core values
    Uniforms uniforms;
    uniforms.time = time_;
    uniforms.deltaTime = dt_;
    uniforms.resolutionX = static_cast<float>(output.width);
    uniforms.resolutionY = static_cast<float>(output.height);
    uniforms.frame = frame_;
    uniforms.mode = params.mode;

    // Copy operator parameters
    uniforms.param0 = params.param0;
    uniforms.param1 = params.param1;
    uniforms.param2 = params.param2;
    uniforms.param3 = params.param3;
    uniforms.param4 = params.param4;
    uniforms.param5 = params.param5;
    uniforms.param6 = params.param6;
    uniforms.param7 = params.param7;
    uniforms.vec0X = params.vec0X;
    uniforms.vec0Y = params.vec0Y;
    uniforms.vec1X = params.vec1X;
    uniforms.vec1Y = params.vec1Y;

    renderer_.runShaderMulti(*shader, output, inputs, uniforms);
}

void Context::setOutput(const std::string& name, const Texture& tex) {
    // If currentNode_ is set (Chain API), prefix the output with node name
    if (!currentNode_.empty()) {
        textureOutputs_[currentNode_ + "." + name] = tex;
    } else {
        textureOutputs_[name] = tex;
    }
}

void Context::setOutput(const std::string& name, float value) {
    if (!currentNode_.empty()) {
        valueOutputs_[currentNode_ + "." + name] = value;
    } else {
        valueOutputs_[name] = value;
    }
}

void Context::setOutput(const std::string& name, const std::vector<float>& values) {
    if (!currentNode_.empty()) {
        valueArrayOutputs_[currentNode_ + "." + name] = values;
    } else {
        valueArrayOutputs_[name] = values;
    }
}

Texture* Context::getInputTexture(const std::string& nodeId, const std::string& output) {
    std::string key = nodeId + "." + output;
    auto it = textureOutputs_.find(key);
    if (it != textureOutputs_.end()) {
        return &it->second;
    }
    // Also check without the output suffix
    it = textureOutputs_.find(nodeId);
    if (it != textureOutputs_.end()) {
        return &it->second;
    }

    // Warn about missing input (only once per key)
    static std::unordered_set<std::string> warnedInputs;
    if (warnedInputs.find(key) == warnedInputs.end()) {
        std::cerr << "[Context] Warning: Input texture not found: " << key << "\n";
        warnedInputs.insert(key);
    }
    return nullptr;
}

float Context::getInputValue(const std::string& nodeId, const std::string& output, float defaultVal) {
    std::string key = nodeId + "." + output;
    auto it = valueOutputs_.find(key);
    if (it != valueOutputs_.end()) {
        return it->second;
    }
    it = valueOutputs_.find(nodeId);
    if (it != valueOutputs_.end()) {
        return it->second;
    }
    return defaultVal;
}

std::vector<float> Context::getInputValues(const std::string& nodeId, const std::string& output) {
    std::string key = nodeId + "." + output;
    auto it = valueArrayOutputs_.find(key);
    if (it != valueArrayOutputs_.end()) {
        return it->second;
    }
    it = valueArrayOutputs_.find(nodeId);
    if (it != valueArrayOutputs_.end()) {
        return it->second;
    }
    return {};
}

void Context::beginFrame(float time, float dt, int frame) {
    time_ = time;
    dt_ = dt;
    frame_ = frame;
}

void Context::endFrame() {
    // Nothing to do for now
}

void Context::clearOutputs() {
    textureOutputs_.clear();
    valueOutputs_.clear();
    valueArrayOutputs_.clear();
}

void Context::clearShaderCache() {
    for (auto& [path, shader] : shaderCache_) {
        if (shader) {
            renderer_.destroyShader(*shader);
        }
    }
    shaderCache_.clear();
}

bool Context::isKeyDown(int key) const {
    return window_ ? window_->isKeyDown(key) : false;
}

bool Context::wasKeyPressed(int key) const {
    return window_ ? window_->wasKeyPressed(key) : false;
}

bool Context::wasKeyReleased(int key) const {
    return window_ ? window_->wasKeyReleased(key) : false;
}

float Context::mouseX() const {
    return window_ ? window_->mouseX() : 0.0f;
}

float Context::mouseY() const {
    return window_ ? window_->mouseY() : 0.0f;
}

float Context::mouseNormX() const {
    return window_ ? window_->mouseNormX() : 0.0f;
}

float Context::mouseNormY() const {
    return window_ ? window_->mouseNormY() : 0.0f;
}

bool Context::isMouseDown(int button) const {
    return window_ ? window_->isMouseDown(button) : false;
}

bool Context::wasMousePressed(int button) const {
    return window_ ? window_->wasMousePressed(button) : false;
}

bool Context::wasMouseReleased(int button) const {
    return window_ ? window_->wasMouseReleased(button) : false;
}

float Context::scrollDeltaX() const {
    return window_ ? window_->scrollDeltaX() : 0.0f;
}

float Context::scrollDeltaY() const {
    return window_ ? window_->scrollDeltaY() : 0.0f;
}

Shader* Context::getCachedShader(const std::string& path) {
    // Use original path as cache key for consistency
    auto it = shaderCache_.find(path);
    if (it != shaderCache_.end()) {
        return it->second.get();
    }

    // Resolve path for actual file loading
    std::string resolvedPath = resolvePath(path);

    // Load and cache the shader
    auto shader = std::make_unique<Shader>(renderer_.loadShaderFromFile(resolvedPath));
    if (!shader->valid()) {
        return nullptr;
    }

    Shader* result = shader.get();
    shaderCache_[path] = std::move(shader);  // Cache with original key
    return result;
}

// 3D Rendering Implementation
class Renderer3DImpl {
public:
    Renderer3DImpl(Renderer& renderer) : renderer_(renderer) {
        renderer3d_.init(renderer);
        // Create the default pipeline with unlit normal shader
        pipeline_.create(renderer, shaders3d::UNLIT_NORMAL);
    }

    ~Renderer3DImpl() = default;

    Mesh* createMesh(const std::vector<Vertex3D>& vertices,
                     const std::vector<uint32_t>& indices) {
        auto mesh = std::make_unique<Mesh>();
        if (mesh->create(renderer_, vertices, indices)) {
            Mesh* result = mesh.get();
            meshes_.push_back(std::move(mesh));
            return result;
        }
        return nullptr;
    }

    void destroyMesh(Mesh* mesh) {
        auto it = std::find_if(meshes_.begin(), meshes_.end(),
            [mesh](const std::unique_ptr<Mesh>& m) { return m.get() == mesh; });
        if (it != meshes_.end()) {
            meshes_.erase(it);
        }
    }

    void render(const Mesh* mesh, const Camera3D& camera,
                const glm::mat4& transform, Texture& output,
                const glm::vec4& clearColor) {
        if (!mesh || !mesh->valid() || !pipeline_.valid()) return;

        float aspectRatio = static_cast<float>(output.width) / output.height;
        renderer3d_.setCamera(camera, aspectRatio);

        auto renderPass = renderer3d_.beginRenderPass(output, clearColor);
        if (!renderPass) return;

        // Set pipeline
        wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_.pipeline());

        // Create and set camera bind group
        auto cameraBindGroup = renderer3d_.createCameraBindGroup(pipeline_.cameraBindGroupLayout());
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraBindGroup, 0, nullptr);

        // Create and set transform bind group
        auto transformBindGroup = renderer3d_.createTransformBindGroup(
            pipeline_.transformBindGroupLayout(), transform);
        wgpuRenderPassEncoderSetBindGroup(renderPass, 1, transformBindGroup, 0, nullptr);

        // Draw the mesh
        mesh->draw(renderPass);

        renderer3d_.endRenderPass();

        // Clean up bind groups
        renderer3d_.releaseBindGroup(cameraBindGroup);
        renderer3d_.releaseBindGroup(transformBindGroup);
    }

    void renderMultiple(const std::vector<const Mesh*>& meshes,
                        const std::vector<glm::mat4>& transforms,
                        const Camera3D& camera, Texture& output,
                        const glm::vec4& clearColor) {
        if (meshes.empty() || !pipeline_.valid()) return;

        float aspectRatio = static_cast<float>(output.width) / output.height;
        renderer3d_.setCamera(camera, aspectRatio);

        auto renderPass = renderer3d_.beginRenderPass(output, clearColor);
        if (!renderPass) return;

        wgpuRenderPassEncoderSetPipeline(renderPass, pipeline_.pipeline());

        auto cameraBindGroup = renderer3d_.createCameraBindGroup(pipeline_.cameraBindGroupLayout());
        wgpuRenderPassEncoderSetBindGroup(renderPass, 0, cameraBindGroup, 0, nullptr);

        for (size_t i = 0; i < meshes.size(); ++i) {
            const Mesh* mesh = meshes[i];
            if (!mesh || !mesh->valid()) continue;

            glm::mat4 transform = (i < transforms.size()) ? transforms[i] : glm::mat4(1.0f);
            auto transformBindGroup = renderer3d_.createTransformBindGroup(
                pipeline_.transformBindGroupLayout(), transform);
            wgpuRenderPassEncoderSetBindGroup(renderPass, 1, transformBindGroup, 0, nullptr);

            mesh->draw(renderPass);

            renderer3d_.releaseBindGroup(transformBindGroup);
        }

        renderer3d_.endRenderPass();
        renderer3d_.releaseBindGroup(cameraBindGroup);
    }

private:
    Renderer& renderer_;
    Renderer3D renderer3d_;
    Pipeline3DInternal pipeline_;
    std::vector<std::unique_ptr<Mesh>> meshes_;
};

// 2D Instanced Rendering Implementation
class Renderer2DImpl {
public:
    Renderer2DImpl(Renderer& renderer) : renderer_(renderer) {
        pipeline_.init(renderer);
    }

    ~Renderer2DImpl() = default;

    void drawCircles(const std::vector<Circle2D>& circles, Texture& output,
                     const glm::vec4& clearColor) {
        // Convert Circle2D to CircleInstance
        std::vector<CircleInstance> instances;
        instances.reserve(circles.size());
        for (const auto& c : circles) {
            CircleInstance inst;
            inst.position = c.position;
            inst.radius = c.radius;
            inst._pad = 0.0f;
            inst.color = c.color;
            instances.push_back(inst);
        }
        pipeline_.drawCircles(instances, output, clearColor);
    }

private:
    Renderer& renderer_;
    Pipeline2DInternal pipeline_;
};

// 3D Instanced Rendering Implementation
class Renderer3DInstancedImpl {
public:
    Renderer3DInstancedImpl(Renderer& renderer) : renderer_(renderer) {
        instancedRenderer_.init(renderer);
    }

    ~Renderer3DInstancedImpl() = default;

    void drawInstanced(const Mesh& mesh, const std::vector<Instance3D>& instances,
                       const Camera3D& camera, Texture& output,
                       const glm::vec4& clearColor) {
        instancedRenderer_.drawInstanced(mesh, instances, camera, output, clearColor);
    }

private:
    Renderer& renderer_;
    Renderer3DInstanced instancedRenderer_;
};

// Skinned Mesh Renderer Implementation
class SkinnedMeshRendererImpl {
public:
    SkinnedMeshRendererImpl(Renderer& renderer) : renderer_(renderer) {
        skinnedRenderer_.init(renderer);
        renderer3d_.init(renderer);
    }

    ~SkinnedMeshRendererImpl() = default;

    SkinnedMeshGPU createMesh(const std::vector<SkinnedVertex3D>& vertices,
                               const std::vector<uint32_t>& indices) {
        return skinnedRenderer_.createMesh(vertices, indices);
    }

    void destroyMesh(SkinnedMeshGPU& mesh) {
        skinnedRenderer_.destroyMesh(mesh);
    }

    void render(SkinnedMesh3D& mesh, const Camera3D& camera,
                const glm::mat4& transform, Texture& output,
                const glm::vec4& clearColor) {
        if (!mesh.valid()) return;

        // Initialize pipeline lazily
        if (!pipeline_.valid()) {
            if (!pipeline_.create(renderer_)) {
                std::cerr << "[SkinnedMeshRenderer] Failed to create pipeline\n";
                return;
            }
        }

        // Create bone bind group if not exists
        if (!boneBindGroup_) {
            boneBindGroup_ = skinnedRenderer_.createBoneBindGroup(
                pipeline_.boneBindGroupLayout(), mesh.boneMatrices);
            boneBuffer_ = nullptr;  // Will be retrieved from first update
        }

        // Update bone matrices from animation
        if (!mesh.boneMatrices.empty()) {
            // Get the buffer from the bind group - we need to update it
            // For now we just recreate the bind group each frame (not optimal but works)
            if (boneBindGroup_) {
                skinnedRenderer_.releaseBindGroup(boneBindGroup_);
            }
            boneBindGroup_ = skinnedRenderer_.createBoneBindGroup(
                pipeline_.boneBindGroupLayout(), mesh.boneMatrices);
        }

        // Begin render pass
        auto pass = renderer3d_.beginRenderPass(output, clearColor);
        if (!pass) return;

        // Set camera
        float aspectRatio = static_cast<float>(output.width) / output.height;
        renderer3d_.setCamera(camera, aspectRatio);

        // Create camera and transform bind groups
        auto cameraBindGroup = renderer3d_.createCameraBindGroup(pipeline_.cameraBindGroupLayout());
        auto transformBindGroup = renderer3d_.createTransformBindGroup(
            pipeline_.transformBindGroupLayout(), transform);

        // Draw the skinned mesh
        wgpuRenderPassEncoderSetPipeline(pass, pipeline_.pipeline());
        wgpuRenderPassEncoderSetBindGroup(pass, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 1, transformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(pass, 2, boneBindGroup_, 0, nullptr);

        auto* gpuMesh = static_cast<SkinnedMeshGPU*>(mesh.handle);
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, gpuMesh->vertexBuffer, 0,
                                              gpuMesh->vertexCount * sizeof(SkinnedVertex3D));
        wgpuRenderPassEncoderSetIndexBuffer(pass, gpuMesh->indexBuffer,
                                             WGPUIndexFormat_Uint32, 0,
                                             gpuMesh->indexCount * sizeof(uint32_t));
        wgpuRenderPassEncoderDrawIndexed(pass, gpuMesh->indexCount, 1, 0, 0, 0);

        // End pass
        renderer3d_.endRenderPass();

        // Clean up bind groups
        renderer3d_.releaseBindGroup(cameraBindGroup);
        renderer3d_.releaseBindGroup(transformBindGroup);
    }

private:
    Renderer& renderer_;
    SkinnedMeshRenderer skinnedRenderer_;
    Renderer3D renderer3d_;
    Pipeline3DSkinnedInternal pipeline_;
    WGPUBindGroup boneBindGroup_ = nullptr;
    WGPUBuffer boneBuffer_ = nullptr;
};

// Destructor must be defined after Renderer3DImpl/Renderer2DImpl are complete
Context::~Context() = default;

Renderer3DImpl& Context::getRenderer3D() {
    if (!renderer3d_) {
        renderer3d_ = std::make_unique<Renderer3DImpl>(renderer_);
    }
    return *renderer3d_;
}

Mesh3D Context::createMesh(const std::vector<Vertex3D>& vertices,
                           const std::vector<uint32_t>& indices) {
    Mesh3D result;
    // Convert public Vertex3D to internal Vertex3D (they're the same layout)
    std::vector<vivid::Vertex3D> internalVertices(vertices.size());
    for (size_t i = 0; i < vertices.size(); ++i) {
        internalVertices[i].position = vertices[i].position;
        internalVertices[i].normal = vertices[i].normal;
        internalVertices[i].uv = vertices[i].uv;
        internalVertices[i].tangent = vertices[i].tangent;
    }

    Mesh* mesh = getRenderer3D().createMesh(internalVertices, indices);
    if (mesh) {
        result.handle = mesh;
        result.vertexCount = mesh->vertexCount();
        result.indexCount = mesh->indexCount();
        result.bounds.min = mesh->bounds().min;
        result.bounds.max = mesh->bounds().max;
    }
    return result;
}

Mesh3D Context::createCube() {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    primitives::generateCube(vertices, indices);
    return createMesh(vertices, indices);
}

Mesh3D Context::createSphere(float radius, int segments, int rings) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    primitives::generateSphere(vertices, indices, radius, segments, rings);
    return createMesh(vertices, indices);
}

Mesh3D Context::createPlane(float width, float height) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    primitives::generatePlane(vertices, indices, width, height);
    return createMesh(vertices, indices);
}

Mesh3D Context::createTorus(float majorRadius, float minorRadius) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    primitives::generateTorus(vertices, indices, majorRadius, minorRadius);
    return createMesh(vertices, indices);
}

Mesh3D Context::createCylinder(float radius, float height, int segments) {
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;
    primitives::generateCylinder(vertices, indices, radius, height, segments);
    return createMesh(vertices, indices);
}

Mesh3D Context::loadMesh(const std::string& path) {
    std::string resolvedPath = resolvePath(path);
    std::vector<Vertex3D> vertices;
    std::vector<uint32_t> indices;

    if (!loadModel(resolvedPath, vertices, indices)) {
        std::cerr << "[Context] Failed to load mesh: " << resolvedPath << "\n";
        return Mesh3D{};
    }

    return createMesh(vertices, indices);
}

bool Context::isMeshSupported(const std::string& path) {
    return isModelSupported(path);
}

void Context::destroyMesh(Mesh3D& mesh) {
    if (mesh.handle) {
        getRenderer3D().destroyMesh(static_cast<Mesh*>(mesh.handle));
        mesh.handle = nullptr;
        mesh.vertexCount = 0;
        mesh.indexCount = 0;
    }
}

void Context::render3D(const Mesh3D& mesh, const Camera3D& camera,
                       const glm::mat4& transform, Texture& output,
                       const glm::vec4& clearColor) {
    if (!mesh.valid()) return;
    getRenderer3D().render(static_cast<const Mesh*>(mesh.handle),
                           camera, transform, output, clearColor);
}

void Context::render3D(const std::vector<Mesh3D>& meshes,
                       const std::vector<glm::mat4>& transforms,
                       const Camera3D& camera, Texture& output,
                       const glm::vec4& clearColor) {
    std::vector<const Mesh*> internalMeshes;
    for (const auto& m : meshes) {
        if (m.valid()) {
            internalMeshes.push_back(static_cast<const Mesh*>(m.handle));
        }
    }
    if (!internalMeshes.empty()) {
        getRenderer3D().renderMultiple(internalMeshes, transforms, camera, output, clearColor);
    }
}

// 2D Instanced Rendering
Renderer2DImpl& Context::getRenderer2D() {
    if (!renderer2d_) {
        renderer2d_ = std::make_unique<Renderer2DImpl>(renderer_);
    }
    return *renderer2d_;
}

void Context::drawCircles(const std::vector<Circle2D>& circles, Texture& output,
                          const glm::vec4& clearColor) {
    getRenderer2D().drawCircles(circles, output, clearColor);
}

// 3D Instanced Rendering
Renderer3DInstancedImpl& Context::getRenderer3DInstanced() {
    if (!renderer3dInstanced_) {
        renderer3dInstanced_ = std::make_unique<Renderer3DInstancedImpl>(renderer_);
    }
    return *renderer3dInstanced_;
}

void Context::drawMeshInstanced(const Mesh3D& mesh,
                                 const std::vector<Instance3D>& instances,
                                 const Camera3D& camera, Texture& output,
                                 const glm::vec4& clearColor) {
    if (!mesh.valid() || instances.empty()) return;
    getRenderer3DInstanced().drawInstanced(
        *static_cast<const Mesh*>(mesh.handle),
        instances, camera, output, clearColor);
}

// Skinned Mesh Methods

SkinnedMeshRendererImpl& Context::getSkinnedMeshRenderer() {
    if (!skinnedMeshRenderer_) {
        skinnedMeshRenderer_ = std::make_unique<SkinnedMeshRendererImpl>(renderer_);
    }
    return *skinnedMeshRenderer_;
}

SkinnedMesh3D Context::loadSkinnedMesh(const std::string& path) {
    std::string resolvedPath = resolvePath(path);
    SkinnedMesh3D result;

    std::vector<SkinnedVertex3D> vertices;
    std::vector<uint32_t> indices;

    if (!loadSkinnedModel(resolvedPath, vertices, indices, result.skeleton, result.animations)) {
        std::cerr << "[Context] Failed to load skinned mesh: " << resolvedPath << "\n";
        return result;
    }

    // Create GPU mesh
    auto gpuMesh = new SkinnedMeshGPU(getSkinnedMeshRenderer().createMesh(vertices, indices));
    if (gpuMesh->valid()) {
        result.handle = gpuMesh;
        result.vertexCount = gpuMesh->vertexCount;
        result.indexCount = gpuMesh->indexCount;

        // Link animations to skeleton
        for (auto& clip : result.animations) {
            clip.linkToSkeleton(result.skeleton);
        }

        // Initialize bone matrices to identity
        result.boneMatrices.resize(result.skeleton.bones.size(), glm::mat4(1.0f));

        // Create ozz animation system
        auto* ozzSystem = new OzzAnimationSystem();
        if (ozzSystem->buildSkeleton(result.skeleton)) {
            // Build all animations (pass original index for index mapping)
            for (size_t i = 0; i < result.animations.size(); ++i) {
                ozzSystem->buildAnimation(result.animations[i], result.skeleton, static_cast<int>(i));
            }
            result.ozzSystem = ozzSystem;
            std::cout << "[Context] Using ozz-animation for " << result.skeleton.bones.size()
                      << " bones, " << result.animations.size() << " animations\n";
        } else {
            std::cerr << "[Context] Failed to build ozz skeleton, using fallback animation\n";
            delete ozzSystem;
        }

        // Auto-play a good animation if available (skip very short ones)
        if (!result.animations.empty()) {
            int bestAnim = 0;
            for (size_t i = 0; i < result.animations.size(); ++i) {
                if (result.animations[i].duration > 1.0f) {
                    bestAnim = static_cast<int>(i);
                    break;
                }
            }
            result.playAnimation(bestAnim, true);
            std::cout << "[Context] Auto-playing animation " << bestAnim << ": "
                      << result.animations[bestAnim].name << "\n";
        }
    } else {
        delete gpuMesh;
    }

    return result;
}

void Context::destroySkinnedMesh(SkinnedMesh3D& mesh) {
    if (mesh.ozzSystem) {
        delete mesh.ozzSystem;
        mesh.ozzSystem = nullptr;
    }
    if (mesh.handle) {
        auto* gpuMesh = static_cast<SkinnedMeshGPU*>(mesh.handle);
        getSkinnedMeshRenderer().destroyMesh(*gpuMesh);
        delete gpuMesh;
        mesh.handle = nullptr;
        mesh.vertexCount = 0;
        mesh.indexCount = 0;
    }
}

void Context::renderSkinned3D(SkinnedMesh3D& mesh, const Camera3D& camera,
                               const glm::mat4& transform, Texture& output,
                               const glm::vec4& clearColor) {
    if (!mesh.valid()) return;

    // If using ozz, sample the animation to compute bone matrices
    if (mesh.ozzSystem && mesh.currentAnimIndex >= 0) {
        mesh.ozzSystem->sampleByOriginalIndex(
            mesh.currentAnimIndex,
            mesh.currentTime,
            mesh.boneMatrices
        );
    }

    getSkinnedMeshRenderer().render(mesh, camera, transform, output, clearColor);
}

} // namespace vivid
