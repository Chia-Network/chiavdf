#include "emu_runner.hpp"
#include "libft4222.h"

//#include "verifier.h"
//#include "bit_manipulation.h"
//#include "alloc.hpp"
//#include "double_utility.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

int chia_vdf_is_emu = 1;

#define EMU_LOC_ID 0x88888
#define SPI_FT_BASE 0x100
#define GPIO_FT_BASE 0x200

FTD2XX_API FT_STATUS WINAPI FT_CreateDeviceInfoList(LPDWORD lpdwNumDevs)
{
    *lpdwNumDevs = 1;
    return 0;
}

FTD2XX_API FT_STATUS WINAPI FT_GetDeviceInfoList(
        FT_DEVICE_LIST_INFO_NODE *pDest, LPDWORD lpdwNumDevs)
{
    *lpdwNumDevs = 1;

    pDest->Type = FT_DEVICE_4222H_0;
    strcpy(pDest->Description, "A");
    pDest->LocId = EMU_LOC_ID;

    return 0;
}

FTD2XX_API FT_STATUS WINAPI FT_OpenEx(PVOID pArg1, DWORD Flags, FT_HANDLE *pHandle)
{
    if ((uintptr_t)pArg1 == EMU_LOC_ID) {
        *pHandle = (void *)SPI_FT_BASE;
    } else if ((uintptr_t)pArg1 == EMU_LOC_ID + 3) {
        *pHandle = (void *)GPIO_FT_BASE;
    } else {
        return FT4222_DEVICE_NOT_FOUND;
    }

    return 0;
}

LIBFT4222_API FT4222_STATUS FT4222_SPIMaster_Init(FT_HANDLE ftHandle, FT4222_SPIMode  ioLine, FT4222_SPIClock clock, FT4222_SPICPOL  cpol, FT4222_SPICPHA  cpha, uint8 ssoMap)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SPIMaster_SetCS(FT_HANDLE ftHandle, SPI_ChipSelect cs)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SetClock(FT_HANDLE ftHandle, FT4222_ClockRate clk)
{
    return FT4222_OK;
}

FTD2XX_API FT_STATUS WINAPI FT_SetUSBParameters(FT_HANDLE ftHandle, ULONG ulInTransferSize, ULONG ulOutTransferSize)
{
    return 0;
}

LIBFT4222_API FT4222_STATUS FT4222_SPI_SetDrivingStrength(FT_HANDLE ftHandle, SPI_DrivingStrength clkStrength, SPI_DrivingStrength ioStrength, SPI_DrivingStrength ssoStrength)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_GPIO_Init(FT_HANDLE ftHandle, GPIO_Dir gpioDir[4])
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SetSuspendOut(FT_HANDLE ftHandle, BOOL enable)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_SetWakeUpInterrupt(FT_HANDLE ftHandle, BOOL enable)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_UnInitialize(FT_HANDLE ftHandle)
{
    return FT4222_OK;
}

FTD2XX_API FT_STATUS WINAPI FT_Close(FT_HANDLE ftHandle)
{
    return 0;
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
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_GPIO_Read(FT_HANDLE ftHandle, GPIO_Port portNum, BOOL* value)
{
    return FT4222_OK;
}

LIBFT4222_API FT4222_STATUS FT4222_I2CMaster_Init(FT_HANDLE ftHandle, uint32 kbps)
{
    return FT4222_OK;
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
