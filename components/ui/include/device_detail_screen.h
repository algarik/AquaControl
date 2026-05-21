// AquaControl - Device detail screen (status + manual override + delete).
#pragma once

#include <cstdint>
#include "lvgl.h"

namespace aqua::ui::device_detail_screen {

lv_obj_t* build(uint8_t device_id);

}  // namespace aqua::ui::device_detail_screen
