/*
 * Copyright 2018-2019 NXP
 *
 * SPDX-License-Identifier:	GPL-2.0+
 */

#include <command.h>
#include <cpu_func.h>
#include <hang.h>
#include <image.h>
#include <init.h>
#include <log.h>
#include <spl.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <errno.h>
#include <asm/io.h>
#include <asm/arch/imx8mp_pins.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/boot_mode.h>
#include <power/pmic.h>

#include <power/pca9450.h>
#include <asm/arch/clock.h>
#include <dm/uclass.h>
#include <dm/device.h>
#include <dm/uclass-internal.h>
#include <dm/device-internal.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <fsl_esdhc_imx.h>
#include <mmc.h>
#include <asm/arch/ddr.h>
#include <asm/sections.h>
#include "../common/eeprom_hw.h"
#include "lpddr4_timing.h"
#include "ddr_handoff.h"

#include <debug_uart.h>

#define DEBUG

DECLARE_GLOBAL_DATA_PTR;

extern struct dram_timing_info dram_timing_512M;
extern struct dram_timing_info dram_timing_1G;
extern struct dram_timing_info dram_timing_2G;
extern struct dram_timing_info dram_timing_4G;

int spl_board_boot_device(enum boot_device boot_dev_spl)
{
#ifdef CONFIG_SPL_BOOTROM_SUPPORT
	return BOOT_DEVICE_BOOTROM;
#else
	switch (boot_dev_spl) {
	case SD1_BOOT:
	case MMC1_BOOT:
	case SD2_BOOT:
	case MMC2_BOOT:
		return BOOT_DEVICE_MMC1;
	case SD3_BOOT:
	case MMC3_BOOT:
		return BOOT_DEVICE_MMC2;
	case QSPI_BOOT:
		return BOOT_DEVICE_NOR;
	case NAND_BOOT:
		return BOOT_DEVICE_NAND;
	case USB_BOOT:
		return BOOT_DEVICE_BOARD;
	default:
		return BOOT_DEVICE_NONE;
	}
#endif
}

__weak unsigned int get_hw_version_spl(void)
{
  void *fdt = (void *)gd->fdt_blob;
  struct gpio_desc gpio[2];
  unsigned int gpio_value[2];
  unsigned int hw_version;
  ofnode node;
  int node2;
  int ret;

  node = ofnode_path("/config");
  if (!ofnode_valid(node)) {
    printf("%s: no /config node?\n", __func__);
    return 2;
  }

  ret = gpio_request_list_by_name_nodev(node, "rbz,hw-coding-gpios",
      gpio, ARRAY_SIZE(gpio), GPIOD_IS_IN);

  gpio_value[0] = dm_gpio_get_value(&(gpio[0]));
  gpio_value[1] = dm_gpio_get_value(&(gpio[1]));

  node2 = fdt_path_offset(fdt, "/chosen");

  if (gpio_value[0] == 0 && gpio_value[1] == 0) {
    hw_version = 3;
    fdt_setprop_string(fdt, node2, "hw-version", "3");
  }
  else {
    hw_version = 2;
    fdt_setprop_string(fdt, node2, "hw-version", "2");
  }

  gpio_free_list_nodev(gpio, ret);

  return hw_version;
} 

/* Function used to identify the DDR.
 * Function returns the timing parameters to use for DDR training, or NULL if failed
 * to identify the DDR.
 *
 * In order to read the DDR values, the function will train the DDR with
 * default parameters.
 * Those parameters may be the same parameters needed to train the DDR.
 * In this case, this function will set @needs_training to false, indicating that
 * there is no need to train the DDR again.
 * Caller can ignore this argument without harm (argument can be NULL).
 */
static struct dram_timing_info *spl_identify_ddr(bool *needs_training)
{
  int ret;
  unsigned int mr0, mr5, mr6, mr7, mr8;
  unsigned int hw_version;
  bool tmp;
  phys_size_t size;

