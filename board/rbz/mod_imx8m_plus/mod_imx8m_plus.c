// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2019 NXP
 */

#include <common.h>
#include <efi_loader.h>
#include <env.h>
#include <errno.h>
#include <init.h>
#include <miiphy.h>
#include <netdev.h>
#include <linux/delay.h>
#include <asm/global_data.h>
#include <asm/io.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <asm/arch/imx8mp_pins.h>
#include <asm/arch/clock.h>
#include <asm/arch/ddr.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <spl.h>
#include <asm/mach-imx/dma.h>
#include <power/pmic.h>
#include <usb.h>
#include <dwc3-uboot.h>
#include <imx_sip.h>
#include <linux/arm-smccc.h>
#include <mmc.h>
#include <i2c.h>
#include "../common/eeprom_hw.h"

DECLARE_GLOBAL_DATA_PTR;

#define HW_VER_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

#define HW_VER_0    IMX_GPIO_NR(3, 14)
#define HW_VER_1    IMX_GPIO_NR(5, 5)

static iomux_v3_cfg_t hv_mux_gpio[] = {
  MX8MP_PAD_NAND_DQS__GPIO3_IO14 | MUX_PAD_CTRL(HW_VER_PAD_CTRL),
  MX8MP_PAD_SPDIF_EXT_CLK__GPIO5_IO05 | MUX_PAD_CTRL(HW_VER_PAD_CTRL),
};

__weak unsigned int get_hw_version(void)
{
  unsigned int gpio_value[2];
  unsigned int hw_version;

  imx_iomux_v3_setup_multiple_pads(hv_mux_gpio, ARRAY_SIZE(hv_mux_gpio));
  gpio_request(HW_VER_0, "hw_ver_0");
  gpio_request(HW_VER_1, "hw_ver_1");
  gpio_direction_input(HW_VER_0);
  gpio_direction_input(HW_VER_1);
  gpio_value[0] = gpio_get_value(HW_VER_0);
  gpio_value[1] = gpio_get_value(HW_VER_1);

  if (gpio_value[0] == 0 && gpio_value[1] == 0)
    hw_version = 3;
  else
    hw_version = 2;

  return hw_version;
} 

__weak unsigned int lpddr4_mr_read(unsigned int mr_rank, unsigned int mr_addr)
{
  unsigned int tmp;

  reg32_write(DRC_PERF_MON_MRR0_DAT(0), 0x1);

  do {
    tmp = reg32_read(DDRC_MRSTAT(0));
  } while (tmp & 0x1);

  reg32_write(DDRC_MRCTRL0(0), (mr_rank << 4) | 0x1);
  reg32_write(DDRC_MRCTRL1(0), (mr_addr << 8));
  reg32setbit(DDRC_MRCTRL0(0), 31);
  do {
    tmp = reg32_read(DRC_PERF_MON_MRR0_DAT(0));
  } while ((tmp & 0x8) == 0);
  tmp = reg32_read(DRC_PERF_MON_MRR1_DAT(0));
  reg32_write(DRC_PERF_MON_MRR0_DAT(0), 0x4);

  while (tmp) { //try to find a significant byte in the word
    if (tmp & 0xff) {
      tmp &= 0xff;
      break;
    }
    tmp >>= 8;
  }

  return tmp;
}

