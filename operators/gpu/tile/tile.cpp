#include "operator_api/wgsl_filter.h"

struct Tile : vivid::WgslFilterBase {
    static constexpr const char* kName = "Tile";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> repeat_x {"repeat_x", 2.0f, 0.1f, 32.0f};
    vivid::Param<float> repeat_y {"repeat_y", 2.0f, 0.1f, 32.0f};
    vivid::Param<float> offset_x {"offset_x", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> offset_y {"offset_y", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> mirror   {"mirror",   0.0f, 0.0f, 1.0f};

    Tile() : WgslFilterBase("tile.wgsl") {}

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        layout_row(repeat_x, 2, 0);
        layout_row(repeat_y, 2, 1);
        layout_row(offset_x, 2, 0);
        layout_row(offset_y, 2, 1);

        out.push_back(&repeat_x);
        out.push_back(&repeat_y);
        out.push_back(&offset_x);
        out.push_back(&offset_y);
        out.push_back(&mirror);
    }
};

VIVID_REGISTER(Tile)
