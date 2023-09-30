#include <cstdint>

int emu_do_io(uint8_t *buf_in, uint16_t size_in, uint8_t *buf_out, uint16_t size_out);
int emu_do_io_i2c(uint8_t *buf, uint16_t size, uint32_t addr, int is_out);
