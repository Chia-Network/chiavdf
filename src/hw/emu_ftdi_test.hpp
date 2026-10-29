#ifndef CHIAVDF_EMU_FTDI_TEST_HPP
#define CHIAVDF_EMU_FTDI_TEST_HPP

#include <string>

// Test controls/counters defined only by emu_funcs.cpp, never the hardware build.
struct EmuFtdiTestState {
  bool mode0 = false;
  std::string spi_serial = "EMULATEDA";
  std::string gpio_serial; // Empty selects EMULATEDB/D according to mode.
  bool spi_info_error = false;
  bool gpio_info_error = false;
  const char *fail_call = nullptr;
  unsigned opens = 0;
  unsigned closes = 0;
  unsigned uninitializes = 0;
  unsigned configuration_calls = 0;
  bool spi_open = false;
  bool gpio_open = false;
};

extern EmuFtdiTestState chia_vdf_emu_ftdi;
#endif
