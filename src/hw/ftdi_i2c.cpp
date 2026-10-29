#include "ftdi_driver.hpp"
#include <chrono>

using Clock = std::chrono::high_resolution_clock;

#if USE_I2C_BITBANG

// -----------------------------------------------------------------------------
// Bitbang I2C

void FtdiDriver::Delay()
{
  if (!delayDisable) {
    uint64_t diff;
    auto t0 = Clock::now();
    do {
      auto t1 = Clock::now();
      diff = std::chrono::duration_cast<std::chrono::microseconds>(t1-t0).count();
    } while (diff < 4);
  }
}

void FtdiDriver::ClearSCL()
{
  if (gpio_dir[port_SCL] != GPIO_OUTPUT) {
    gpio_dir[port_SCL] = GPIO_OUTPUT;
    FT4222_GPIO_Init(gpio_ft_handle, gpio_dir);
  }
}

void FtdiDriver::ClearSDA()
{
  if (gpio_dir[port_SDA] != GPIO_OUTPUT) {
    gpio_dir[port_SDA] = GPIO_OUTPUT;
    FT4222_GPIO_Init(gpio_ft_handle, gpio_dir);
  }
}

int FtdiDriver::ReadSCL()
{
  if (gpio_dir[port_SCL] != GPIO_INPUT) {
    gpio_dir[port_SCL] = GPIO_INPUT;
    FT4222_GPIO_Init(gpio_ft_handle, gpio_dir);
  }
  bool val;
  GetGPIO(port_SCL, val);
  return val ? 1 : 0;
}

int FtdiDriver::ReadSDA()
{
  if (gpio_dir[port_SDA] != GPIO_INPUT) {
    gpio_dir[port_SDA] = GPIO_INPUT;
    FT4222_GPIO_Init(gpio_ft_handle, gpio_dir);
  }
  bool val;
  GetGPIO(port_SDA, val);
  return val ? 1 : 0;
}

int FtdiDriver::ReadBit(int *bit)
{
  // "ReadSDA" makes SDA an input - processor lets go of pin and internal
  //  pull-up resistor makes it high.  Now slave can drive the pin.
  ReadSDA();

  Delay();

  // Clock stretching - Makes SCL an input and pull-up resistor makes
  //  the pin high.  Slave device can pull SCL low to extend clock cycle.
  if (!clockStretchDisable) {
    auto t0 = Clock::now();
    while (!ReadSCL())
      {
        // Check for timeout
        if (timeoutEnable) {
          auto t1 = Clock::now();
          uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
          if (diff > 2000)
            return I2C_RETURN_CODE_timeout;
        }
      }
  } else {
    ReadSCL();
  }

  // At this point, SCL is high and SDA is valid - so read the bit.
  *bit = ReadSDA();

  Delay();

  ClearSCL();     // Pull the serial clock line low ...

  return I2C_RETURN_CODE_success;     //  and return.
}

int FtdiDriver::WriteBit(int bit)
{
  if (bit)
    {
      ReadSDA();      // Make SDA an input ... so pin is pulled up.
    }
  else
    {
      ClearSDA();     // Make SDA an output ... so pin is pulled low.
    }

  Delay();

  // Clock stretching - Makes SCL an input and pull-up resistor makes
  //  the pin high.  Slave device can pull SCL low to extend clock cycle.
  if (!clockStretchDisable) {
    auto t0 = Clock::now();
    while (!ReadSCL())
      {
        // Check for timeout
        if (timeoutEnable) {
          auto t1 = Clock::now();
          uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
          if (diff > 2000)
            return I2C_RETURN_CODE_timeout;
        }
      }
  } else {
    ReadSCL();
  }

  // SCL is high and SDA is valid ...
  //  Check that nobody else is driving SDA
  if (bit && !ReadSDA())
    {
      return I2C_RETURN_CODE_lostArbitration;       // Lost arbitration
    }

  Delay();
  ClearSCL();

  return I2C_RETURN_CODE_success;           // Success!
}

int FtdiDriver::SendStartCondition()
{
  if (start)
    {
      // set SDA to 1
      ReadSDA();
      Delay();

      // Clock stretching - Makes SCL an input and pull-up resistor makes
      //  the pin high.  Slave device can pull SCL low to extend clock cycle.
      if (!clockStretchDisable) {
        auto t0 = Clock::now();
        while (!ReadSCL())
        {
            // Check for timeout
            if (timeoutEnable) {
              auto t1 = Clock::now();
              uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
              if (diff > 2000)
                return I2C_RETURN_CODE_timeout;
            }
          }
      } else {
        ReadSCL();
      }
    }

  if (!ReadSDA())
    {
      return I2C_RETURN_CODE_sdaBadState;
    }

  // SCL is high, set SDA from 1 to 0
  ClearSDA();
  Delay();
  ClearSCL();

  start = 1;

  return I2C_RETURN_CODE_success;
}

int FtdiDriver::SendStopCondition()
{
  // set SDA to 0
  ClearSDA();
  Delay();

  // Clock stretching - Makes SCL an input and pull-up resistor makes
  //  the pin high.  Slave device can pull SCL low to extend clock cycle.
  if (!clockStretchDisable) {
    auto t0 = Clock::now();
    while (!ReadSCL())
      {
        // Check for timeout
        if (timeoutEnable) {
          auto t1 = Clock::now();
          uint64_t diff = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
          if (diff > 2000)
            return I2C_RETURN_CODE_timeout;
        }
      }
  } else {
    ReadSCL();
  }

  // SCL is high, set SDA from 0 to 1
  ReadSDA();
  Delay();

  start = 0;

  return I2C_RETURN_CODE_success;
}

#endif

