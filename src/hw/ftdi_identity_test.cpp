// Exercise the real parser and driver with emulated D2XX handles, without USB.
#define main hw_vdf_client_entry_for_identity_test
#include "hw_vdf_client.cpp"
#undef main

#include "emu_ftdi_test.hpp"
#include "ftdi_identity.hpp"
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
struct Fixture {
  EmuFtdiTestState saved = chia_vdf_emu_ftdi;
  Fixture() { chia_vdf_emu_ftdi = {}; }
  ~Fixture() { chia_vdf_emu_ftdi = saved; }
};

void require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

int parse(std::vector<std::string> args, vdf_client_opts &opts) {
  std::vector<char *> argv;
  for (auto &arg : args) argv.push_back(&arg[0]);
  return parse_opts(static_cast<int>(argv.size()), argv.data(), &opts);
}

void check_cli() {
  for (const std::string serial : {std::string("EMULATED"), std::string(15, 'x')}) {
    vdf_client_opts opts{};
    require(parse({"prog", "--expected-serial", serial, "5000"}, opts) == 0,
            "valid CLI serial rejected");
    require(opts.expected_serial == serial, "CLI serial not owned/stored");
    require(parse({"prog", "--expected-serial=" + serial, "5000"}, opts) == 0,
            "equals CLI serial rejected");
    require(opts.expected_serial == serial, "equals CLI serial not owned/stored");
    require(parse({"prog", "5000"}, opts) == 0 && opts.expected_serial.empty(),
            "default CLI serial not cleared");
  }
  for (const char *serial : {"", "space serial", "abcdefghijklmnop", "line\n", "tab\t", "del\x7f", "high\x80"}) {
    vdf_client_opts opts{};
    require(parse({"prog", "--expected-serial", serial, "5000"}, opts) == -1,
            "invalid CLI serial accepted");
  }
  vdf_client_opts opts{};
  require(parse({"prog", "5000", "--expected-serial"}, opts) == -1,
          "missing CLI serial accepted");
}

void check_modes() {
  for (const bool mode0 : {false, true}) {
    Fixture fixture;
    chia_vdf_emu_ftdi.mode0 = mode0;
    FtdiDriver driver;
    for (const char *expected : {"EMULATED", "EMULATEDA"}) {
      require(driver.Open(0, expected) == 0 && driver.IsOpen(), "correct board rejected");
      require(driver.Close() == 0, "close failed");
      require(chia_vdf_emu_ftdi.opens == chia_vdf_emu_ftdi.closes, "successful open leaked");
    }
    const std::string longest_base(15, 'x');
    chia_vdf_emu_ftdi.spi_serial = longest_base + "A";
    chia_vdf_emu_ftdi.gpio_serial = longest_base + (mode0 ? "B" : "D");
    vdf_client_opts opts{};
    require(parse({"prog", "--expected-serial", longest_base, "5000"}, opts) == 0,
            "maximum base rejected by CLI");
    require(driver.Open(0, opts.expected_serial.c_str()) == 0,
            "full-width interface serial rejected");
    require(driver.Close() == 0, "maximum-base close failed");
    require(driver.Open() == 0, "legacy mode open failed");
    require(driver.Close() == 0, "legacy mode close failed");
    // A board base ending in A is valid; the full interface-A serial is AA.
    chia_vdf_emu_ftdi.spi_serial = "AA";
    chia_vdf_emu_ftdi.gpio_serial = mode0 ? "AB" : "AD";
    require(driver.Open(0, "A") == 0, "one-character base rejected");
    require(driver.Close() == 0, "short-base close failed");
  }
}

void check_rejected(const std::function<void(EmuFtdiTestState &)> &change,
                    bool identity_failure) {
  for (const bool mode0 : {false, true}) {
    Fixture fixture;
    auto &state = chia_vdf_emu_ftdi;
    state.mode0 = mode0;
    change(state);
    FtdiDriver driver;
    require(driver.Open(0, "EMULATED") != 0 && !driver.IsOpen(), "failed open accepted");
    require(!state.spi_open && !state.gpio_open && state.opens == state.closes,
            "failed open leaked a handle");
    if (identity_failure) {
      require(state.configuration_calls == 0 && state.uninitializes == 0,
              "identity rejection modified an unconfirmed board");
    }
    if (state.fail_call == EmuFtdiFailure::FT4222_SetClock ||
        state.fail_call == EmuFtdiFailure::FT_SetUSBParameters)
      require(state.uninitializes == 1, "configured SPI bridge skipped UnInitialize");
    // Retry the expected-serial path, not just a legacy unguarded open.
    const auto opens = state.opens;
    state = {};
    state.mode0 = mode0;
    require(driver.Open(0, "EMULATED") == 0 && driver.IsOpen(), "retry after rejection failed");
    require(opens != 0 && driver.Close() == 0 && state.opens == state.closes,
            "retry cleanup failed");
  }
}

