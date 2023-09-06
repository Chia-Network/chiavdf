#include <memory>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <string>
#include <assert.h>
#include <time.h>

#include <chrono>
typedef std::chrono::high_resolution_clock Clock;

#include "ftdi_driver.hpp"

void print_buf(size_t offset, uint8_t *buf, size_t size) {
  uint32_t *buf32 = (uint32_t *)buf;
  for (unsigned i = 0; i < size / (32/8); i++) {
    fprintf(stderr, "%08lx: %08x\n", offset + i * (32/8), buf32[i]);
  }
}

#define SPI_TRANSFER_RW_DATA         0
#define SPI_TRANSFER_READ_REQUEST    1
#define SPI_TRANSFER_STATUS          2
#define SPI_TRANSFER_READ_WITH_DUMMY 3

#define SPI_WAIT_8                   0
#define SPI_WAIT_16                  1
#define SPI_WAIT_24                  2
#define SPI_WAIT_32                  3

#define SPI_ADDR_WIDTH               3 // Bytes

struct spi_cmd {
  union {
    struct {
      unsigned data_length   : 3;
      unsigned wait_cycles   : 2;
      unsigned transfer_type : 2;
      unsigned write         : 1;
    };
    uint8_t u;
  };
};

#define CYCLES_PER_BYTE 2
#define WAIT_CYCLES     ((SPI_WAIT_8 + 1) * (8 / CYCLES_PER_BYTE)) // For READ8
#define CMD_BYTES       4
#define MAX_WRITE_BUF   2048 + CMD_BYTES
#define MAX_READ_BUF    2048 + WAIT_CYCLES

#define CHECK(x) { \
  FT4222_STATUS stat = (FT4222_STATUS)x;               \
  if (FT4222_OK != stat) {                             \
    eprintf("%s failed (error %d)", #x, (int)stat);    \
    return stat;                                       \
  }                                                    \
}

FtdiDriver::FtdiDriver() {
  spi_ft_handle = NULL;
  gpio_ft_handle = NULL;
  int_write_buf = new uint8_t[MAX_WRITE_BUF];
  int_read_buf  = new uint8_t[MAX_READ_BUF];
}

FtdiDriver::~FtdiDriver() {
  // TODO: Handle deletion from multiple references
  Close();
  delete [] int_write_buf;
  delete [] int_read_buf;
  int_write_buf = NULL;
  int_read_buf  = NULL;
}

void FtdiDriver::eprintf(const char * format, ...) {
  va_list argptr;
  va_start(argptr, format);

#ifdef USE_THROW
  char buffer[1024];
  vsnprintf(buffer, sizeof(buffer), format, argptr);
  va_end(argptr);
  throw(buffer);
#else
  vfprintf(stderr, format, argptr);
  fprintf(stderr, "\n");
  va_end(argptr);
#endif
}

bool FtdiDriver::IsOpen() {
  //printf("spi_ft_handle %x\n", spi_ft_handle);
  //printf("gpio_ft_handle %x\n", gpio_ft_handle);
  return spi_ft_handle != NULL && gpio_ft_handle != NULL;
}

inline std::string DeviceFlagToString(DWORD flags) {
  std::string msg;
  msg += (flags & 0x1)? "DEVICE_OPEN" : "DEVICE_CLOSED";
  msg += ", ";
  msg += (flags & 0x2)? "High-speed USB" : "Full-speed USB";
  return msg;
}

int FtdiDriver::List() {
  DWORD     num_devs = 0;

  CHECK(FT_CreateDeviceInfoList(&num_devs));

  if (num_devs == 0) {
    eprintf("No FTDI devices connected.");
    return -1;
  }

  // Allocate storage
  FT_DEVICE_LIST_INFO_NODE *dev_info =
    (FT_DEVICE_LIST_INFO_NODE *)malloc((size_t)num_devs *
                                       sizeof(FT_DEVICE_LIST_INFO_NODE));

  // Populate the list of info nodes
  if (FT_GetDeviceInfoList(dev_info, &num_devs) != FT4222_OK) {
    free(dev_info);
    eprintf("FT_GetDeviceInfoList failed");
    return -1;
  }

  for (unsigned i = 0; i < num_devs; i++) {
    if (dev_info[i].Type == FT_DEVICE_4222H_0  ||
        dev_info[i].Type == FT_DEVICE_4222H_1_2) {
      // In mode 0, the FT4222H presents two interfaces: A and B.
      // In modes 1 and 2, it presents four interfaces: A, B, C and D.
      size_t descLen = strlen(dev_info[i].Description);
      // fprintf(stderr, "Device %d: '%s', loc %d\n", i,
      //        dev_info[i].Description, dev_info[i].LocId);
      if ('A' == dev_info[i].Description[descLen - 1]) {
        fprintf(stderr, "Device %d: '%s', loc %d\n", i,
               dev_info[i].Description, dev_info[i].LocId);
      }
    }
  }

  free(dev_info);
  return 0;
}

