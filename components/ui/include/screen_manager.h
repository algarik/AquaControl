// AquaControl — Screen manager (Phase 4).
//
// Minimal stack-based screen navigation around LVGL's `lv_scr_load_anim`.
// Each "screen" is the root `lv_obj_t*` returned by a builder; the manager
// owns the lifetime: when a screen is replaced or popped, its root object
// is deleted.
//
// All public calls are safe to invoke from any task; they acquire the
// LVGL port lock internally.
#pragma once

#include "lvgl.h"

namespace aqua::ui::screen_manager {

enum class Transition : uint8_t {
    NONE = 0,
    FADE,
    SLIDE_LEFT,    // new screen slides in from the right
    SLIDE_RIGHT,   // new screen slides in from the left
};

// Push a new screen on top of the current one. Old screen is kept on the
// stack so pop() can restore it. `new_root` must be a brand-new LVGL screen
// object created via `lv_obj_create(nullptr)`.
//
// Default transition is NONE: on this 800x480 RGB panel with a single
// framebuffer the slide animations are visibly choppy (~1 frame of motion
// then a hard cut) because the panel scanout cannot keep up with full-frame
// invalidations at the 220 ms animation duration. Instant cuts feel
// noticeably more responsive.
void push(lv_obj_t* new_root, Transition tr = Transition::NONE);

// Pop the current screen and return to the previous one. Deletes the popped
// screen object. No-op if the stack is empty or has only one entry.
void pop(Transition tr = Transition::NONE);

// Replace the current screen entirely. Deletes the old root; the stack is
// reduced to just `new_root`. Used for boot → dashboard and dashboard →
// wizard transitions where back navigation is undesired.
void replace(lv_obj_t* new_root, Transition tr = Transition::NONE);

// Current top-of-stack screen (nullptr until first push/replace).
lv_obj_t* current();

// Number of screens on the stack (0 before first push/replace).
int depth();

// Pop all screens above the root (bottom of stack), navigating back to the
// dashboard. No-op if depth is already 0 or 1. Uses lv_async_call internally
// so it is safe to call from any task or event callback.
void pop_to_root(Transition tr = Transition::NONE);

}  // namespace aqua::ui::screen_manager