int board_phys_sdram_size(phys_size_t *size)
{
  unsigned int hw_version = 3;
  unsigned int mr0, mr5, mr6, mr7, mr8;

  hw_version = get_hw_version();

  mr0 = lpddr4_mr_read(0xF, 0x0);
  mr5 = lpddr4_mr_read(0xF, 0x5);
  mr6 = lpddr4_mr_read(0xF, 0x6);
  mr7 = lpddr4_mr_read(0xF, 0x7);
  mr8 = lpddr4_mr_read(0xF, 0x8);
  printf("hw_version=%d, MR0=0x%x, MR5=0x%x, MR6=0x%x, MR7=0x%x, MR8=0x%x\n", hw_version, mr0, mr5, mr6, mr7, mr8);

  if (hw_version == 2) {
    if (mr5 == 0xff && mr6 == 0x7 && mr7 == 0x0 && mr8 == 0x10) {
      *size = (u64)4096 << 20ULL;
    } else if (mr5 == 0xff && mr6 == 0x7 && mr7 == 0xb8 && mr8 == 0x10) {
      *size = (u64)1024 << 20ULL;  // CAMBIAR A 2GB
    } else if (mr5 == 0xff && mr6 == 0x54 && mr7 == 0x1 && mr8 == 0x10) {
      *size = (u64)1024 << 20ULL;
    } else {
      *size = (u64)1024 << 20ULL;
    }
  } else if (hw_version == 3) {
    if (mr0 == 0x39 && mr5 == 0xff && mr6 == 0x07 && mr8 == 0x10) {
      *size = (u64)4096 << 20ULL;
    } else if (mr0 == 0xb9 && mr5 == 0xff && mr6 == 0x07 && mr8 == 0x10) {
      *size = (u64)2048 << 20ULL;
    } else if (mr0 == 0x98 && mr5 == 0xff && mr6 == 0x03 && mr8 == 0x08) {
      *size = (u64)1024 << 20ULL;
    } else {
      *size = (u64)1024 << 20ULL;
    }
  } else {
    *size = (u64)1024 << 20ULL;
  }

  return 0;
}

#define UART_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_FSEL1)
#define WDOG_PAD_CTRL	(PAD_CTL_DSE6 | PAD_CTL_ODE | PAD_CTL_PUE | PAD_CTL_PE)

static iomux_v3_cfg_t const uart_pads[] = {
	MX8MP_PAD_UART3_RXD__UART3_DCE_RX | MUX_PAD_CTRL(UART_PAD_CTRL),
	MX8MP_PAD_UART3_TXD__UART3_DCE_TX | MUX_PAD_CTRL(UART_PAD_CTRL),
};

static iomux_v3_cfg_t const wdog_pads[] = {
	MX8MP_PAD_GPIO1_IO02__WDOG1_WDOG_B  | MUX_PAD_CTRL(WDOG_PAD_CTRL),
};

#if CONFIG_IS_ENABLED(EFI_HAVE_CAPSULE_SUPPORT)
struct efi_fw_image fw_images[] = {
	{
		.image_type_id = IMX_BOOT_IMAGE_GUID,
		.fw_name = u"IMX8MP-MOD-RBZ-RAW",
		.image_index = 1,
	},
};

struct efi_capsule_update_info update_info = {
	.dfu_string = "mmc 2=flash-bin raw 0 0x2000 mmcpart 1",
	.images = fw_images,
};

u8 num_image_type_guids = ARRAY_SIZE(fw_images);
#endif /* EFI_HAVE_CAPSULE_SUPPORT */

int board_early_init_f(void)
{
	struct wdog_regs *wdog = (struct wdog_regs *)WDOG1_BASE_ADDR;

	imx_iomux_v3_setup_multiple_pads(wdog_pads, ARRAY_SIZE(wdog_pads));

	set_wdog_reset(wdog);

	imx_iomux_v3_setup_multiple_pads(uart_pads, ARRAY_SIZE(uart_pads));

	init_uart_clk(2);

	return 0;
}

#define EEPROM_MAC_OFFSET 0x9A

int board_read_rom_ethaddr(void)
{
  int ret = -EINVAL;
  unsigned char mac[6] = {};
  struct udevice *dev;
  ofnode eeprom;

  eeprom = ofnode_get_chosen_node("rbz,eeprom");
  if (!ofnode_valid(eeprom))
    return -ENODEV;

  ret = uclass_get_device_by_ofnode(UCLASS_I2C_EEPROM, eeprom, &dev);
  if (ret)
    return ret;

  ret = dm_i2c_read(dev, EEPROM_MAC_OFFSET, mac, 6);
  if (ret) {
    debug("%s: I2C EEPROM MAC address read failed\n", __func__);
    return ret;
  }

  if (is_valid_ethaddr(mac))
    eth_env_set_enetaddr("ethaddr", mac);

  return 0;
}

#ifdef CONFIG_OF_BOARD_SETUP
int ft_board_setup(void *blob, struct bd_info *bd)
{
	return 0;
}
#endif

#ifdef CONFIG_USB_DWC3

