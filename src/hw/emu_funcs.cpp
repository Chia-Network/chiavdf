#include "emu_runner.hpp"
#include "emu_ftdi_test.hpp"

//#include "verifier.h"
//#include "bit_manipulation.h"
//#include "alloc.hpp"
//#include "double_utility.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "libft4222.h"

#ifdef _WIN32
// Ensure local emulator function definitions are emitted without dllimport.
#ifdef FTD2XX_API
#undef FTD2XX_API
#define FTD2XX_API
#endif
#ifdef LIBFT4222_API
#undef LIBFT4222_API
#define LIBFT4222_API
#endif
#endif

#if defined(_WIN32) && defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winconsistent-dllimport"
#endif

int chia_vdf_is_emu = 1;

#define EMU_LOC_ID 0x88888
#define SPI_FT_BASE kEmuSpiHandle
#define GPIO_FT_BASE kEmuGpioHandle

EmuFtdiTestState chia_vdf_emu_ftdi;

static bool fail_call(EmuFtdiFailure call) {
    return chia_vdf_emu_ftdi.fail_call == call;
}

static FT4222_STATUS configure(EmuFtdiFailure name) {
    ++chia_vdf_emu_ftdi.configuration_calls;
    return fail_call(name) ? FT4222_IO_ERROR : FT4222_OK;
}

FTD2XX_API FT_STATUS WINAPI FT_CreateDeviceInfoList(LPDWORD lpdwNumDevs)
{
    *lpdwNumDevs = 1;
    return 0;
}

FTD2XX_API FT_STATUS WINAPI FT_GetDeviceInfoList(
        FT_DEVICE_LIST_INFO_NODE *pDest, LPDWORD lpdwNumDevs)
{
    *lpdwNumDevs = 1;

    pDest->Type = chia_vdf_emu_ftdi.mode0 ? FT_DEVICE_4222H_0 : FT_DEVICE_4222H_1_2;
#ifdef _WIN32
    strcpy_s(pDest->Description, sizeof(pDest->Description), "A");
#else
    snprintf(pDest->Description, sizeof(pDest->Description), "%s", "A");
#endif
    pDest->LocId = EMU_LOC_ID;
    snprintf(pDest->SerialNumber, sizeof(pDest->SerialNumber), "%s", chia_vdf_emu_ftdi.spi_serial.c_str());

    return 0;
}

FTD2XX_API FT_STATUS WINAPI FT_OpenEx(PVOID pArg1, DWORD Flags, FT_HANDLE *pHandle)
{
    auto &state = chia_vdf_emu_ftdi;
    const auto location = reinterpret_cast<uintptr_t>(pArg1);
    if (location == EMU_LOC_ID) {
        if (state.spi_open) return FT_DEVICE_NOT_OPENED;
        *pHandle = (void *)SPI_FT_BASE;
        state.spi_open = true;
    } else if (((location == EMU_LOC_ID + 3 && !state.mode0 && !state.gpio_d_unavailable) ||
                (location == EMU_LOC_ID + 1 && (state.mode0 || state.mode1_b_available))) && !fail_call(EmuFtdiFailure::gpio_open)) {
        if (state.gpio_open) return FT_DEVICE_NOT_OPENED;
        *pHandle = (void *)GPIO_FT_BASE;
        state.gpio_open = true;
        state.gpio_is_b = location == EMU_LOC_ID + 1;
    } else {
        return FT_DEVICE_NOT_FOUND;
    }
    ++state.opens;
    return FT_OK;
}

