#pragma once

#include "operator_api/types.h"

namespace vivid::ui {

inline void introspect_emit(const VividEditorContext& ctx,
                            const VividIntrospectWidget& w) {
    if (ctx.introspect_fn) ctx.introspect_fn(ctx.introspect_sink, &w);
}

} // namespace vivid::ui
