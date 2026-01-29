// File Browser Panel Implementation
// Project file tree navigation

#include <vivid/devtools/panels/file_browser_panel.h>
#include <vivid/context.h>
#include <vivid/gui/ui_style.h>
#include <filesystem>
#include <algorithm>
#include <iostream>

namespace fs = std::filesystem;

namespace vivid {

// File types to show in browser
static const std::set<std::string> ALLOWED_EXTENSIONS = {
    ".cpp", ".h", ".hpp", ".c",
    ".wgsl", ".glsl", ".hlsl",
    ".json", ".md", ".txt",
    ".cmake"
};

struct FileNode {
    std::string name;          // Just filename
    std::string path;          // Full path
    bool isDirectory = false;
    bool expanded = false;
    std::vector<std::unique_ptr<FileNode>> children;
    int depth = 0;

    // Sort: directories first, then alphabetically
    static bool compare(const std::unique_ptr<FileNode>& a, const std::unique_ptr<FileNode>& b) {
        if (a->isDirectory != b->isDirectory) {
            return a->isDirectory;  // Directories first
        }
        return a->name < b->name;
    }
};

struct FileBrowserPanel::Impl {
    std::string rootPath;
    std::unique_ptr<FileNode> rootNode;
    std::set<std::string> expandedFolders;
    std::string selectedPath;
    int hoveredIndex = -1;
    float scrollOffset = 0;

    FileSelectedCallback onFileSelected;

    // Flattened list for rendering
    struct FlatEntry {
        FileNode* node;
        int depth;
    };
    std::vector<FlatEntry> flatList;

    // Build file tree from directory
    std::unique_ptr<FileNode> buildTree(const fs::path& path, int depth = 0) {
        auto node = std::make_unique<FileNode>();
        node->name = path.filename().string();
        node->path = path.string();
        node->depth = depth;

        try {
            if (fs::is_directory(path)) {
                node->isDirectory = true;
                node->expanded = expandedFolders.count(node->path) > 0;

                for (const auto& entry : fs::directory_iterator(path)) {
                    // Skip hidden files/directories
                    std::string name = entry.path().filename().string();
                    if (name.empty() || name[0] == '.') continue;

                    // Skip build directories
                    if (name == "build" || name == "cmake-build-debug" || name == "cmake-build-release") continue;

                    if (entry.is_directory()) {
                        auto child = buildTree(entry.path(), depth + 1);
                        // Only add directories that have viewable files
                        if (child && !child->children.empty()) {
                            node->children.push_back(std::move(child));
                        }
                    } else if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if (ALLOWED_EXTENSIONS.count(ext) > 0) {
                            auto child = std::make_unique<FileNode>();
                            child->name = name;
                            child->path = entry.path().string();
                            child->depth = depth + 1;
                            child->isDirectory = false;
                            node->children.push_back(std::move(child));
                        }
                    }
                }

                // Sort children
                std::sort(node->children.begin(), node->children.end(), FileNode::compare);
            }
        } catch (const std::exception& e) {
            std::cerr << "[FileBrowserPanel] Error reading " << path << ": " << e.what() << std::endl;
        }

        return node;
    }

    // Flatten tree for rendering
    void flattenTree(FileNode* node) {
        if (!node) return;

        flatList.push_back({node, node->depth});

        if (node->isDirectory && node->expanded) {
            for (auto& child : node->children) {
                flattenTree(child.get());
            }
        }
    }

    void rebuildFlatList() {
        flatList.clear();
        if (rootNode) {
            flattenTree(rootNode.get());
        }
    }

    // Toggle expand/collapse
    void toggleExpand(FileNode* node) {
        if (!node || !node->isDirectory) return;
        node->expanded = !node->expanded;
        if (node->expanded) {
            expandedFolders.insert(node->path);
        } else {
            expandedFolders.erase(node->path);
        }
        rebuildFlatList();
    }

    UIStyle style;
    glm::vec4 lastBounds = {0, 0, 0, 0};
    float lastLineHeight = 0;
};

