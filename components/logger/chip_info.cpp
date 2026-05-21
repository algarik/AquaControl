// AquaControl — Chip & firmware info implementation.
#include "chip_info.h"

#include <cstdio>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_idf_version.h"
#include "esp_mac.h"
#include "esp_system.h"

namespace aqua::chip {

Info read() {
    Info info{};

    esp_chip_info_t cinfo = {};
    esp_chip_info(&cinfo);
    switch (cinfo.model) {
        case CHIP_ESP32:    info.chip_model = "ESP32";    break;
        case CHIP_ESP32S2:  info.chip_model = "ESP32-S2"; break;
        case CHIP_ESP32S3:  info.chip_model = "ESP32-S3"; break;
        case CHIP_ESP32C3:  info.chip_model = "ESP32-C3"; break;
        case CHIP_ESP32C6:  info.chip_model = "ESP32-C6"; break;
        case CHIP_ESP32H2:  info.chip_model = "ESP32-H2"; break;
        default:            info.chip_model = "unknown";  break;
    }
    info.chip_revision = (uint8_t)cinfo.revision;
    info.cpu_cores     = cinfo.cores;

    uint32_t flash_sz = 0;
    esp_flash_get_size(nullptr, &flash_sz);
    info.flash_size_bytes = flash_sz;

    info.psram_size_bytes    = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    info.free_psram_bytes    = heap_caps_get_free_size (MALLOC_CAP_SPIRAM);
    info.free_heap_bytes     = esp_get_free_heap_size();
    info.min_free_heap_bytes = esp_get_minimum_free_heap_size();
    info.idf_version         = esp_get_idf_version();

    const esp_app_desc_t* desc = esp_app_get_description();
    if (desc) {
        info.app_name     = desc->project_name;
        info.app_version  = desc->version;
        info.compile_time = desc->time;
        info.compile_date = desc->date;
        char sha[17];
        for (int i = 0; i < 8; ++i) {
            snprintf(sha + i * 2, 3, "%02x", desc->app_elf_sha256[i]);
        }
        sha[16] = '\0';
        info.sha256_short = sha;
    }
    return info;
}

}  // namespace aqua::chip
