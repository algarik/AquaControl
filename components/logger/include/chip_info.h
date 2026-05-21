// AquaControl — Chip & firmware info accessor (Phase 3.5 Block E16).
#pragma once

#include <cstdint>
#include <string>

namespace aqua::chip {

struct Info {
    std::string chip_model;       // "ESP32-S3"
    uint8_t     chip_revision;    // major.minor packed
    uint8_t     cpu_cores;
    uint32_t    flash_size_bytes;
    uint32_t    psram_size_bytes;
    uint32_t    free_heap_bytes;
    uint32_t    free_psram_bytes;
    uint32_t    min_free_heap_bytes;
    std::string idf_version;      // e.g. "v5.5.4"
    std::string app_name;
    std::string app_version;
    std::string compile_time;
    std::string compile_date;
    std::string sha256_short;     // first 16 hex chars of elf sha256
};

Info read();

}  // namespace aqua::chip
