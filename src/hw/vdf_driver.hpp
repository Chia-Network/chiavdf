// Copyright Supranational LLC

#ifndef VDF_DRIVER_HPP
#define VDF_DRIVER_HPP

#include <gmp.h>
#include "ftdi_driver.hpp"

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

  // Enable the engine at the provided address
  void EnableEngine(unsigned control_csr) {
    uint32_t buf;
    
    // Clear reset
    buf = 0x0;
    ftdi.Write(control_csr, (uint8_t *)&buf, REG_BYTES);

    // Enable clock
    buf = 0x1;
    ftdi.Write(control_csr, (uint8_t *)&buf, REG_BYTES);
  }

  // Disable the engine at the provided address
  void DisableEngine(unsigned control_csr) {
    uint32_t buf;
    
    // Disable clock
    buf = 0x0;
    ftdi.Write(control_csr, (uint8_t *)&buf, REG_BYTES);

    // Set reset
    buf = 0x10;
    ftdi.Write(control_csr, (uint8_t *)&buf, REG_BYTES);
  }
};

#endif
