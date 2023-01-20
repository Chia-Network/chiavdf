#ifndef VDF_DRIVER_HPP
#define VDF_DRIVER_HPP

#include <gmp.h>
#include "ftdi_driver.hpp"
#include "pvt.hpp"
#include "clock.hpp"

// Define the baseclass VDF driver
class VdfDriver {
public:
  // Bit sizes for polynomial conversion
  unsigned WORD_BITS;
  unsigned REDUNDANT_BITS;
  bool     SIGNED;

  // CSR width
  static const unsigned REG_BITS  = 32;
  static const unsigned REG_BYTES = 32/8;
  static const uint32_t RESET_BIT  = 0x10;
  static const uint32_t CLOCK_BIT  = 0x01;
  static const uint32_t BURST_ADDR = 0x300000;
  static const uint32_t VDF_ENGINE_STRIDE = 0x10000;

  FtdiDriver ftdi;

  VdfDriver(unsigned _WORD_BITS, unsigned _REDUNDANT_BITS,
            bool _SIGNED, FtdiDriver &_ftdi)
    : WORD_BITS(_WORD_BITS), REDUNDANT_BITS(_REDUNDANT_BITS),
      SIGNED(_SIGNED), ftdi(_ftdi) {
  }
  
  // Functions for serializing to buffers
  // Append size bytes to 'buf' starting at offset. Returns size.
  size_t write_bytes(size_t size, size_t offset, uint8_t *buf,
                     uint32_t val);
  size_t write_bytes(size_t size, size_t offset, uint8_t *buf,
                     uint64_t val);
  size_t write_bytes(size_t size, size_t offset, uint8_t *buf,
                     mpz_t val, size_t num_coeffs);

  // Functions for deserializing from buffers
  size_t read_bytes(size_t size, size_t offset, uint8_t *buf,
                    uint32_t &val);
  size_t read_bytes(size_t size, size_t offset, uint8_t *buf,
                    uint64_t &val);
  size_t read_bytes(size_t size, size_t offset, uint8_t *buf,
                    mpz_t val, size_t num_coeffs);
  
  // Returns the buffer size needed for a command
  virtual size_t CmdSize() = 0;

  // Returns the buffer size needed for a status
  virtual size_t StatusSize() = 0;

  // Perform a single dword register write, return 0 on success
  int RegWrite(uint32_t addr, uint32_t data) {
    uint32_t buf;
    int stat;
    write_bytes(REG_BYTES, 0, (uint8_t *)&buf, data);
    stat = ftdi.WriteCmd(addr, (uint8_t *)&buf, REG_BYTES);
    if (stat) {
      printf("ftdi.WriteCmd in RegWrite failed (error %d)\n",stat);
      return stat;
    };
    return 0;
  }

  // Perform a single dword register read, return 0 on success
  int RegRead(uint32_t addr, uint32_t &data) {
    uint32_t buf;
    int stat;
    stat = ftdi.ReadCmd(addr, (uint8_t *)&buf, REG_BYTES);
    if (stat) {
      printf("ftdi.ReadCmd in RegRead failed (error %d)\n",stat);
      return stat;
    };
    read_bytes(REG_BYTES, 0, (uint8_t *)&buf, data);
    return 0;
  }

  // Enable the engine at the provided address
  void EnableEngine(unsigned control_csr) {
    RegWrite(control_csr, (uint32_t)0);  // Clear reset
    RegWrite(control_csr, CLOCK_BIT);    // Enable clock
  }

  // Disable the engine at the provided address
  void DisableEngine(unsigned control_csr) {
    RegWrite(control_csr, (uint32_t)0);  // Disable clock
    RegWrite(control_csr, RESET_BIT);    // Set reset
  }

  // Reset the engine at the provided address
  void ResetEngine(unsigned control_csr) {
    DisableEngine(control_csr);
    EnableEngine(control_csr);
  }

  void EnablePvt();
  double value_to_temp(uint32_t temp_code);
  uint32_t temp_to_value(double temp);
  double GetPvtTemp();
  double GetPvtVoltage();
  double GetTempAlarmExternal();
  double GetTempAlarmEngine();
  bool IsTempAlarmExternalSet();
  bool IsTempAlarmEngineSet();
  bool IsTempAlarmSet();
  bool SetTempAlarmExternal(double temp);
  bool SetTempAlarmEngine(double temp);
  void ResetPLL();
  bool SetPLLFrequency(double frequency);
  double GetPLLFrequency();
  int Reset(uint32_t sleep_duration);

  void PrintVRRegs();
  double GetBoardVoltage();
  int SetBoardVoltage(double voltage);

  void PrintCSRegs();
  double GetBoardCurrent();

  double GetPower();
};
#endif
