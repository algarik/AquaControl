// AquaControl - Screen manager implementation.
//
// IMPORTANT lvgl pattern note: pop()/replace() defer the actual screen swap
// (and deletion of the outgoing screen) via lv_async_call. Without this,
// calling pop() from inside an event callback would synchronously destroy
// the button that fired the click event, leaving LVGL to walk a freed
// widget tree on its way out of lv_event_send -> reboot without a clean
// panic because the corruption happens deep inside the event dispatch.
#include "screen_manager.h"

#include <atomic>
#include <cassert>
#include <vector>

#include "ac_logger.h"
#include "esp_lvgl_port.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace aqua::ui::screen_manager {

static const char* TAG = "ScreenMgr";

// s_depth mirrors stack().size() but is stored as an atomic so that
// dim_manager (Core 0) can read depth() without acquiring the LVGL lock.
// The vector itself is only accessed from the LVGL task (Core 1).
static std::atomic<int> s_depth{0};

namespace {

std::vector<lv_obj_t*>& stack() {
    static std::vector<lv_obj_t*> s;
    return s;
}

lv_screen_load_anim_t to_lv_anim(Transition tr) {
    switch (tr) {
        case Transition::FADE:        return LV_SCR_LOAD_ANIM_FADE_IN;
        case Transition::SLIDE_LEFT:  return LV_SCR_LOAD_ANIM_MOVE_LEFT;
        case Transition::SLIDE_RIGHT: return LV_SCR_LOAD_ANIM_MOVE_RIGHT;
        case Transition::NONE:
        default:                      return LV_SCR_LOAD_ANIM_NONE;
    }
}

constexpr uint32_t kAnimMs  = 220;
constexpr uint32_t kAnimDly = 0;

struct PopReq     { lv_screen_load_anim_t anim; };
struct PushReq    { lv_obj_t* new_root; lv_screen_load_anim_t anim; };
struct ReplaceReq { lv_obj_t* new_root; lv_screen_load_anim_t anim; };
struct PopToRootReq { lv_screen_load_anim_t anim; };

static void pop_async_cb(void* arg) {
    auto* req = static_cast<PopReq*>(arg);
    lv_screen_load_anim_t anim = req->anim;
    delete req;

    auto& s = stack();
    if (s.size() < 2) return;
    s.pop_back();  // outgoing screen freed by auto_del below
    s_depth.store((int)s.size(), std::memory_order_release);
    lv_obj_t* below = s.back();
    lv_screen_load_anim(below, anim, kAnimMs, kAnimDly,
                        /*auto_del=*/true);
}

static void push_async_cb(void* arg) {
    auto* req = static_cast<PushReq*>(arg);
    lv_obj_t* new_root = req->new_root;
    lv_screen_load_anim_t anim = req->anim;
    delete req;
    if (!new_root) return;

    stack().push_back(new_root);
    s_depth.store((int)stack().size(), std::memory_order_release);
    // auto_del=false: the outgoing screen stays alive on the stack so it
    // can safely keep dispatching the click event that triggered the push.
    lv_screen_load_anim(new_root, anim, kAnimMs, kAnimDly, /*auto_del=*/false);
}

static void replace_async_cb(void* arg) {
    auto* req = static_cast<ReplaceReq*>(arg);
    lv_obj_t* new_root = req->new_root;
    lv_screen_load_anim_t anim = req->anim;
    delete req;
    if (!new_root) return;

    auto& s = stack();
    lv_obj_t* old_top = s.empty() ? nullptr : s.back();
    lv_obj_t* active_before = lv_screen_active();
    s.clear();
    s.push_back(new_root);
    s_depth.store((int)s.size(), std::memory_order_release);
    lv_screen_load_anim(new_root, anim, kAnimMs, kAnimDly,
                        /*auto_del=*/true);
    if (old_top != nullptr &&
        old_top != new_root &&
        old_top != active_before) {
        lv_obj_del_async(old_top);
    }
}

static void pop_to_root_async_cb(void* arg) {
    auto* req = static_cast<PopToRootReq*>(arg);
    lv_screen_load_anim_t anim = req->anim;
    delete req;

    auto& s = stack();
    if (s.size() < 2) return;
    lv_obj_t* root = s.front();
    lv_obj_t* act  = lv_screen_active();
    // Delete intermediate screens that are NOT currently displayed.
    // The active screen (act) must NOT be passed to lv_obj_del_async here.
    // Race: if del_async fires on act_scr before lv_display_refr_timer
    // processes the pending scr_to_load, LVGL sets disp->act_scr = NULL
    // and the next refresh calls lv_obj_update_layout(NULL), spinning the
    // LVGL task on CPU1 until the task watchdog fires.
    // auto_del=true lets LVGL discard act safely after the slide animation.
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i] != act) lv_obj_del_async(s[i]);
    }
    s.erase(s.begin() + 1, s.end());
    s_depth.store((int)s.size(), std::memory_order_release);
    lv_screen_load_anim(root, anim, kAnimMs, kAnimDly, /*auto_del=*/true);
}

}  // namespace

void push(lv_obj_t* new_root, Transition tr) {
    if (new_root == nullptr) return;
    // Deferred: runs from the LVGL task between event dispatches, so push()
    // is safe from any core/task — same pattern as replace(), pop().
    auto* req = new PushReq{new_root, to_lv_anim(tr)};
    if (lv_async_call(push_async_cb, req) != LV_RESULT_OK) {
        AC_LOGE(TAG, "push: lv_async_call failed");
        delete req;
    }
}

void pop(Transition tr) {
    // Deferred: the actual screen swap (and deletion of the outgoing screen
    // via auto_del=true) runs from the LVGL task between event dispatches,
    // so click callbacks may safely call pop().
    auto* req = new PopReq{to_lv_anim(tr)};
    if (lv_async_call(pop_async_cb, req) != LV_RESULT_OK) {
        AC_LOGE(TAG, "pop: lv_async_call failed");
        delete req;
    }
}

void replace(lv_obj_t* new_root, Transition tr) {
    if (new_root == nullptr) return;
    auto* req = new ReplaceReq{new_root, to_lv_anim(tr)};
    if (lv_async_call(replace_async_cb, req) != LV_RESULT_OK) {
        AC_LOGE(TAG, "replace: lv_async_call failed");
        delete req;
    }
}

lv_obj_t* current() {
    auto& s = stack();
    return s.empty() ? nullptr : s.back();
}

int depth() {
    // Read the atomic mirror — safe from any core without the LVGL lock.
    return s_depth.load(std::memory_order_acquire);
}

void pop_to_root(Transition tr) {
    auto* req = new PopToRootReq{to_lv_anim(tr)};
    if (lv_async_call(pop_to_root_async_cb, req) != LV_RESULT_OK) {
        AC_LOGE(TAG, "pop_to_root: lv_async_call failed");
        delete req;
    }
}

}  // namespace aqua::ui::screen_manager