int FtdiDriver::Open() {
  return OpenClk((int)SYS_CLK_80, (int)CLK_DIV_8);
}

int FtdiDriver::OpenClk(unsigned sys_clk, unsigned clk_div, DWORD target_id) {
  DWORD     num_devs = 0;

  CHECK(FT_CreateDeviceInfoList(&num_devs));

  if (num_devs == 0) {
    eprintf("No FTDI devices connected.");
    return -1;
  }

  // Allocate storage
  FT_DEVICE_LIST_INFO_NODE *dev_info =
    (FT_DEVICE_LIST_INFO_NODE *)malloc((size_t)num_devs *
                                       sizeof(FT_DEVICE_LIST_INFO_NODE));

  // Populate the list of info nodes
  if (FT_GetDeviceInfoList(dev_info, &num_devs) != FT4222_OK) {
    free(dev_info);
    eprintf("FT_GetDeviceInfoList failed");
    return -1;
  }

  bool found = false;
  unsigned device_id = 0;
  for (unsigned i = 0; i < num_devs; i++) {
    if (dev_info[i].Type == FT_DEVICE_4222H_0  ||
        dev_info[i].Type == FT_DEVICE_4222H_1_2) {
      // In mode 0, the FT4222H presents two interfaces: A and B.
      // In modes 1 and 2, it presents four interfaces: A, B, C and D.
      size_t descLen = strlen(dev_info[i].Description);
      if ('A' == dev_info[i].Description[descLen - 1]) {
        if ((target_id != 0 && dev_info[i].LocId == target_id) ||
            target_id == 0) {
          device_id = dev_info[i].LocId;
          found = true;
          //print_buf(0x0, (uint8_t *)dev_info[i].SerialNumber, 16);
          break;
        }
      }
      //printf("Dev %d:\n",i);
      //printf(" Flags=0x%x\n",dev_info[i].Flags);
      //printf(" Type=0x%x\n",dev_info[i].Type);
      //printf(" ID=0x%x\n",dev_info[i].ID);
      //printf(" LocId=0x%x\n",dev_info[i].LocId);
      //printf(" SerialNumber=%s\n",dev_info[i].SerialNumber);
      //printf(" Description=%s\n",dev_info[i].Description);
    }
  }

  free(dev_info);

  if (!found) {
    eprintf("No FT4222H detected.");
    return -1;
  }

  return Open(device_id, sys_clk, clk_div);
}

int FtdiDriver::SetMode(FtdiDriver::MODE_t mode)
{
  //printf("SetMode mode %d\n", mode);
  if (mode == active_mode) {
    return 0;
  }

  if (active_mode != MODE_none) {
    CHECK(FT4222_UnInitialize(spi_ft_handle));
    //printf("SetMode FT4222_UnInitialize\n");
    active_mode = MODE_none;
  }

  switch(mode) {

  case MODE_spi:
    //printf("SetMode(MODE_spi);\n");
    CHECK(FT4222_SPIMaster_Init(spi_ft_handle, SPI_IO_QUAD, spi_clk_div,
                                CLK_IDLE_LOW, CLK_LEADING, 0x01));
    // The SPI_ChipSelect value is named either CS_ACTIVE_NEGTIVE or
    // CS_ACTIVE_LOW depending on libftd2xx version, but it's 0 in any case
    CHECK(FT4222_SPIMaster_SetCS(spi_ft_handle, (SPI_ChipSelect)0));
    CHECK(FT4222_SPI_SetDrivingStrength(spi_ft_handle,
                                        // clk, io, sso
                                        //DS_8MA, DS_8MA, DS_8MA));
                                        //DS_4MA, DS_4MA, DS_8MA));
                                        DS_4MA, DS_4MA, DS_4MA));
    active_mode = mode;
    break;

  case MODE_i2c:
    //printf("SetMode(MODE_i2c);\n");
    CHECK(FT4222_I2CMaster_Init(spi_ft_handle, 100 /*kHz*/));
    active_mode = mode;
    break;

  default:
    active_mode = MODE_none;
  }

  return 0;
}