#define USB_PHY_CTRL0			0xF0040
#define USB_PHY_CTRL0_REF_SSP_EN	BIT(2)

#define USB_PHY_CTRL1			0xF0044
#define USB_PHY_CTRL1_RESET		BIT(0)
#define USB_PHY_CTRL1_COMMONONN		BIT(1)
#define USB_PHY_CTRL1_ATERESET		BIT(3)
#define USB_PHY_CTRL1_VDATSRCENB0	BIT(19)
#define USB_PHY_CTRL1_VDATDETENB0	BIT(20)

#define USB_PHY_CTRL2			0xF0048
#define USB_PHY_CTRL2_TXENABLEN0	BIT(8)

#define USB_PHY_CTRL6			0xF0058

#define HSIO_GPR_BASE                               (0x32F10000U)
#define HSIO_GPR_REG_0                              (HSIO_GPR_BASE)
#define HSIO_GPR_REG_0_USB_CLOCK_MODULE_EN_SHIFT    (1)
#define HSIO_GPR_REG_0_USB_CLOCK_MODULE_EN          (0x1U << HSIO_GPR_REG_0_USB_CLOCK_MODULE_EN_SHIFT)


static struct dwc3_device dwc3_device_data = {
#ifdef CONFIG_SPL_BUILD
	.maximum_speed = USB_SPEED_HIGH,
#else
	.maximum_speed = USB_SPEED_SUPER,
#endif
	.base = USB1_BASE_ADDR,
	.dr_mode = USB_DR_MODE_PERIPHERAL,
	.index = 0,
	.power_down_scale = 2,
};

int usb_gadget_handle_interrupts(int index)
{
	dwc3_uboot_handle_interrupt(index);
	return 0;
}

static void dwc3_nxp_usb_phy_init(struct dwc3_device *dwc3)
{
	u32 RegData;

	/* enable usb clock via hsio gpr */
	RegData = readl(HSIO_GPR_REG_0);
	RegData |= HSIO_GPR_REG_0_USB_CLOCK_MODULE_EN;
	writel(RegData, HSIO_GPR_REG_0);

	/* USB3.0 PHY signal fsel for 100M ref */
	RegData = readl(dwc3->base + USB_PHY_CTRL0);
	RegData = (RegData & 0xfffff81f) | (0x2a<<5);
	writel(RegData, dwc3->base + USB_PHY_CTRL0);

	RegData = readl(dwc3->base + USB_PHY_CTRL6);
	RegData &=~0x1;
	writel(RegData, dwc3->base + USB_PHY_CTRL6);

	RegData = readl(dwc3->base + USB_PHY_CTRL1);
	RegData &= ~(USB_PHY_CTRL1_VDATSRCENB0 | USB_PHY_CTRL1_VDATDETENB0 |
			USB_PHY_CTRL1_COMMONONN);
	RegData |= USB_PHY_CTRL1_RESET | USB_PHY_CTRL1_ATERESET;
	writel(RegData, dwc3->base + USB_PHY_CTRL1);

	RegData = readl(dwc3->base + USB_PHY_CTRL0);
	RegData |= USB_PHY_CTRL0_REF_SSP_EN;
	writel(RegData, dwc3->base + USB_PHY_CTRL0);

	RegData = readl(dwc3->base + USB_PHY_CTRL2);
	RegData |= USB_PHY_CTRL2_TXENABLEN0;
	writel(RegData, dwc3->base + USB_PHY_CTRL2);

	RegData = readl(dwc3->base + USB_PHY_CTRL1);
	RegData &= ~(USB_PHY_CTRL1_RESET | USB_PHY_CTRL1_ATERESET);
	writel(RegData, dwc3->base + USB_PHY_CTRL1);
}
#endif

#if defined(CONFIG_USB_DWC3) || defined(CONFIG_USB_XHCI_IMX8M)
#define USB2_PWR_EN IMX_GPIO_NR(1, 14)
int board_usb_init(int index, enum usb_init_type init)
{
	int ret = 0;
	imx8m_usb_power(index, true);

	if (index == 0 && init == USB_INIT_DEVICE) {
		dwc3_nxp_usb_phy_init(&dwc3_device_data);
		return dwc3_uboot_init(&dwc3_device_data);
	} else if (index == 0 && init == USB_INIT_HOST) {
		return ret;
	}

	return 0;
}

