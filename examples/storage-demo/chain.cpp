// Storage Demo - Mouse Recording and Playback
// Demonstrates persistent storage using the vivid-storage addon
//
// Controls:
//   Hold Left Mouse: Record mouse path (red dot follows cursor)
//   Press R: Start replay of recorded path
//   Press C: Clear recorded path
//   Press S: Save to file (auto-saves on exit too)
//
// The recorded path persists across app restarts!

#include <vivid/vivid.h>
#include <vivid/storage/storage.h>
#include <cmath>
#include <vector>
#include <sstream>

using namespace vivid;

// Storage for persistent data
static storage::Storage* store = nullptr;

// Recording state
static std::vector<glm::vec2> recordedPath;
static bool isRecording = false;
static bool isReplaying = false;
static float replayTime = 0.0f;
static int replayIndex = 0;
static float recordInterval = 0.016f;  // ~60 samples/sec
static float timeSinceLastSample = 0.0f;

// Visuals
static Texture output;

void setup(Chain& chain) {
    chain.setOutput("out");
}

// Convert path to/from storage format
void savePath() {
    if (!store) return;

    // Store path as comma-separated values: x1,y1,x2,y2,...
    std::ostringstream oss;
    for (size_t i = 0; i < recordedPath.size(); ++i) {
        if (i > 0) oss << ",";
        oss << recordedPath[i].x << "," << recordedPath[i].y;
    }
    store->setString("path", oss.str());
    store->setInt("pathLength", static_cast<int>(recordedPath.size()));
    store->save();

    std::cout << "[storage-demo] Saved " << recordedPath.size() << " points\n";
}

void loadPath() {
    if (!store) return;

    recordedPath.clear();

    std::string pathStr = store->getString("path", "");
    if (pathStr.empty()) {
        std::cout << "[storage-demo] No saved path found\n";
        return;
    }

    // Parse comma-separated values
    std::istringstream iss(pathStr);
    std::string token;
    std::vector<float> values;

    while (std::getline(iss, token, ',')) {
        try {
            values.push_back(std::stof(token));
        } catch (...) {}
    }

    // Convert pairs to vec2
    for (size_t i = 0; i + 1 < values.size(); i += 2) {
        recordedPath.push_back(glm::vec2(values[i], values[i + 1]));
    }

    std::cout << "[storage-demo] Loaded " << recordedPath.size() << " points from previous session\n";
}

void update(Chain& chain, Context& ctx) {
    // Initialize on first frame
    if (!output.valid()) {
        output = ctx.createTexture();

        // Create storage (loads existing data from file)
        store = new storage::Storage("mouse_recording.json");
        loadPath();

        // If we have a path, start replaying immediately
        if (!recordedPath.empty()) {
            isReplaying = true;
            replayIndex = 0;
            replayTime = 0.0f;
            std::cout << "[storage-demo] Auto-replaying saved path...\n";
        }
    }

    float dt = ctx.dt();

    // Handle input
    if (ctx.wasKeyPressed(Key::R) && !recordedPath.empty()) {
        isReplaying = true;
        replayIndex = 0;
        replayTime = 0.0f;
        std::cout << "[storage-demo] Replaying " << recordedPath.size() << " points\n";
    }

    if (ctx.wasKeyPressed(Key::C)) {
        recordedPath.clear();
        isReplaying = false;
        savePath();
        std::cout << "[storage-demo] Cleared path\n";
    }

    if (ctx.wasKeyPressed(Key::S)) {
        savePath();
    }

    // Recording: hold left mouse button
    bool mouseDown = ctx.isMouseDown(0);

    if (mouseDown && !isReplaying) {
        if (!isRecording) {
            // Start new recording
            recordedPath.clear();
            isRecording = true;
            timeSinceLastSample = recordInterval;  // Record first point immediately
            std::cout << "[storage-demo] Recording started...\n";
        }

        // Sample at regular intervals
        timeSinceLastSample += dt;
        if (timeSinceLastSample >= recordInterval) {
            timeSinceLastSample = 0.0f;
            recordedPath.push_back(glm::vec2(ctx.mouseNormX(), ctx.mouseNormY()));
        }
    } else if (isRecording) {
        // Stopped recording
        isRecording = false;
        savePath();
        std::cout << "[storage-demo] Recording stopped: " << recordedPath.size() << " points\n";
    }

    // Update replay
    if (isReplaying && !recordedPath.empty()) {
        replayTime += dt;

        // Advance through path based on time
        int targetIndex = static_cast<int>(replayTime / recordInterval);
        if (targetIndex >= static_cast<int>(recordedPath.size())) {
            // Loop replay
            replayTime = 0.0f;
            replayIndex = 0;
        } else {
            replayIndex = targetIndex;
        }
    }

    // Stop replay when recording starts
    if (isRecording) {
        isReplaying = false;
    }

    // Draw
    ctx.clear(output, glm::vec4(0.1f, 0.1f, 0.15f, 1.0f));

    // Draw recorded path as a trail
    if (recordedPath.size() > 1) {
        for (size_t i = 1; i < recordedPath.size(); ++i) {
            float alpha = 0.3f;
            // Highlight the portion already played
            if (isReplaying && static_cast<int>(i) <= replayIndex) {
                alpha = 0.8f;
            }

            glm::vec2 p1 = recordedPath[i - 1];
            glm::vec2 p2 = recordedPath[i];

            // Draw line segment as a thin rectangle
            ctx.drawLine(output, p1, p2, glm::vec4(0.4f, 0.4f, 0.6f, alpha), 2.0f);
        }
    }

    // Draw current position indicator
    glm::vec2 currentPos;
    glm::vec4 dotColor;

    if (isRecording) {
        // Recording: show red dot at mouse
        currentPos = glm::vec2(ctx.mouseNormX(), ctx.mouseNormY());
        dotColor = glm::vec4(1.0f, 0.2f, 0.2f, 1.0f);
    } else if (isReplaying && !recordedPath.empty()) {
        // Replaying: show green dot at replay position
        currentPos = recordedPath[replayIndex];
        dotColor = glm::vec4(0.2f, 1.0f, 0.4f, 1.0f);
    } else {
        // Idle: show dim dot at mouse
        currentPos = glm::vec2(ctx.mouseNormX(), ctx.mouseNormY());
        dotColor = glm::vec4(0.5f, 0.5f, 0.5f, 0.5f);
    }

    ctx.drawCircle(output, currentPos, 0.015f, dotColor);

    // Draw small dots at path start/end
    if (!recordedPath.empty()) {
        ctx.drawCircle(output, recordedPath.front(), 0.008f, glm::vec4(0.2f, 0.8f, 0.2f, 0.8f));
        ctx.drawCircle(output, recordedPath.back(), 0.008f, glm::vec4(0.8f, 0.2f, 0.2f, 0.8f));
    }

    ctx.setOutput("out", output);
}

VIVID_CHAIN(setup, update)
