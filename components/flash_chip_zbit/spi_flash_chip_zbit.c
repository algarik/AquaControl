/*
 * SPDX-FileCopyrightText: 2025 AquaControl
 * SPDX-License-Identifier: Apache-2.0
 *
 * Driver for Zbit Semiconductor SPI flash chips (vendor ID 0x5E).
 *
 * Verified against ZB25VQ32 (0x5E4016) per project-local datasheet at
 * .claude/hw_docs/ZB25VQ32.pdf:
 *   - Suspend opcode .......... 0x75 (= CMD_SUSPEND)
 *   - Resume  opcode .......... 0x7A (= CMD_RESUME)
 *   - Read SR2 opcode ......... 0x35 (= CMD_RDSR2)
 *   - SUS bit position ........ SR2[7] (single bit, used for both program
 *                                       and erase suspend status)
 * These match the Winbond W25Q protocol, so the per-op behaviour is inherited
 * from the generic chip driver. Only probe, capability advertisement and the
 * suspend-command configuration are vendor-specific.
 *
 * Why this driver exists: the CrowPanel ESP32-S3 5.0" HMI ships with a Zbit
 * ZB25VQ32 flash. ESP-IDF has no built-in driver for vendor 0x5E, so the
 * chip falls back to the generic driver, which refuses to advertise
 * SPI_FLASH_CHIP_CAP_SUSPEND. Without suspend, NVS writes hold the cache
 * disabled long enough to crash the RGB-LCD bounce-buffer EOF ISR (which
 * memcpy()s from PSRAM frame buffer). With suspend, NVS writes are sliced
 * into ~20us chunks separated by cache-on windows, eliminating the race.
 */

#include "esp_log.h"
#include "spi_flash_chip_generic.h"
#include "spi_flash_chip_zbit.h"
#include "spi_flash/spi_flash_defs.h"
#include "sdkconfig.h"

static esp_err_t spi_flash_chip_zbit_probe(esp_flash_t *chip, uint32_t flash_id)
{
    const uint8_t MFG_ID = 0x5E; /* Zbit Semiconductor */
    if ((flash_id >> 16) != MFG_ID) {
        return ESP_ERR_NOT_FOUND;
    }
    return ESP_OK;
}

static spi_flash_caps_t spi_flash_chip_zbit_get_caps(esp_flash_t *chip)
{
    spi_flash_caps_t caps_flags = 0;

    /* 32 Mbit / 4 MB and below are 3-byte-address only; no 32MB cap. */
    if ((chip->chip_id & 0xFF) >= 0x19) {
        caps_flags |= SPI_FLASH_CHIP_CAP_32MB_SUPPORT;
    }

#if CONFIG_SPI_FLASH_AUTO_SUSPEND
    /* Whitelist Zbit parts that have been verified against their datasheet
     * for program/erase suspend. Add new IDs here only after confirming
     * 0x75/0x7A and SR2[7]=SUS in the relevant datasheet. */
    switch (chip->chip_id) {
    case 0x5E4016: /* ZB25VQ32 - 32 Mbit, verified */
        caps_flags |= SPI_FLASH_CHIP_CAP_SUSPEND;
        break;
    default:
        break;
    }
#endif

    return caps_flags;
}

static esp_err_t spi_flash_chip_zbit_suspend_cmd_conf(esp_flash_t *chip)
{
    /* ZB25VQ32 SUS status bit lives at SR2[7], read via CMD_RDSR2 (0x35).
     * Suspend / resume opcodes match the Winbond convention. */
    spi_flash_sus_cmd_conf sus_conf = {
        .sus_mask = 0x80,        /* SR2[7] = SUS */
        .cmd_rdsr = CMD_RDSR2,   /* 0x35 */
        .sus_cmd  = CMD_SUSPEND, /* 0x75 */
        .res_cmd  = CMD_RESUME,  /* 0x7A */
    };

    return chip->host->driver->sus_setup(chip->host, &sus_conf);
}

static const char chip_name[] = "zbit";

const spi_flash_chip_t esp_flash_chip_zbit = {
    .name = chip_name,
    .timeout = &spi_flash_chip_generic_timeout,
    .probe = spi_flash_chip_zbit_probe,
    .reset = spi_flash_chip_generic_reset,
    .detect_size = spi_flash_chip_generic_detect_size,
    .erase_chip = spi_flash_chip_generic_erase_chip,
    .erase_sector = spi_flash_chip_generic_erase_sector,
    .erase_block = spi_flash_chip_generic_erase_block,
    .sector_size = 4 * 1024,
    .block_erase_size = 64 * 1024,

    .get_chip_write_protect = spi_flash_chip_generic_get_write_protect,
    .set_chip_write_protect = spi_flash_chip_generic_set_write_protect,

    .num_protectable_regions = 0,
    .protectable_regions = NULL,
    .get_protected_regions = NULL,
    .set_protected_regions = NULL,

    .read = spi_flash_chip_generic_read,
    .write = spi_flash_chip_generic_write,
    .program_page = spi_flash_chip_generic_page_program,
    .page_size = 256,
    .write_encrypted = spi_flash_chip_generic_write_encrypted,

    .wait_idle = spi_flash_chip_generic_wait_idle,
    .set_io_mode = spi_flash_chip_generic_set_io_mode,
    .get_io_mode = spi_flash_chip_generic_get_io_mode,

    .read_reg = spi_flash_chip_generic_read_reg,
    .yield = spi_flash_chip_generic_yield,
    .sus_setup = spi_flash_chip_zbit_suspend_cmd_conf,
    .read_unique_id = spi_flash_chip_generic_read_unique_id_none,
    .get_chip_caps = spi_flash_chip_zbit_get_caps,
    .config_host_io_mode = spi_flash_chip_generic_config_host_io_mode,
};
