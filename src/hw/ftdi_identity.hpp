#ifndef CHIAVDF_FTDI_IDENTITY_HPP
#define CHIAVDF_FTDI_IDENTITY_HPP

#include "ftdi_driver.hpp"
#include <cstring>
#include <string>

namespace ftdi_identity {

struct Result {
  const char *error = nullptr;
  FT_STATUS status = FT_OK;
  std::string serial;
  std::string base;
};

// FT4222 multi-interface serials must be a nonempty board base followed by
// the interface letter. Fail closed on other formats rather than guessing.
// Read the opened handle: enumeration can precede unplug/location reuse.
inline Result confirm(FT_HANDLE handle, char suffix, const std::string &expected,
                      bool allow_full_serial = false) {
  Result result;
  FT_DEVICE type;
  DWORD id;
  char serial[16] = {}, description[64] = {};
  result.status = FT_GetDeviceInfo(handle, &type, &id, serial, description, nullptr);
  if (result.status != FT_OK) {
    result.error = "unreadable";
    return result;
  }
  const auto end = static_cast<const char *>(std::memchr(serial, 0, sizeof(serial)));
  if (end == nullptr || end - serial < 2 || end[-1] != suffix) {
    result.error = "invalid interface serial format";
    return result;
  }
  for (const char *c = serial; c != end; ++c) {
    if (static_cast<unsigned char>(*c) < 33 || static_cast<unsigned char>(*c) > 126) {
      result.error = "invalid interface serial format";
      return result;
    }
  }
  result.serial.assign(serial, static_cast<size_t>(end - serial));
  result.base.assign(serial, static_cast<size_t>(end - serial - 1));
  if (result.base != expected && !(allow_full_serial && result.serial == expected))
    result.error = "mismatch";
  return result;
}

} // namespace ftdi_identity
#endif