int FtdiDriver::Open(DWORD loc_id, unsigned sys_clk, unsigned clk_div) {
  Close();

  // fprintf(stderr, "Opening device %d\n", loc_id);
  spi_loc_id = loc_id;

  CHECK(FT_OpenEx((PVOID)(uintptr_t)spi_loc_id, FT_OPEN_BY_LOCATION,
                  &spi_ft_handle));

  // originally we set jumpers for CNFMODE1, which uses 4 USB interfaces with
  // SPIM on "A,B,C" and GPIO on "D" (ie +3).  But it may be convenient to get
  // more GPs by using CNFMODE0 which uses 2 USB interfaces with SPIM on "A"
  // and GPIO on "B" (ie +1).  Try to open GP on both.
  gpio_loc_id = loc_id + 3;
  cnfmode = 1;
  if (FT_OpenEx((PVOID)(uintptr_t)gpio_loc_id, FT_OPEN_BY_LOCATION,
                &gpio_ft_handle)) {
    gpio_loc_id = loc_id + 1;
    cnfmode = 0;
    CHECK(FT_OpenEx((PVOID)(uintptr_t)gpio_loc_id, FT_OPEN_BY_LOCATION,
                    &gpio_ft_handle));
  }

  CHECK(FT4222_SetClock(spi_ft_handle, (FT4222_ClockRate)sys_clk));
  CHECK(FT_SetUSBParameters(spi_ft_handle, 2048+16, 2048+16));

  // save clock divide SPI
  spi_clk_div = (FT4222_SPIClock)clk_div;

  // start with SPI mode
  CHECK(SetMode(MODE_spi));

  // Init GPIO
  gpio_dir[0] = GPIO_INPUT;
  gpio_dir[1] = GPIO_INPUT;
  gpio_dir[2] = GPIO_INPUT;
  gpio_dir[3] = GPIO_INPUT;
  CHECK(FT4222_GPIO_Init(gpio_ft_handle, gpio_dir));

  // Set up GPIOs
  // Disable suspend out, enable gpio 2
  CHECK(FT4222_SetSuspendOut(gpio_ft_handle, 0));
  // Disable interrupt, enable gpio 3
  CHECK(FT4222_SetWakeUpInterrupt(gpio_ft_handle, 0));

  return 0;
}

int FtdiDriver::Close() {
  if (spi_ft_handle != NULL) {
    //printf("FtdiDriver::Close\n");
    CHECK(FT4222_UnInitialize(spi_ft_handle));
    CHECK(FT_Close(spi_ft_handle));
    spi_ft_handle = NULL;
  }
  if (gpio_ft_handle != NULL) {
    //printf("FtdiDriver::Close\n");
    CHECK(FT4222_UnInitialize(gpio_ft_handle));
    CHECK(FT_Close(gpio_ft_handle));
    gpio_ft_handle = NULL;
  }
  return 0;
}

void FtdiDriver::ParseParams(unsigned &sys_clk, unsigned &clk_div,
                             int argc, char *argv[]) {
  for (int i = 0; i < argc; i++) {
    if (strcmp(argv[i], "-clk") == 0) {
      i++;
      if (strcmp(argv[i], "60") == 0) {
        sys_clk = SYS_CLK_60;
      } else if (strcmp(argv[i], "24") == 0) {
        sys_clk = SYS_CLK_24;
      } else if (strcmp(argv[i], "48") == 0) {
        sys_clk = SYS_CLK_48;
      } else if (strcmp(argv[i], "80") == 0) {
        sys_clk = SYS_CLK_80;
      } else {
        fprintf(stderr, "Invalid clk %s\n", argv[i]);
        exit(1);
      }
    } else if (strcmp(argv[i], "-div") == 0) {
      i++;
      if (strcmp(argv[i], "2") == 0) {
        clk_div = CLK_DIV_2;
      } else if (strcmp(argv[i], "4") == 0) {
        clk_div = CLK_DIV_4;
      } else if (strcmp(argv[i], "8") == 0) {
        clk_div = CLK_DIV_8;
      } else if (strcmp(argv[i], "16") == 0) {
        clk_div = CLK_DIV_16;
      } else if (strcmp(argv[i], "32") == 0) {
        clk_div = CLK_DIV_32;
      } else if (strcmp(argv[i], "64") == 0) {
        clk_div = CLK_DIV_64;
      } else if (strcmp(argv[i], "128") == 0) {
        clk_div = CLK_DIV_128;
      } else if (strcmp(argv[i], "256") == 0) {
        clk_div = CLK_DIV_256;
      } else if (strcmp(argv[i], "512") == 0) {
        clk_div = CLK_DIV_512;
      } else {
        fprintf(stderr, "Invalid clk div %s\n", argv[i]);
        exit(1);
      }
    }
  }
}

