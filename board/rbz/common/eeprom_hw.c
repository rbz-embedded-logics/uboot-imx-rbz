#include <efi_loader.h>
#include <env.h>
#include <init.h>
#include <asm/global_data.h>
#include <asm/mach-imx/iomux-v3.h>
#include <asm-generic/gpio.h>
#include <asm/arch/imx8mn_pins.h>
#include <asm/arch/clock.h>
#include <asm/arch/sys_proto.h>
#include <asm/mach-imx/gpio.h>
#include <asm/mach-imx/mxc_i2c.h>
#include <i2c.h>
#include <asm/io.h>
#include <usb.h>
#include <imx_sip.h>
#include <linux/arm-smccc.h>
#include <dm/uclass.h>

u32 ram_sizes_mb[16] = {
  512,
  1024,
  2048,
  3072,
  4096,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
  0,
};

#define EEPROM_ASSEMBLY_OFFSET 0xfe

__weak int board_read_rom_eeprom(unsigned char *config)
{
  int ret = -EINVAL;
  struct udevice *dev;
  ofnode eeprom;

  eeprom = ofnode_get_chosen_node("rbz,eeprom_config");
  if (!ofnode_valid(eeprom)) {
    return -ENODEV;
  }

  debug("%s: Path to EEPROM %s\n", __func__,
      ofnode_read_chosen_string("rbz,eeprom_config"));

  ret = uclass_get_device_by_ofnode(UCLASS_I2C_EEPROM, eeprom, &dev);
  if (ret) {
    return ret;
  }

  ret = dm_i2c_read(dev, EEPROM_ASSEMBLY_OFFSET, config, 1);
  if (ret) {
    debug("%s: I2C EEPROM address read failed\n", __func__);
    return ret;
  }

  return 0;
}

phys_size_t get_ramsize(unsigned char config)
{
  return (u64)ram_sizes_mb[config] << 20ULL;
}
