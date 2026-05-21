/*
 * SPDX-FileCopyrightText: 2025 AquaControl
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zbit Semiconductor SPI flash chip driver for ESP-IDF.
 *
 * Targets ZB25VQ32 (vendor 0x5E, device 0x4016) found on CrowPanel ESP32-S3
 * 5.0" HMI boards. ZB25VQ32 implements the Winbond W25Q-compatible command
 * set including program/erase suspend (0x75) / resume (0x7A) with the SUS
 * status bit at SR2 bit 7 (read via 0x35). See datasheet (project copy at
 * .claude/hw_docs/ZB25VQ32.pdf) for details.
 */
#pragma once

#include "spi_flash_chip_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

extern const spi_flash_chip_t esp_flash_chip_zbit;

#ifdef __cplusplus
}
#endif
