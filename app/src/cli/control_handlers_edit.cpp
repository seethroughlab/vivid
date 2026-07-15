#include "cli/control_handlers_internal.h"

#include "app/app.h"
#include "app/edit_gateway.h"   // ADR-0017 undo/redo

#include <string>

namespace vivid {

// ADR-0017 — undo/redo over the edit gateway. Both return the post-op history state so a client
// (or the MCP bridge) can drive/label buttons: {ok, did, can_undo, can_redo, undo_label, redo_label}.
void register_edit_handlers(Handlers& handlers_) {
    auto history = [](EditGateway& g, bool did) {
        json r = ok();
        r["did"] = did;
        r["can_undo"] = g.can_undo();
        r["can_redo"] = g.can_redo();
        r["undo_label"] = g.undo_label();
        r["redo_label"] = g.redo_label();
        return r;
    };
    handlers_["undo"] = [history](const ControlCtx& c, const json&) {
        if (!c.app || !c.app->edit_gateway) return err(code::kInternal, "no edit gateway");
        return history(*c.app->edit_gateway, c.app->edit_gateway->undo());
    };
    handlers_["redo"] = [history](const ControlCtx& c, const json&) {
        if (!c.app || !c.app->edit_gateway) return err(code::kInternal, "no edit gateway");
        return history(*c.app->edit_gateway, c.app->edit_gateway->redo());
    };
}

}  // namespace vivid