int board_usb_cleanup(int index, enum usb_init_type init)
{
	int ret = 0;
	if (index == 0 && init == USB_INIT_DEVICE) {
		dwc3_uboot_exit(index);
	} else if (index == 0 && init == USB_INIT_HOST) {
	}

	imx8m_usb_power(index, false);

	return ret;
}
#endif

static int setup_eqos(void)
{
	struct iomuxc_gpr_base_regs *gpr =
		(struct iomuxc_gpr_base_regs *)IOMUXC_GPR_BASE_ADDR;

	/* set INTF as RGMII, enable RGMII TXC clock */
	clrsetbits_le32(&gpr->gpr[1],
			IOMUXC_GPR_GPR1_GPR_ENET_QOS_INTF_SEL_MASK, BIT(16));
	setbits_le32(&gpr->gpr[1], BIT(19) | BIT(21));

	return set_clk_eqos(ENET_125MHZ);
}

#if CONFIG_IS_ENABLED(NET)
int board_phy_config(struct phy_device *phydev)
{
	if (phydev->drv->config)
		phydev->drv->config(phydev);
	return 0;
}
#endif

#define DISPMIX				13
#define MIPI				15

int board_init(void)
{
	struct arm_smccc_res res;

	if (CONFIG_IS_ENABLED(DWC_ETH_QOS)) {
		setup_eqos();
	}

#if defined(CONFIG_USB_DWC3) || defined(CONFIG_USB_XHCI_IMX8M)
	init_usb_clk();
#endif

	/* enable the dispmix & mipi phy power domain */
	arm_smccc_smc(IMX_SIP_GPC, IMX_SIP_GPC_PM_DOMAIN,
		      DISPMIX, true, 0, 0, 0, 0, &res);
	arm_smccc_smc(IMX_SIP_GPC, IMX_SIP_GPC_PM_DOMAIN,
		      MIPI, true, 0, 0, 0, 0, &res);

	return 0;
}

#define SATA_RST_PAD IMX_GPIO_NR(2, 8)
static iomux_v3_cfg_t const sata_rst_pads[] = {
      MX8MP_PAD_SD1_DATA6__GPIO2_IO08 | MUX_PAD_CTRL(NO_PAD_CTRL),
};


int board_late_init(void)
{
#ifdef CONFIG_ENV_IS_IN_MMC
	board_late_mmc_env_init();
#endif
#ifdef CONFIG_ENV_VARS_UBOOT_RUNTIME_CONFIG
	env_set("board_name", "MOD_IMX8M_PLUS");
	env_set("board_rev", "RV00");
#endif

    board_read_rom_ethaddr();

    imx_iomux_v3_setup_multiple_pads(sata_rst_pads,
        ARRAY_SIZE(sata_rst_pads));

    gpio_request(SATA_RST_PAD, "sata_rst");
    gpio_direction_output(SATA_RST_PAD, 0);

	return 0;
}

#ifdef CONFIG_ANDROID_SUPPORT
bool is_power_key_pressed(void) {
	return (bool)(!!(readl(SNVS_HPSR) & (0x1 << 6)));
}
#endif

#ifdef CONFIG_SPL_MMC_SUPPORT
#define UBOOT_RAW_SECTOR_OFFSET 0x40
unsigned long spl_mmc_get_uboot_raw_sector(struct mmc *mmc)
{
	u32 boot_dev = spl_boot_device();
	switch (boot_dev) {
		case BOOT_DEVICE_MMC2:
			return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR - UBOOT_RAW_SECTOR_OFFSET;
		default:
			return CONFIG_SYS_MMCSD_RAW_MODE_U_BOOT_SECTOR;
	}
}
#endif

#ifdef CONFIG_FSL_FASTBOOT
#ifdef CONFIG_ANDROID_RECOVERY
int is_recovery_key_pressing(void)
{
	return 0; /* TODO */
}
#endif /* CONFIG_ANDROID_RECOVERY */
#endif /* CONFIG_FSL_FASTBOOT */
