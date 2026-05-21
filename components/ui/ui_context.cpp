// AquaControl — UI dependency context implementation.
#include "ui_context.h"

namespace aqua::ui {

static UiContext s_ctx;

void set_ui_context(const UiContext& ctx) { s_ctx = ctx; }
const UiContext& ui_context()             { return s_ctx; }

}  // namespace aqua::ui