  hw_version = get_hw_version_spl();
  printf("HW version: %d\n", hw_version);

  /* Init the @needs_training argument */
  if (!needs_training)
    needs_training = &tmp;
  *needs_training = true;

  /* Training with 4G Micron */
  if (!ddr_init(&dram_timing_4gb_micron)) {
    mr0 = lpddr4_mr_read(0xF, 0x0);
    mr5 = lpddr4_mr_read(0xF, 0x5);
    mr6 = lpddr4_mr_read(0xF, 0x6);
    mr7 = lpddr4_mr_read(0xF, 0x7);
    mr8 = lpddr4_mr_read(0xF, 0x8);

    printf("MR0=0x%x, MR5=0x%x, MR6=0x%x, MR7=0x%x, MR8=0x%x\n", mr0, mr5, mr6, mr7, mr8);

    printf("DDR 4G Micron idenfified!\n");
    *needs_training = false;
    size = (u64) 4096 << 20ULL;
    rbz_ddr_handoff_store(size);
    return &dram_timing_4gb_micron;
  } else {
    if (hw_version == 2) {
      /* Trainig with 1G Micron */
      ret = ddr_init(&dram_timing_1gb_micron);
      if (ret)
        goto err;

      mr5 = lpddr4_mr_read(0xF, 0x5);
      mr6 = lpddr4_mr_read(0xF, 0x6);
      mr7 = lpddr4_mr_read(0xF, 0x7);
      mr8 = lpddr4_mr_read(0xF, 0x8);

      printf("HW=2, MR5=0x%x, MR6=0x%x, MR7=0x%x, MR8=0x%x\n", mr5, mr6, mr7, mr8);

      if (mr5 == 0xff && mr6 == 0x54 && mr7 == 0x1 && mr8 == 0x10) {
        printf("DDR 1G Micron identified!\n");
        size = (u64) 1024 << 20ULL;
        rbz_ddr_handoff_store(size);
        *needs_training = false;
        return &dram_timing_1gb_micron;
      } else if (mr5 == 0xff && mr6 == 0x7 && mr7 == 0xb8 && mr8 == 0x10) {
        printf("DDR 2G Micron identified!\n");
        size = (u64) 1024 << 20ULL;
        rbz_ddr_handoff_store(size);
        return &dram_timing_1gb_micron;   // CAMBIAR A 2GB
      } else {
        printf("DDR 2G Micron identified!\n");
        size = (u64) 1024 << 20ULL;
        rbz_ddr_handoff_store(size);
        return &dram_timing_1gb_micron;   // CAMBIAR A 2GB
      }
    } else {
      /* Trainig with 1G Micron */
      ret = ddr_init(&dram_timing_1gb_micron);
      if (ret)
        goto err;
      mr0 = lpddr4_mr_read(0xF, 0x0);
      mr5 = lpddr4_mr_read(0xF, 0x5);
      mr6 = lpddr4_mr_read(0xF, 0x6);
      mr7 = lpddr4_mr_read(0xF, 0x7);
      mr8 = lpddr4_mr_read(0xF, 0x8);

      printf("HW=3, MR0=0x%x, MR5=0x%x, MR6=0x%x, MR7=0x%x, MR8=0x%x\n", mr0, mr5, mr6, mr7, mr8);

      if (mr0 == 0xb9 && mr5 == 0xff && mr6 == 0x07 && mr8 == 0x10) {
        printf("DDR 2G Micron identified!\n");
        size = (u64) 2048 << 20ULL;
        rbz_ddr_handoff_store(size);
        return &dram_timing_2gb_micron;
      } else if (mr0 == 0x98 && mr5 == 0xff && mr6 == 0x3 && mr8 == 0x8) {
        printf("DDR 1G Micron identified!\n");
        size = (u64) 1024 << 20ULL;
        rbz_ddr_handoff_store(size);
        return &dram_timing_2gb_micron;
      } else {
        printf("DDR not identified!\n");
        size = (u64) 1024 << 20ULL;
        rbz_ddr_handoff_store(size);
        return &dram_timing_2gb_micron;
      }
    }
  }

err:
  printf("Could not identify DDR!\n");
  return NULL;
}

