// Copyright Supranational

#ifndef __FTDI_DRIVER_HPP__
#define __FTDI_DRIVER_HPP__

#include <stdint.h>
#include <stdlib.h>

void print_buf(size_t offset, uint8_t *buf, size_t size);

class FtdiDriver {
public:
  typedef unsigned int DWORD;
  static const unsigned GPIO2 = 2;
  static const unsigned GPIO3 = 3;
  
  FtdiDriver();
  ~FtdiDriver();

  int List();

  bool IsOpen();
  
  // Open a connection to an automatically located FTDI device
  // Returns status, 0 for success
  int Open();

  // Open with given SPI clock parameters
  int OpenClk(unsigned sys_clk, unsigned clk_div, DWORD target_id = 0);

  // Open a connection to a specific FTDI device
  // Returns status, 0 for success
  int Open(DWORD spi_loc_id, unsigned sys_clk, unsigned clk_div);

  // Close the connection to the FTDI device
  int Close();

  // Note: caller should set sys_clk and clk_div to a default value
  // prior to calling.
  static void ParseParams(unsigned &sys_clk, unsigned &clk_div,
                          int argc, char *argv[]);

  // Write arbitrary size message
  // Returns status, 0 for success
  int Write(uint32_t addr, uint8_t *write_data, size_t size);

  // Read arbitrary size message
  // Returns status, 0 for success
  int Read(uint32_t addr, uint8_t *read_data, size_t size);
  
  // Write size bytes, starting at the specified register.
  // Size must be 4, 16, 32, 64, 128, or 256
  // Returns status, 0 for success
  int WriteCmd(uint32_t addr, uint8_t *write_data, size_t size);

  // Read size bytes, starting at the specified register. 
  // Size must be 4, 16, 32, 64, 128, or 256
  // Returns status, 0 for success
  int ReadCmd(uint32_t addr, uint8_t *read_data, size_t size);
  
  // Write size bytes, starting at the specified register.
  // Returns status, 0 for success
  int WriteRaw(uint8_t *write_buf, size_t size);

  // Read size bytes, starting at the specified register.
  // Returns status, 0 for success
  int ReadRaw(uint8_t *write_buf, size_t wsize,
              uint8_t *read_buf, size_t rsize);

  // TODO - Read/write abritrary sizes

  // Set GPIO output value
  // Port must be GPIO2 or GPIO3.
  int SetGPIO(int port, bool value);

  // Get GPIO output value
  // Port must be GPIO2 or GPIO3.
  int GetGPIO(int port, bool &value);

  // Toggle GPIO
  void ToggleGPIO(int port);
  
private:
  // Opaque pointer for the FTDI driver
  void *spi_ft_handle;
  void *gpio_ft_handle;
  
  // Location ID for the FTDI SPI device
  DWORD spi_loc_id;

  // Location ID for the FTDI GPIO device
  DWORD gpio_loc_id;

  // Internal transaction buffer
  uint8_t *int_write_buf;
  uint8_t *int_read_buf;
};

#endif