// static bool is_size_valid(size_t size) {
//   return (size == 4 ||
//           size == 16 ||
//           size == 32 ||
//           size == 64 ||
//           size == 128 ||
//           size == 256);
// }

static bool round_size_up(size_t &size) {
  if (size <= 4) {
    size = 4;
  } else if (size <= 16) {
    size = 16;
  } else if (size <= 32) {
    size = 32;
  } else if (size <= 64) {
    size = 64;
  } else if (size <= 128) {
    size = 128;
  } else if (size <= 256) {
    size = 256;
  } else if (size > 256) {
    return false;
  }
  return true;
}

static int encode_size(size_t size) {
  switch(size) {
  case 4:
    return 0;
  case 16:
    return 1;
  case 32:
    return 2;
  case 64:
    return 3;
  case 128:
    return 4;
  case 256:
    return 5;
  default:
    fprintf(stderr, "Unexpected size %zu\n", size);
    exit(1);
  }
}

int FtdiDriver::WriteRaw(uint8_t *write_buf, size_t size) {
  assert(IsOpen());
  // fprintf(stderr, "WriteRaw\n");
  // print_buf(0x0, write_buf, size);

  uint32_t size_of_read;
  CHECK(SetMode(MODE_spi));
  CHECK(FT4222_SPIMaster_MultiReadWrite(spi_ft_handle, NULL, write_buf,
                                        0,    // singleWriteBytes
                                        size, // multiWriteBytes
                                        0,    // multiReadBytes
                                        &size_of_read));
  return 0;
}

int FtdiDriver::ReadRaw(uint8_t *write_buf, size_t wsize,
                        uint8_t *read_buf, size_t rsize) {
  assert(IsOpen());

  uint32_t size_of_read;
  CHECK(SetMode(MODE_spi));
  CHECK(FT4222_SPIMaster_MultiReadWrite(spi_ft_handle, read_buf, write_buf,
                                        0,     // singleWriteBytes
                                        wsize, // multiWriteBytes
                                        rsize, // multiReadBytes
                                        &size_of_read));
  if (size_of_read != rsize) {
    eprintf("Size of read (%d) does not match expected (%zu)",
            size_of_read, rsize);
    return -1;
  }

  return 0;
}

int FtdiDriver::Write(uint32_t addr, uint8_t *write_buf, size_t wsize) {
  const size_t TARGET_WRITE_SIZE = 256;
  while (wsize > 0) {
    if (wsize >= TARGET_WRITE_SIZE) {
      int ret;
      if ((ret = WriteCmd(addr, write_buf, TARGET_WRITE_SIZE)) != 0) {
        return ret;
      }
      addr += TARGET_WRITE_SIZE;
      wsize -= TARGET_WRITE_SIZE;
      write_buf += TARGET_WRITE_SIZE;
    } else {
      // Write remaining data
      return WriteCmd(addr, write_buf, wsize);
    }
  }
  return 0;
}

int FtdiDriver::Read(uint32_t addr, uint8_t *read_buf, size_t rsize) {
  const size_t TARGET_READ_SIZE = 256;
  while (rsize > 0) {
    if (rsize >= TARGET_READ_SIZE) {
      int ret;
      if ((ret = ReadCmd(addr, read_buf, TARGET_READ_SIZE)) != 0) {
        return ret;
      }
      addr += TARGET_READ_SIZE;
      rsize -= TARGET_READ_SIZE;
      read_buf += TARGET_READ_SIZE;
    } else {
      // Read remaining data
      return ReadCmd(addr, read_buf, rsize);
    }
  }
  return 0;
}