void spl_dram_init(void)
{
  struct dram_timing_info *dram_info;
  int ret = -1;
  bool need_training;

  dram_info = spl_identify_ddr(&need_training);
  if (dram_info) {
    /* DDR was identified, do we need to train the DDR? */
    if (need_training)
      ret = ddr_init(dram_info);
    else
      ret = 0;
  }

  /* If we failed to identify the DDR, or the parameters returned from
   * spl_identify_ddr caused in DDR training failure - fall back to a
   * generic way to train the DDR.
   */
  if (ret == -1) {
      hang(); //Could not init the DDR - nothing we can do..
  }
}

#if CONFIG_IS_ENABLED(DM_PMIC_PCA9450)
int power_init_board(void)
{
	struct udevice *dev;
	int ret;

	ret = pmic_get("pca9450@25", &dev);
	if (ret == -ENODEV) {
		puts("No pca9450@25\n");
		return 0;
	}
	if (ret != 0)
		return ret;

	/* BUCKxOUT_DVS0/1 control BUCK123 output */
	pmic_reg_write(dev, PCA9450_BUCK123_DVS, 0x29);

	/*
	 * increase VDD_SOC to typical value 0.95V before first
	 * DRAM access, set DVS1 to 0.85v for suspend.
	 * Enable DVS control through PMIC_STBY_REQ and
	 * set B1_ENMODE=1 (ON by PMIC_ON_REQ=H)
	 */
	pmic_reg_write(dev, PCA9450_BUCK1OUT_DVS0, 0x1C);
	pmic_reg_write(dev, PCA9450_BUCK1OUT_DVS1, 0x14);
	pmic_reg_write(dev, PCA9450_BUCK1CTRL, 0x59);

	/* Kernel uses OD/OD freq for SOC */
	/* To avoid timing risk from SOC to ARM,increase VDD_ARM to OD voltage 0.95v */
	pmic_reg_write(dev, PCA9450_BUCK2OUT_DVS0, 0x1C);

	/* set WDOG_B_CFG to cold reset */
	pmic_reg_write(dev, PCA9450_RESET_CTRL, 0xA1);

	return 0;
}
#endif

void spl_board_init(void)
{
	arch_misc_init();

	/* Set GIC clock to 500Mhz for OD VDD_SOC. Kernel driver does not allow to change it.
	 * Should set the clock after PMIC setting done.
	 * Default is 400Mhz (system_pll1_800m with div = 2) set by ROM for ND VDD_SOC
	 */
	clock_enable(CCGR_GIC, 0);
	clock_set_target_val(GIC_CLK_ROOT, CLK_ROOT_ON | CLK_ROOT_SOURCE_SEL(5));
	clock_enable(CCGR_GIC, 1);

	puts("Normal Boot\n");
}

#ifdef CONFIG_SPL_LOAD_FIT
int board_fit_config_name_match(const char *name)
{
	/* Just empty function now - can't decide what to choose */
	debug("%s: %s\n", __func__, name);

	return 0;
}
#endif

void board_init_f(ulong dummy)
{
	struct udevice *dev;
	int ret;

	/* Clear the BSS. */
	memset(__bss_start, 0, __bss_end - __bss_start);

	arch_cpu_init();

	board_early_init_f();

	timer_init();

	ret = spl_early_init();
	if (ret) {
		debug("spl_early_init() failed: %d\n", ret);
		hang();
	}

	ret = uclass_get_device_by_name(UCLASS_CLK,
					"clock-controller@30380000",
					&dev);
	if (ret < 0) {
		printf("Failed to find clock node. Check device tree\n");
		hang();
	}

	preloader_console_init();

	enable_tzc380();

	power_init_board();

	/* DDR initialization */
	spl_dram_init();

	board_init_r(NULL, 0);
}
