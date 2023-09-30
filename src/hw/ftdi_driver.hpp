#ifndef __FTDI_DRIVER_HPP__
#define __FTDI_DRIVER_HPP__

#include <stdint.h>
#include <stdlib.h>

extern "C" {
#include "ftd2xx.h"
#include "libft4222.h"
}

void print_buf(size_t offset, uint8_t *buf, size_t size);

class FtdiDriver {
public:
  typedef unsigned int DWORD;
  static const unsigned GPIO2 = 2;
  static const unsigned GPIO3 = 3;

  FtdiDriver();
  ~FtdiDriver();

  void eprintf(const char * format, ...);

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

  // Enumeration of FTDI statemachine operation modes
  typedef enum {
    MODE_none,
    MODE_spi,
    MODE_i2c
  } MODE_t;

  // Select mode for FTDI state machine transfers
  int SetMode(FtdiDriver::MODE_t mode);

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
  // Port must be GPIO_PORT2 or GPIO_PORT3.
  int SetGPIO(GPIO_Port port, bool value);

  // Get GPIO output value
  // Port must be GPIO_PORT2 or GPIO_PORT3.
  int GetGPIO(GPIO_Port port, bool &value);

  // Tristate GPIO output
  // Port must be GPIO_PORT2 or GPIO_PORT3.
  int TriGPIO(GPIO_Port port);

  // Toggle GPIO
  void ToggleGPIO(GPIO_Port port);

  typedef enum {
    I2C_RETURN_CODE_success = 0,
    I2C_RETURN_CODE_fail,
    I2C_RETURN_CODE_timeout,
    I2C_RETURN_CODE_sdaBadState,
    I2C_RETURN_CODE_lostArbitration,
    I2C_RETURN_CODE_nack,
    I2C_RETURN_CODE_checkSumFail,
    I2C_RETURN_CODE__last
  } I2C_RETURN_CODE_t;

  const char *i2c_error_string[32] =
    {
      "success",
      "fail",
      "timeout",
      "sdaBadState",
      "lostArbitration",
      "nack",
      "checkSumFail",
      "(badErrorCode)"
    };

  int i2c_Init();

  int SendStopCondition();

  int i2c_TransmitX
  ( int sendStartCondition,
    int sendStopCondition,
    int slaveAddress,
    uint8_t *buf,
    uint16_t bytesToXfer,
    uint16_t &bytesXfered );

  int i2c_ReceiveX
  ( int sendStartCondition,
    int sendStopCondition,
    int slaveAddress,
    uint8_t *buf,
    uint16_t bytesToXfer,
    uint16_t &bytesXfered );

  const char *i2c_error
  (int code);

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

  // detected FTDI4222 CNF MODE
  int cnfmode;

  GPIO_Dir gpio_dir[4];

  // selected SPI clock divider
  FT4222_SPIClock spi_clk_div;

  // current state machine transfer mode
  MODE_t active_mode = MODE_none;
};
#endif
