#ifndef CHIAVDF_EMU_FTDI_TEST_HPP
#define CHIAVDF_EMU_FTDI_TEST_HPP

#include <string>
#include <cstdint>

// Test controls/counters defined only by emu_funcs.cpp, never the hardware build.
enum class EmuFtdiFailure {
  none,
  gpio_open,
  FT4222_SetClock,
  FT_SetUSBParameters,
  FT4222_SPIMaster_Init,
  FT4222_SPIMaster_SetCS,
  FT4222_SPI_SetDrivingStrength,
  FT4222_GPIO_Init,
  FT4222_SetSuspendOut,
  FT4222_SetWakeUpInterrupt,
  spi_close,
  gpio_close,
  FT4222_UnInitialize,
  FT4222_I2CMaster_Init
};

struct EmuFtdiTestState {
  bool mode0 = false;
  bool mode1_b_available = false;
  bool gpio_d_unavailable = false;
  bool gpio_is_b = false;
  std::string spi_serial = "EMULATEDA";
  std::string gpio_serial; // Empty selects EMULATEDB/D according to mode.
  bool spi_info_error = false;
  bool gpio_info_error = false;
  EmuFtdiFailure fail_call = EmuFtdiFailure::none;
  unsigned opens = 0;
  unsigned closes = 0;
  unsigned uninitializes = 0;
  unsigned configuration_calls = 0;
  bool spi_open = false;
  bool gpio_open = false;
};

constexpr uintptr_t kEmuSpiHandle = 0x100;
constexpr uintptr_t kEmuGpioHandle = 0x200;

extern EmuFtdiTestState chia_vdf_emu_ftdi;
#endif