int FtdiDriver::i2c_Init()
{
#if USE_I2C_BITBANG
  port_SCL = GPIO_PORT0;
  port_SDA = GPIO_PORT1;

  timeoutEnable = 1;
  clockStretchDisable = 1;
  delayDisable = 1;
  start = 0;

  if (!ReadSCL()) return I2C_RETURN_CODE_timeout;
  if (!ReadSDA()) return I2C_RETURN_CODE_timeout;
#endif

  return I2C_RETURN_CODE_success;
}

int FtdiDriver::i2c_TransmitX
( int sendStartCondition,
  int sendStopCondition,
  int slaveAddress,
  uint8_t *buf,
  uint16_t bytesToXfer,
  uint16_t &bytesXfered )
{
#if !USE_I2C_BITBANG

  FT_STATUS s;

  s = SetMode(MODE_i2c);
  if (s!=FT_OK)
    return I2C_RETURN_CODE_fail;

  s = FT4222_I2CMaster_WriteEx
    (spi_ft_handle,
     slaveAddress,
     (sendStartCondition ? START : 0) | (sendStopCondition ? STOP : 0),
     buf,
     bytesToXfer,
     &bytesXfered);

  if (s!=FT_OK)
    return I2C_RETURN_CODE_fail;

  return I2C_RETURN_CODE_success;

#else

  int ret;
  int bit, nack;

  if (sendStartCondition) {
    ret = SendStartCondition();
    if (ret != I2C_RETURN_CODE_success)
      goto transmitx_error;
  }

  // send slave address and write flag
  {
    uint8_t byteToSend = (slaveAddress<<1) | 0;
    for (bit = 0; bit < 8; bit++) {
      ret = WriteBit((byteToSend & 0x80) != 0);
      if (ret != I2C_RETURN_CODE_success)
        goto transmitx_error;
      byteToSend <<= 1;
    }

    ret = ReadBit(&nack);
    if (ret != I2C_RETURN_CODE_success)
      goto transmitx_error;
  }

  // send packet
  if (!nack) {
    for(bytesXfered = 0; bytesXfered < bytesToXfer; bytesXfered++) {

      uint8_t byteToSend = buf[bytesXfered];
      for (bit = 0; bit < 8; bit++) {
        ret = WriteBit((byteToSend & 0x80) != 0);
        if (ret != I2C_RETURN_CODE_success)
          goto transmitx_error;
        byteToSend <<= 1;
      }

      ret = ReadBit(&nack);
      if (ret != I2C_RETURN_CODE_success)
        goto transmitx_error;

      if (nack)
        break;
    }
  }

  if (sendStopCondition) {
    ret = SendStopCondition();
    if (ret != I2C_RETURN_CODE_success)
      goto transmitx_error;
  }

  return I2C_RETURN_CODE_success;

 transmitx_error:
  SendStopCondition();
  ReadSDA();
  Delay();
  start = 0;
  return ret;

#endif
}

int FtdiDriver::i2c_ReceiveX
( int sendStartCondition,
  int sendStopCondition,
  int slaveAddress,
  uint8_t *buf,
  uint16_t bytesToXfer,
  uint16_t &bytesXfered )
{
#if !USE_I2C_BITBANG

  FT_STATUS s;

  s = SetMode(MODE_i2c);
  if (s!=FT_OK)
    return I2C_RETURN_CODE_fail;

  s = FT4222_I2CMaster_ReadEx
    (spi_ft_handle,
     slaveAddress,
     (sendStartCondition ? Repeated_START : 0) | (sendStopCondition ? STOP : 0),
     buf,
     bytesToXfer,
     &bytesXfered);
  if (s!=FT_OK)
    return I2C_RETURN_CODE_fail;

  return I2C_RETURN_CODE_success;

#else

  int ret;
  int b, bit, nack, sendAcknowledgeBit;

  if (sendStartCondition) {
    ret = SendStartCondition();
    if (ret != I2C_RETURN_CODE_success)
      goto receivex_error;
  }

  // send slave address and read flag
  {
    uint8_t byteToSend = (slaveAddress<<1) | 1;
    for (bit = 0; bit < 8; bit++) {
      ret = WriteBit((byteToSend & 0x80) != 0);
      if (ret != I2C_RETURN_CODE_success)
        goto receivex_error;
      byteToSend <<= 1;
    }

    ret = ReadBit(&nack);
    if (ret != I2C_RETURN_CODE_success)
      goto receivex_error;
  }

  // receive packet
  if (!nack) {
    for(bytesXfered = 0; bytesXfered < bytesToXfer; ) {

      uint8_t byte = 0;
      for (bit = 0; bit < 8; bit++) {
        byte <<= 1;
        ret = ReadBit(&b);
        if (ret != I2C_RETURN_CODE_success)
          goto receivex_error;
        if (b)
          byte |= 1;
      }

      buf[bytesXfered++] = byte;
      sendAcknowledgeBit = bytesXfered < bytesToXfer;

      ret = WriteBit(!sendAcknowledgeBit);
      if (ret != I2C_RETURN_CODE_success)
        goto receivex_error;
    }
  }

  if (sendStopCondition) {
    ret = SendStopCondition();
    if (ret != I2C_RETURN_CODE_success)
      goto receivex_error;
  }

  return I2C_RETURN_CODE_success;

 receivex_error:
  SendStopCondition();
  ReadSDA();
  Delay();
  start = 0;
  return ret;

#endif
}

const char *FtdiDriver::i2c_error(int code)
{
  if ((int)code >= (int)I2C_RETURN_CODE__last) {
    code = I2C_RETURN_CODE__last;
  }
  return i2c_error_string[(int)code];
}