FileBrowserPanel::FileBrowserPanel() {
    m_config.id = "filebrowser";
    m_config.title = "Files";
    m_config.bounds = {20, 60, 250, 500};
    m_config.dockSide = DockSide::None;
    m_config.visible = false;
    m_config.resizable = true;
    m_config.draggable = true;
    m_config.minWidth = 180.0f;
    m_config.minHeight = 200.0f;
}

FileBrowserPanel::~FileBrowserPanel() = default;

bool FileBrowserPanel::init(Context& ctx, WGPUTextureFormat surfaceFormat) {
    m_impl = std::make_unique<Impl>();
    m_impl->style.scale = ctx.contentScale();
    return true;
}

void FileBrowserPanel::shutdown() {
    m_impl.reset();
}

void FileBrowserPanel::render(OverlayCanvas& canvas, const glm::vec4& bounds,
                               const gui::InputState& input, const UIStyle& style) {
    if (!m_config.visible || !m_impl) {
        m_inputRouting.consumedInput = false;
        m_focus.hovered = false;
        return;
    }

    m_impl->style = style;

    glm::vec4 renderBounds = beginRender(input, bounds);
    float x = renderBounds.x;
    float y = renderBounds.y;
    float w = renderBounds.z;
    float h = renderBounds.w;

    // Panel chrome (title bar controlled by m_display.showTitleBar)
    float titleBarHeight = m_display.showTitleBar ? style.titleBarHeight() : 0.0f;
    float contentY = y + titleBarHeight;
    float contentH = h - titleBarHeight;

    renderChrome(canvas, x, y, w, h, style, m_display.showTitleBar, &input);

    // Content area
    float padding = 4;
    float contentX = x + padding;
    float contentW = w - padding * 2;
    contentY += padding;
    contentH -= padding * 2;

    m_impl->lastBounds = {contentX, contentY, contentW, contentH};

    // Get font metrics
    int fontIndex = 0;
    float lineHeight = canvas.fontLineHeight(fontIndex);
    if (lineHeight <= 0) lineHeight = 18.0f;
    m_impl->lastLineHeight = lineHeight;

    // Background
    canvas.fillRect(contentX, contentY, contentW, contentH, style.panelBg);

    if (!m_impl->rootNode || m_impl->flatList.empty()) {
        canvas.text("No project", contentX + 8, contentY + lineHeight, style.textDim, fontIndex);
        return;
    }

    canvas.beginClipRect(contentX, contentY, contentW, contentH);

    // Calculate visible entries
    int visibleCount = static_cast<int>(contentH / lineHeight) + 1;
    int startIndex = static_cast<int>(m_impl->scrollOffset / lineHeight);
    int endIndex = std::min(startIndex + visibleCount, static_cast<int>(m_impl->flatList.size()));

    // Track hover
    m_impl->hoveredIndex = -1;

    for (int i = startIndex; i < endIndex; i++) {
        auto& entry = m_impl->flatList[i];
        FileNode* node = entry.node;
        float entryY = contentY + (i * lineHeight) - m_impl->scrollOffset;

        // Indentation
        float indent = node->depth * 16.0f;
        float textX = contentX + 8 + indent;

        // Check hover
        bool hovered = (input.mousePos.x >= contentX &&
                        input.mousePos.x <= contentX + contentW &&
                        input.mousePos.y >= entryY &&
                        input.mousePos.y < entryY + lineHeight);

        if (hovered) {
            m_impl->hoveredIndex = i;
        }

        // Selection/hover highlight
        bool selected = (node->path == m_impl->selectedPath);
        if (selected) {
            canvas.fillRect(contentX, entryY, contentW, lineHeight, style.accent * 0.3f);
        } else if (hovered) {
            canvas.fillRect(contentX, entryY, contentW, lineHeight, style.buttonHover);
        }

        // Directory icon/arrow
        if (node->isDirectory) {
            const char* arrow = node->expanded ? "v" : ">";
            canvas.text(arrow, textX - 12, entryY + lineHeight - 4, style.textDim, fontIndex);
        }

        // File/folder name
        glm::vec4 textColor = style.textPrimary;
        if (node->isDirectory) {
            textColor = style.accent;
        }
        canvas.text(node->name, textX, entryY + lineHeight - 4, textColor, fontIndex);
    }

    canvas.endClipRect();
}

