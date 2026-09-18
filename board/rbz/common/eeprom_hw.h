#ifndef __EEPROM_HW_H_
#define __EEPROM_HW_H_

int board_read_rom_eeprom(unsigned char *config);
phys_size_t get_ramsize(unsigned char config);

#endif
