#pragma once

#include <cstddef>

// ADR-0018 / Phase-4 audit P1-01: close EVERY open plugin-editor window, unconditionally.
//
// A floated VST3/CLAP editor window holds a raw handle into the live plugin instance. When a
// Full-tier undo/redo rebuilds the audio topology it frees those instances, so any editor window
// still open would dangle — a use-after-free the next frame reads. The manual track-removal path
// already closes editors *before* teardown for exactly this reason; the undo restore path did not.
//
// `close_editor_pool` nulls every slot in a fixed window pool, invoking `closer` on each non-null
// one first. Unlike the per-frame reap (which only closes windows that already report themselves
// closed), this closes them ALL — that unconditional-close-and-null is the property that prevents
// the dangle. It is a pure template (no Window/GUI/plugin dependency), so the semantics are
// unit-testable with a fake pointer type + a recording closer.
namespace vivid {

template <class T, std::size_t N, class Closer>
void close_editor_pool(T* (&pool)[N], Closer&& closer) {
    for (std::size_t k = 0; k < N; ++k)
        if (pool[k]) {
            closer(pool[k]);
            pool[k] = nullptr;
        }
}

}  // namespace vivid