bool FileBrowserPanel::handleInput(const gui::InputState& input) {
    if (!m_focus.focused || !m_impl) return false;

    // Scroll
    if (input.scroll.y != 0) {
        m_impl->scrollOffset -= input.scroll.y * 30;
        float maxScroll = std::max(0.0f,
            static_cast<float>(m_impl->flatList.size()) * m_impl->lastLineHeight - m_impl->lastBounds.w);
        m_impl->scrollOffset = std::max(0.0f, std::min(m_impl->scrollOffset, maxScroll));
        return true;
    }

    // Click handling
    if (input.mouseClicked[0] && m_impl->hoveredIndex >= 0) {
        auto& entry = m_impl->flatList[m_impl->hoveredIndex];
        FileNode* node = entry.node;

        if (node->isDirectory) {
            // Toggle expand/collapse
            m_impl->toggleExpand(node);
        } else {
            // Select file
            m_impl->selectedPath = node->path;
        }
        return true;
    }

    // Double-click to open file
    static float lastClickTime = 0;
    static int lastClickIndex = -1;
    if (input.mouseClicked[0] && m_impl->hoveredIndex >= 0) {
        float currentTime = static_cast<float>(input.time);
        if (m_impl->hoveredIndex == lastClickIndex && (currentTime - lastClickTime) < 0.4f) {
            // Double-click detected
            auto& entry = m_impl->flatList[m_impl->hoveredIndex];
            if (!entry.node->isDirectory && m_impl->onFileSelected) {
                m_impl->onFileSelected(entry.node->path);
            }
        }
        lastClickTime = currentTime;
        lastClickIndex = m_impl->hoveredIndex;
    }

    return false;
}

void FileBrowserPanel::onKeyDown(int key, int mods) {
    // Arrow key navigation could be added here
}

void FileBrowserPanel::setRootDirectory(const std::string& path) {
    if (!m_impl) return;

    m_impl->rootPath = path;
    refresh();
}

const std::string& FileBrowserPanel::rootDirectory() const {
    static std::string empty;
    return m_impl ? m_impl->rootPath : empty;
}

void FileBrowserPanel::refresh() {
    if (!m_impl || m_impl->rootPath.empty()) return;

    try {
        fs::path root(m_impl->rootPath);
        if (fs::exists(root) && fs::is_directory(root)) {
            m_impl->rootNode = m_impl->buildTree(root);
            // Auto-expand root
            if (m_impl->rootNode) {
                m_impl->rootNode->expanded = true;
                m_impl->expandedFolders.insert(m_impl->rootNode->path);
            }
            m_impl->rebuildFlatList();
        }
    } catch (const std::exception& e) {
        std::cerr << "[FileBrowserPanel] Failed to read directory: " << e.what() << std::endl;
    }
}

void FileBrowserPanel::onFileSelected(FileSelectedCallback callback) {
    if (m_impl) {
        m_impl->onFileSelected = std::move(callback);
    }
}

std::set<std::string> FileBrowserPanel::expandedFolders() const {
    return m_impl ? m_impl->expandedFolders : std::set<std::string>();
}

void FileBrowserPanel::setExpandedFolders(const std::set<std::string>& folders) {
    if (m_impl) {
        m_impl->expandedFolders = folders;
        if (m_impl->rootNode) {
            m_impl->rebuildFlatList();
        }
    }
}

void FileBrowserPanel::selectFile(const std::string& path) {
    if (m_impl) {
        m_impl->selectedPath = path;
    }
}

} // namespace vivid
