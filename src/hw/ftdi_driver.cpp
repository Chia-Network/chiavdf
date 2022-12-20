// Copyright Supranational

#include <memory>
#include <stdio.h>
#include <string.h>
#include <string>
#include <assert.h>
#include "ftdi_driver.hpp"

extern "C" {
#include "ftd2xx.h"
#include "libft4222.h"
}

void print_buf(size_t offset, uint8_t *buf, size_t size) {
  uint32_t *buf32 = (uint32_t *)buf;
  for (unsigned i = 0; i < size / (32/8); i++) {
    printf("%08zu: %08x\n", offset + i * (32/8), buf32[i]);
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
    printf("%s failed (error %d)\n", #x, (int)stat);   \
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
  // Close();
  // delete [] int_write_buf;
  // delete [] int_read_buf;
  // int_write_buf = NULL;
  // int_read_buf  = NULL;
}

bool FtdiDriver::IsOpen() {
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
    printf("No FTDI devices connected.\n");
    return -1;
  }

  // Allocate storage
  FT_DEVICE_LIST_INFO_NODE *dev_info =
    (FT_DEVICE_LIST_INFO_NODE *)malloc((size_t)num_devs *
                                       sizeof(FT_DEVICE_LIST_INFO_NODE));
    
  // Populate the list of info nodes
  if (FT_GetDeviceInfoList(dev_info, &num_devs) != FT4222_OK) {
    printf("FT_GetDeviceInfoList failed\n");
    free(dev_info);
    return -1;
  }

  for (unsigned i = 0; i < num_devs; i++) {
    if (dev_info[i].Type == FT_DEVICE_4222H_0  ||
        dev_info[i].Type == FT_DEVICE_4222H_1_2) {
      // In mode 0, the FT4222H presents two interfaces: A and B.
      // In modes 1 and 2, it presents four interfaces: A, B, C and D.
      size_t descLen = strlen(dev_info[i].Description);
      if ('A' == dev_info[i].Description[descLen - 1]) {
        printf("Device %d: '%s', loc %d\n", i,
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
    printf("No FTDI devices connected.\n");
    return -1;
  }

  // Allocate storage
  FT_DEVICE_LIST_INFO_NODE *dev_info =
    (FT_DEVICE_LIST_INFO_NODE *)malloc((size_t)num_devs *
                                       sizeof(FT_DEVICE_LIST_INFO_NODE));
    
  // Populate the list of info nodes
  if (FT_GetDeviceInfoList(dev_info, &num_devs) != FT4222_OK) {
    printf("FT_GetDeviceInfoList failed\n");
    free(dev_info);
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
    }
  }
  
  free(dev_info);
  
  if (!found) {
    printf("No FT4222H detected.\n");
    return -1;
  }
  
  return Open(device_id, sys_clk, clk_div);
}

int FtdiDriver::Open(DWORD loc_id, unsigned sys_clk, unsigned clk_div) {
  Close();

  // printf("Opening device %d\n", loc_id);
  spi_loc_id = loc_id;
  gpio_loc_id = loc_id + 3;

  CHECK(FT_OpenEx((PVOID)(uintptr_t)spi_loc_id, FT_OPEN_BY_LOCATION,
                  &spi_ft_handle));
  CHECK(FT_OpenEx((PVOID)(uintptr_t)gpio_loc_id, FT_OPEN_BY_LOCATION,
                  &gpio_ft_handle));

  // Init SPI, selection subordinate 0
  CHECK(FT4222_SPIMaster_Init(spi_ft_handle, SPI_IO_QUAD,
                              (FT4222_SPIClock)clk_div,
                              CLK_IDLE_LOW, CLK_LEADING, 0x01));
  CHECK(FT4222_SPIMaster_SetCS(spi_ft_handle, CS_ACTIVE_NEGTIVE));
  CHECK(FT4222_SetClock(spi_ft_handle, (FT4222_ClockRate)sys_clk));
  CHECK(FT_SetUSBParameters(spi_ft_handle, 2048+16, 2048+16));


  CHECK(FT4222_SPI_SetDrivingStrength(spi_ft_handle,
                                      //DS_8MA, DS_8MA, DS_8MA));
                                      //DS_4MA, DS_4MA, DS_8MA));
                                      DS_4MA, DS_4MA, DS_12MA));
    
  // Init GPIO
  GPIO_Dir gpio_dir[4];
  gpio_dir[0] = GPIO_OUTPUT;
  gpio_dir[1] = GPIO_OUTPUT;
  gpio_dir[2] = GPIO_OUTPUT;
  gpio_dir[3] = GPIO_OUTPUT;
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
    CHECK(FT4222_UnInitialize(spi_ft_handle));
    CHECK(FT_Close(spi_ft_handle));
    spi_ft_handle = NULL;
  }
  if (gpio_ft_handle != NULL) {
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
        printf("Invalid clk %s\n", argv[i]);
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
        printf("Invalid clk div %s\n", argv[i]);
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
    printf("Unexpected size %zu\n", size);
    exit(1);
  }
}

int FtdiDriver::WriteRaw(uint8_t *write_buf, size_t size) {
  assert(IsOpen());
  // printf("WriteRaw\n");
  // print_buf(0x0, write_buf, size);
  
  uint32_t size_of_read;
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
  CHECK(FT4222_SPIMaster_MultiReadWrite(spi_ft_handle, read_buf, write_buf,
                                        0,     // singleWriteBytes
                                        wsize, // multiWriteBytes
                                        rsize, // multiReadBytes
                                        &size_of_read));
  if (size_of_read != rsize) {
    printf("Size of read (%d) does not match expected (%zu)\n",
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
    printf("Unexpected wsize %zu, padded_size %zu\n", wsize, padded_size);
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
  // printf("ReadCmd(%x, %p, %zu)\n", addr, read_buf, rsize);
  assert(IsOpen());
  size_t padded_size = rsize;
  if (!round_size_up(padded_size)) {
    printf("Unexpected read rsize %zu\n", rsize);
    return -1;
  }

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

int FtdiDriver::SetGPIO(int port, bool value) {
  assert(IsOpen());
  assert(port == GPIO_PORT2 || port == GPIO_PORT3);
  CHECK(FT4222_GPIO_Write(gpio_ft_handle, (GPIO_Port)port, value));
  return 0;
}

int FtdiDriver::GetGPIO(int port, bool &value) {
  assert(IsOpen());
  assert(port == GPIO_PORT2 || port == GPIO_PORT3);
  BOOL int_val;
  CHECK(FT4222_GPIO_Read(gpio_ft_handle, (GPIO_Port)port, &int_val));
  value = int_val > 0;
  return 0;
}

void FtdiDriver::ToggleGPIO(int port) {
  bool val;
  GetGPIO(port, val);
  SetGPIO(port, !val);
}