int FtdiDriver::WriteCmd(uint32_t addr, uint8_t *write_buf, size_t wsize) {
  //printf("WriteCmd(%x, %p, %zu)\n", addr, write_buf, wsize);
  assert(IsOpen());
  size_t padded_size = wsize;
  if (!round_size_up(padded_size)) {
    eprintf("Unexpected wsize %zu, padded_size %zu", wsize, padded_size);
    return -1;
  }

  spi_cmd cmd;
  cmd.write         = 1;
  cmd.transfer_type = SPI_TRANSFER_RW_DATA;
  cmd.wait_cycles   = 0;
  cmd.data_length   = encode_size(padded_size);
  int_write_buf[0] = (uint8_t)cmd.u;

  // 4 byte addressing
  addr >>= 2;
  int_write_buf[1] = (addr >> (8 * 2)) & 0xff;
  int_write_buf[2] = (addr >> (8 * 1)) & 0xff;
  int_write_buf[3] = (addr >> (8 * 0)) & 0xff;

  // Fill in wsize bytes - padding we can ignore
  for (unsigned i = 0; i < wsize; i++) {
    int_write_buf[i + 4] = write_buf[i];
  }

  return WriteRaw(int_write_buf, padded_size + CMD_BYTES);
}

int FtdiDriver::ReadCmd(uint32_t addr, uint8_t *read_buf, size_t rsize) {
  //printf("ReadCmd(%x, %p, %zu)\n", addr, read_buf, rsize);
  assert(IsOpen());
  size_t padded_size = rsize;
  if (!round_size_up(padded_size)) {
    eprintf("Unexpected read rsize %zu", rsize);
    return -1;
  }
  //printf("ReadCmd: addr %x, size %ld, padded size %ld)\n", addr, rsize, padded_size);

  spi_cmd cmd;
  cmd.write         = 0;
  cmd.transfer_type = SPI_TRANSFER_READ_WITH_DUMMY;
  cmd.wait_cycles   = 0;
  cmd.data_length   = encode_size(padded_size);
  int_write_buf[0] = (uint8_t)cmd.u;

  // 4 byte addressing
  addr >>= 2;
  int_write_buf[1] = (addr >> (8 * 2)) & 0xff;
  int_write_buf[2] = (addr >> (8 * 1)) & 0xff;
  int_write_buf[3] = (addr >> (8 * 0)) & 0xff;

  // Clear the read buffer to ensure we don't look at stale data
  memset(int_read_buf, 0, MAX_READ_BUF);
  CHECK(ReadRaw(int_write_buf, CMD_BYTES,
                int_read_buf, padded_size + WAIT_CYCLES));

  // Fill in rsize bytes - padding we can ignore
  for (unsigned i = 0; i < rsize; i++) {
    read_buf[i] = int_read_buf[i + WAIT_CYCLES];
  }
  return 0;
}

int FtdiDriver::SetGPIO(GPIO_Port port, bool value) {
  assert(IsOpen());
  assert((cnfmode==0 &&
          (port == GPIO_PORT0 || port == GPIO_PORT1 ||
           port == GPIO_PORT2 || port == GPIO_PORT3))
         ||
         (cnfmode==1 &&
          (port == GPIO_PORT2 || port == GPIO_PORT3)));
  //FIXME: might this cause a glitch when going from tri to out?
  if (gpio_dir[port] != GPIO_OUTPUT) {
    gpio_dir[port] = GPIO_OUTPUT;
    CHECK(FT4222_GPIO_Init(gpio_ft_handle, gpio_dir));
  }
  CHECK(FT4222_GPIO_Write(gpio_ft_handle, port, value));
  return 0;
}

int FtdiDriver::GetGPIO(GPIO_Port port, bool &value) {
  assert(IsOpen());
  assert((cnfmode==0 &&
          (port == GPIO_PORT0 || port == GPIO_PORT1 ||
           port == GPIO_PORT2 || port == GPIO_PORT3))
         ||
         (cnfmode==1 &&
          (port == GPIO_PORT2 || port == GPIO_PORT3)));
  BOOL int_val;
  CHECK(FT4222_GPIO_Read(gpio_ft_handle, port, &int_val));
  value = int_val > 0;
  return 0;
}

int FtdiDriver::TriGPIO(GPIO_Port port)
{
  assert(IsOpen());
  assert((cnfmode==0 &&
          (port == GPIO_PORT0 || port == GPIO_PORT1 ||
           port == GPIO_PORT2 || port == GPIO_PORT3))
         ||
         (cnfmode==1 &&
          (port == GPIO_PORT2 || port == GPIO_PORT3)));
  if (gpio_dir[port] != GPIO_INPUT) {
    gpio_dir[port] = GPIO_INPUT;
    CHECK(FT4222_GPIO_Init(gpio_ft_handle, gpio_dir));
  }
  return 0;
}

void FtdiDriver::ToggleGPIO(GPIO_Port port) {
  bool val;
  GetGPIO(port, val);
  SetGPIO(port, !val);
}

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
