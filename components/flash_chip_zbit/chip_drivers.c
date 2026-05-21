/*
 * SPDX-FileCopyrightText: 2025 AquaControl
 * SPDX-License-Identifier: Apache-2.0
 *
 * Custom default_registered_chips[] for AquaControl.
 *
 * Required when CONFIG_SPI_FLASH_OVERRIDE_CHIP_DRIVER_LIST=y. The first
 * matching driver wins, so vendor-specific entries must come before the
 * catch-all generic driver. The Zbit entry is placed first because the
 * target board (CrowPanel ESP32-S3 5.0" HMI) ships with ZB25VQ32 and we
 * want SPI_FLASH_CHIP_CAP_SUSPEND advertised to enable AUTO_SUSPEND.
 *
 * The remaining vendor drivers are kept so the codebase still functions
 * if the same firmware is flashed to a board with a different flash chip.
 */

#include "spi_flash_chip_driver.h"
#include "spi_flash_chip_generic.h"
#include "spi_flash_chip_zbit.h"

#ifdef CONFIG_SPI_FLASH_SUPPORT_ISSI_CHIP
#include "spi_flash_chip_issi.h"
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_GD_CHIP
#include "spi_flash_chip_gd.h"
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_MXIC_CHIP
#include "spi_flash_chip_mxic.h"
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_WINBOND_CHIP
#include "spi_flash_chip_winbond.h"
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP
#include "spi_flash_chip_boya.h"
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_TH_CHIP
#include "spi_flash_chip_th.h"
#endif

const spi_flash_chip_t *default_registered_chips[] = {
    /* Project target chip first */
    &esp_flash_chip_zbit,

#ifdef CONFIG_SPI_FLASH_SUPPORT_ISSI_CHIP
    &esp_flash_chip_issi,
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_GD_CHIP
    &esp_flash_chip_gd,
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_MXIC_CHIP
    &esp_flash_chip_mxic,
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_WINBOND_CHIP
    &esp_flash_chip_winbond,
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_BOYA_CHIP
    &esp_flash_chip_boya,
#endif
#ifdef CONFIG_SPI_FLASH_SUPPORT_TH_CHIP
    &esp_flash_chip_th,
#endif

    /* Catch-all - must remain last. */
    &esp_flash_chip_generic,
    NULL,
};