FTD2XX_API FT_STATUS WINAPI FT_GetDeviceInfo(FT_HANDLE handle, FT_DEVICE *type,
        LPDWORD id, PCHAR serial, PCHAR description, LPVOID) {
    const auto &state = chia_vdf_emu_ftdi;
    const bool spi = handle == (void *)SPI_FT_BASE;
    if ((spi && (!state.spi_open || state.spi_info_error)) ||
        (!spi && (handle != (void *)GPIO_FT_BASE || !state.gpio_open || state.gpio_info_error)))
        return FT_INVALID_HANDLE;
    *type = state.mode0 ? FT_DEVICE_4222H_0 : FT_DEVICE_4222H_1_2;
    *id = 0x0403601c;
    const std::string &value = spi ? state.spi_serial : state.gpio_serial;
    const char *text = !spi && value.empty() ? (state.gpio_is_b ? "EMULATEDB" : "EMULATEDD") : value.c_str();
    // A 16-byte value deliberately exercises the non-terminated D2XX buffer case.
    strncpy(serial, text, 16);
    snprintf(description, 64, "%s", "Chia VDF emulator");
    return FT_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SPIMaster_Init(FT_HANDLE ftHandle, FT4222_SPIMode  ioLine, FT4222_SPIClock clock, FT4222_SPICPOL  cpol, FT4222_SPICPHA  cpha, uint8 ssoMap)
{
    return configure(EmuFtdiFailure::FT4222_SPIMaster_Init);
}

LIBFT4222_API FT4222_STATUS FT4222_SPIMaster_SetCS(FT_HANDLE ftHandle, SPI_ChipSelect cs)
{
    return configure(EmuFtdiFailure::FT4222_SPIMaster_SetCS);
}

LIBFT4222_API FT4222_STATUS FT4222_SetClock(FT_HANDLE ftHandle, FT4222_ClockRate clk)
{
    return configure(EmuFtdiFailure::FT4222_SetClock);
}

FTD2XX_API FT_STATUS WINAPI FT_SetUSBParameters(FT_HANDLE ftHandle, ULONG ulInTransferSize, ULONG ulOutTransferSize)
{
    ++chia_vdf_emu_ftdi.configuration_calls;
    return fail_call(EmuFtdiFailure::FT_SetUSBParameters) ? FT_IO_ERROR : FT_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SPI_SetDrivingStrength(FT_HANDLE ftHandle, SPI_DrivingStrength clkStrength, SPI_DrivingStrength ioStrength, SPI_DrivingStrength ssoStrength)
{
    return configure(EmuFtdiFailure::FT4222_SPI_SetDrivingStrength);
}

LIBFT4222_API FT4222_STATUS FT4222_GPIO_Init(FT_HANDLE ftHandle, GPIO_Dir gpioDir[4])
{
    return configure(EmuFtdiFailure::FT4222_GPIO_Init);
}

LIBFT4222_API FT4222_STATUS FT4222_SetSuspendOut(FT_HANDLE ftHandle, BOOL enable)
{
    return configure(EmuFtdiFailure::FT4222_SetSuspendOut);
}

LIBFT4222_API FT4222_STATUS FT4222_SetWakeUpInterrupt(FT_HANDLE ftHandle, BOOL enable)
{
    return configure(EmuFtdiFailure::FT4222_SetWakeUpInterrupt);
}

LIBFT4222_API FT4222_STATUS FT4222_UnInitialize(FT_HANDLE ftHandle)
{
    ++chia_vdf_emu_ftdi.uninitializes;
    return fail_call(EmuFtdiFailure::FT4222_UnInitialize) ? FT4222_IO_ERROR : FT4222_OK;
}

FTD2XX_API FT_STATUS WINAPI FT_Close(FT_HANDLE ftHandle)
{
    auto &state = chia_vdf_emu_ftdi;
    bool *open = ftHandle == (void *)SPI_FT_BASE ? &state.spi_open :
                 ftHandle == (void *)GPIO_FT_BASE ? &state.gpio_open : nullptr;
    if (open == nullptr || !*open) return FT_INVALID_HANDLE;
    if (fail_call(ftHandle == (void *)SPI_FT_BASE ? EmuFtdiFailure::spi_close : EmuFtdiFailure::gpio_close))
        return FT_IO_ERROR;
    *open = false;
    ++state.closes;
    return FT_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SPIMaster_MultiReadWrite(FT_HANDLE ftHandle, uint8* readBuffer, uint8* writeBuffer, uint8 singleWriteBytes, uint16 multiWriteBytes, uint16 multiReadBytes, uint32* sizeOfRead)
{
    int ret = emu_do_io(writeBuffer, multiWriteBytes, readBuffer, multiReadBytes);
    if (!ret) {
        *sizeOfRead = multiReadBytes;
        return FT4222_OK;
    } else {
        return FT4222_IO_ERROR;
    }
}

LIBFT4222_API FT4222_STATUS FT4222_GPIO_Write(FT_HANDLE ftHandle, GPIO_Port portNum, BOOL bValue)
{
    // GPIO2 drives the active-low board reset, VDF_RST_N.
    if (portNum == GPIO_PORT2 && !bValue) emu_reset_board();
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_GPIO_Read(FT_HANDLE ftHandle, GPIO_Port portNum, BOOL* value)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_I2CMaster_Init(FT_HANDLE ftHandle, uint32 kbps)
{
    return configure(EmuFtdiFailure::FT4222_I2CMaster_Init);
}

LIBFT4222_API FT4222_STATUS FT4222_I2CMaster_ReadEx(FT_HANDLE ftHandle, uint16 deviceAddress, uint8 flag, uint8* buffer, uint16 bufferSize, uint16* sizeTransferred)
{
    int ret = emu_do_io_i2c(buffer, bufferSize, deviceAddress, 1);
    if (!ret) {
        *sizeTransferred = bufferSize;
        return FT4222_OK;
    } else {
        return FT4222_IO_ERROR;
    }
}

LIBFT4222_API FT4222_STATUS FT4222_I2CMaster_WriteEx(FT_HANDLE ftHandle, uint16 deviceAddress, uint8 flag, uint8* buffer, uint16 bufferSize, uint16* sizeTransferred)
{
    int ret = emu_do_io_i2c(buffer, bufferSize, deviceAddress, 0);
    if (!ret) {
        *sizeTransferred = bufferSize;
        return FT4222_OK;
    } else {
        return FT4222_IO_ERROR;
    }
}

#if defined(_WIN32) && defined(__clang__)
#pragma clang diagnostic pop
#endif