void check_failures() {
  check_rejected([](EmuFtdiTestState &s) { s.spi_serial = "OTHERA"; }, true);
  check_rejected([](EmuFtdiTestState &s) { s.gpio_serial = s.mode0 ? "OTHERB" : "OTHERD"; }, true);
  check_rejected([](EmuFtdiTestState &s) { s.spi_info_error = true; }, true);
  check_rejected([](EmuFtdiTestState &s) { s.gpio_info_error = true; }, true);
  for (const std::string serial : {"", "A", "EMULATED", "EMULATEDB", "bad A", "abcdefghijklmnop"})
    check_rejected([&](EmuFtdiTestState &s) { s.spi_serial = serial; }, true);
  check_rejected([](EmuFtdiTestState &s) { s.gpio_serial = s.mode0 ? "EMULATEDD" : "EMULATEDB"; }, true);
  for (const auto call : {EmuFtdiFailure::gpio_open, EmuFtdiFailure::FT4222_SetClock, EmuFtdiFailure::FT_SetUSBParameters,
                          EmuFtdiFailure::FT4222_SPIMaster_Init, EmuFtdiFailure::FT4222_SPIMaster_SetCS,
                          EmuFtdiFailure::FT4222_SPI_SetDrivingStrength, EmuFtdiFailure::FT4222_GPIO_Init,
                          EmuFtdiFailure::FT4222_SetSuspendOut, EmuFtdiFailure::FT4222_SetWakeUpInterrupt})
    check_rejected([&](EmuFtdiTestState &s) { s.fail_call = call; }, false);

  check_rejected([](EmuFtdiTestState &s) {
    if (!s.mode0) { s.mode1_b_available = true; s.gpio_d_unavailable = true; }
    else s.gpio_serial = "OTHERB";
  }, true);
  {
    Fixture fixture;
    auto &state = chia_vdf_emu_ftdi;
    state.mode1_b_available = true;
    state.gpio_d_unavailable = true;
    FtdiDriver driver;
    require(driver.Open() != 0 && state.configuration_calls == 0 &&
            state.uninitializes == 0 && state.opens == state.closes,
            "legacy open configured an incompatible GPIO interface");
  }
  {
    Fixture fixture;
    FtdiDriver driver;
    chia_vdf_emu_ftdi.spi_serial = "OTHERA";
    chia_vdf_emu_ftdi.fail_call = EmuFtdiFailure::spi_close;
    require(driver.Open(0, "EMULATED") != 0 && chia_vdf_emu_ftdi.spi_open,
            "identity/close failure fixture did not retain the stuck handle");
    require(chia_vdf_emu_ftdi.configuration_calls == 0 && chia_vdf_emu_ftdi.uninitializes == 0,
            "identity rollback configured foreign board");
    chia_vdf_emu_ftdi.fail_call = EmuFtdiFailure::none;
    require(driver.Close() == 0 && chia_vdf_emu_ftdi.opens == chia_vdf_emu_ftdi.closes,
            "identity rollback close retry leaked");
  }
  for (const auto call : {EmuFtdiFailure::spi_close, EmuFtdiFailure::gpio_close}) {
    Fixture fixture;
    FtdiDriver driver;
    require(driver.Open(0, "EMULATED") == 0, "close-error fixture open failed");
    chia_vdf_emu_ftdi.fail_call = call;
    require(driver.Close() != 0 && !driver.IsOpen() && chia_vdf_emu_ftdi.closes == 1,
            "one close failure skipped the other handle");
    chia_vdf_emu_ftdi.fail_call = EmuFtdiFailure::none;
    require(driver.Close() == 0 && chia_vdf_emu_ftdi.opens == chia_vdf_emu_ftdi.closes &&
            chia_vdf_emu_ftdi.uninitializes == 2,
            "failed close retry repeated uninitialize or lost the handle");
  }
  Fixture fixture;
  FtdiDriver driver;
  require(driver.Open(0, "EMULATED") == 0, "cleanup fixture open failed");
  chia_vdf_emu_ftdi.fail_call = EmuFtdiFailure::FT4222_UnInitialize;
  require(driver.Close() != 0, "uninitialize error hidden");
  require(!driver.IsOpen() && chia_vdf_emu_ftdi.uninitializes == 2 &&
          chia_vdf_emu_ftdi.opens == chia_vdf_emu_ftdi.closes,
          "uninitialize failure skipped handle cleanup");
  chia_vdf_emu_ftdi.fail_call = EmuFtdiFailure::none;
  require(driver.Open() == 0, "legacy optional open failed");
}

void check_diagnostics() {
  Fixture fixture;
  FtdiDriver driver;
  require(driver.Open(0, "EMULATED") == 0, "diagnostic fixture open failed");
  chia_vdf_emu_ftdi.gpio_info_error = true;
  auto result = ftdi_identity::confirm(reinterpret_cast<FT_HANDLE>(kEmuGpioHandle), 'D', "EMULATED");
  require(result.error != nullptr && std::string(result.error) == "unreadable" &&
          result.status == FT_INVALID_HANDLE, "D2XX error status lost");
  chia_vdf_emu_ftdi.gpio_info_error = false;
  result = ftdi_identity::confirm(reinterpret_cast<FT_HANDLE>(kEmuGpioHandle), 'D', "OTHER");
  require(result.error != nullptr && std::string(result.error) == "mismatch" &&
          result.status == FT_OK && result.serial == "EMULATEDD", "mismatch diagnostic lost");
}
} // namespace

int main() {
  try {
    check_cli();
    check_modes();
    check_failures();
    check_diagnostics();
    std::cout << "FTDI identity tests passed\n";
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "FTDI identity test failed: " << error.what() << '\n';
    return 1;
  }
}
