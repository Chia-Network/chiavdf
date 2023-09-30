#include <vector>
#include <cstring>
#include <cassert>
#include <cmath>
#include <unistd.h>
#include "vdf_driver.hpp"
#include "pll_freqs.hpp"

#define VR_I2C_ADDR 0x38
#define CS_I2C_ADDR 0x70

size_t VdfDriver::write_bytes(size_t size, size_t offset, uint8_t *buf,
                              uint32_t val) {
  // Insert bytes from val in big endian order
  uint8_t *p = (uint8_t *)&val;
  for (unsigned i = 0; i < REG_BYTES; i++) {
    buf[i + offset] = p[REG_BYTES - i - 1];
  }

  return size;
}

size_t VdfDriver::write_bytes(size_t size, size_t offset, uint8_t *buf,
                              uint64_t val) {
  write_bytes(REG_BYTES, offset, buf, (uint32_t)val);
  write_bytes(REG_BYTES, offset + REG_BYTES, buf, (uint32_t)(val >> REG_BITS));
  return size;
}

size_t VdfDriver::write_bytes(size_t size, size_t offset, uint8_t *buf,
                              mpz_t _val, size_t num_coeffs) {
  mpz_t tmp, val;
  mpz_inits(tmp, val, NULL);

  // Copy val so we don't alter it
  mpz_set(val, _val);

  // Two's complement signed values
  if (mpz_sgn(val) < 0) {
    unsigned bits = num_coeffs * WORD_BITS + REDUNDANT_BITS;
    mpz_t shifted;
    mpz_init(shifted);
    mpz_set_ui(shifted, 1);
    mpz_mul_2exp(shifted, shifted, bits); // left shift
    mpz_neg(val, val);
    mpz_sub(val, shifted, val);
    mpz_clear(shifted);
  }
  mpz_set(tmp, val);

  // Extract coefficients
  uint32_t word_mask = (1UL << WORD_BITS) - 1;
  std::vector<uint32_t> coeffs;
  for (size_t i = 0; i < num_coeffs - 1; i++) {
    uint32_t coeff = mpz_get_ui(tmp) & word_mask;
    coeffs.push_back(coeff);
    mpz_tdiv_q_2exp(tmp, tmp, WORD_BITS);
  }
  // Last coeff does not get masked
  coeffs.push_back(mpz_get_ui(tmp));

  // Pack coefficients, with redundant bits
  mpz_set_ui(tmp, 0);
  for (int i = num_coeffs - 1; i >= 0; i--) {
    mpz_mul_2exp(tmp, tmp, REDUNDANT_BITS); // left shift
    mpz_add_ui(tmp, tmp, coeffs[i]);
  }

  // Clear the buffer space
  memset(buf + offset, 0, size);

  // For simplicity assume size is divisible by 32 bits
  assert(size % REG_BYTES == 0);

  // Write 32 bits at a time
  uint64_t mask = (1ULL << REG_BITS) - 1;
  for (size_t count = 0; count < size; count += REG_BYTES) {
    write_bytes(REG_BYTES, offset + count, buf,
                (uint32_t)(mpz_get_ui(tmp) & mask));

    mpz_tdiv_q_2exp(tmp, tmp, REG_BITS);
  }

  mpz_clears(tmp, val, NULL);
  return size;
}

size_t VdfDriver::read_bytes(size_t size, size_t offset, uint8_t *buf,
                             uint32_t &val) {
  // Reads bytes from buf into val in big endian order
  uint8_t *p = (uint8_t *)&val;
  for (unsigned i = 0; i < REG_BYTES; i++) {
    p[REG_BYTES - i - 1] = buf[i + offset];
  }

  return size;
}

size_t VdfDriver::read_bytes(size_t size, size_t offset, uint8_t *buf,
                             uint64_t &val) {
  uint32_t val32;
  read_bytes(REG_BYTES, offset, buf, val32);
  val = val32;

  read_bytes(REG_BYTES, offset + REG_BYTES, buf, val32);
  val |= ((uint64_t)val32) << REG_BITS;

  return size;
}

size_t VdfDriver::read_bytes(size_t size, size_t offset, uint8_t *buf,
                             mpz_t val, size_t num_coeffs) {
  mpz_t tmp, tmp2;
  mpz_inits(tmp, tmp2, NULL);

  // Gather uint32s
  std::vector<uint32_t> words;
  for (size_t count = 0; count < size; count += REG_BYTES) {
    uint32_t word;
    read_bytes(REG_BYTES, offset + count, buf, word);
    words.push_back(word);
  }

  // Pack redundant coeffs, most significant first
  mpz_set_ui(tmp, 0);
  for (int i = words.size() - 1; i >= 0; i--) {
    mpz_mul_2exp(tmp, tmp, REG_BITS); // left shift
    mpz_add_ui(tmp, tmp, words[i]);
  }

  // Unpack and reduce
  mpz_set_ui(val, 0);
  uint32_t coeff_mask = (1UL << REDUNDANT_BITS) - 1;
  for (size_t i = 0; i < num_coeffs && mpz_sgn(tmp) != 0; i++) {
    uint32_t coeff = mpz_get_ui(tmp) & coeff_mask;
    if (SIGNED && coeff >> (REDUNDANT_BITS - 1)) {
      // Negative coeff, so two complement and subtract
      coeff = (1 << REDUNDANT_BITS) - coeff;
      mpz_set_ui(tmp2, coeff);
      mpz_mul_2exp(tmp2, tmp2, WORD_BITS * i); // left shift
      mpz_sub(val, val, tmp2);
    } else {
      mpz_set_ui(tmp2, coeff);
      mpz_mul_2exp(tmp2, tmp2, WORD_BITS * i); // left shift
      mpz_add(val, val, tmp2);
    }

    mpz_tdiv_q_2exp(tmp, tmp, REDUNDANT_BITS);
  }

  mpz_clears(tmp, tmp2, NULL);
  return size;
}

// Enable PVT
void VdfDriver::EnablePvt() {
  uint32_t pvt_period = 83;
  RegWrite(PVT_PERIOD_REG_OFFSET, pvt_period);
  RegWrite(PVT_ENABLE_REG_OFFSET, (uint32_t)1);
}

// Calculate temperature (C) from input code
double VdfDriver::ValueToTemp(uint32_t temp_code) {
  double a4 = -0.000000000008929;
  double a3 = 0.000000065714;
  double a2 = -0.00018002;
  double a1 = 0.33061;
  double a0 = -60.9267;

  double x4 = pow((double) temp_code, 4.0);
  double x3 = pow((double) temp_code, 3.0);
  double x2 = pow((double) temp_code, 2.0);

  double temp = (a4 * x4) + (a3 * x3) + (a2 * x2) +
                (a1 * (double) temp_code) + a0;
  return temp;
}

// Calculate code from temperature input (C)
uint32_t VdfDriver::TempToValue(double temp) {
  double a4 = 0.000000027093;
  double a3 = 0.00002108;
  double a2 = 0.0076534;
  double a1 = 3.7764;
  double a0 = 205.64;

  double x4 = pow(temp, 4.0);
  double x3 = pow(temp, 3.0);
  double x2 = pow(temp, 2.0);

  uint32_t temp_code = (uint32_t)((a4 * x4) + (a3 * x3) + (a2 * x2) +
                                  (a1 * temp) + a0 + 0.5);

  return temp_code;
}

// Get temperature from PVT
double VdfDriver::GetPvtTemp() {
  int ret_val;
  uint32_t temp_data;
  ret_val = RegRead(PVT_TEMPERATURE_REG_OFFSET, temp_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetPvtTemp bad reg read %d\n", ret_val);
    return 0.0;
  }

  double temp = ValueToTemp(temp_data);

  return temp;
}

// Get voltage from PVT
double VdfDriver::GetPvtVoltage() {
  int ret_val;
  uint32_t voltage_data;
  ret_val = RegRead(PVT_VOLTAGE_REG_OFFSET, voltage_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetPvtVoltage bad reg read %d\n", ret_val);
    return 0.0;
  }

  double a1 = 0.00054903;
  double a0 = 0.45727;

  double voltage = (a1 * (double) voltage_data) + a0;
  return voltage;
}

// Get the programmed temperature external alarm (C)
double VdfDriver::GetTempAlarmExternal() {
  int ret_val;
  uint32_t temp_data;
  ret_val = RegRead(PVT_TEMP_ALARM_EXTERNAL_REG_OFFSET, temp_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetTempAlarmExternal bad reg read %d\n", ret_val);
    return 0.0;
  }

  temp_data &= PVT_TEMP_ALARM_EXTERNAL_THRESHOLD_MASK;

  double temp = ValueToTemp(temp_data);
  return temp;
}

// Get the programmed temperature engine alarm (C)
double VdfDriver::GetTempAlarmEngine() {
  int ret_val;
  uint32_t temp_data;
  ret_val = RegRead(PVT_TEMP_ALARM_ENGINE_REG_OFFSET, temp_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetTempAlarmEngine bad reg read %d\n", ret_val);
    return 0.0;
  }

  temp_data &= PVT_TEMP_ALARM_ENGINE_THRESHOLD_MASK;

  double temp = ValueToTemp(temp_data);
  return temp;
}

// Check if the temperature external alarm is triggered
bool VdfDriver::IsTempAlarmExternalSet() {
  int ret_val;
  uint32_t temp_data;
  ret_val = RegRead(PVT_TEMP_ALARM_EXTERNAL_REG_OFFSET, temp_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetTempAlarmExternal bad reg read %d\n", ret_val);
    return true;
  }

  return (((temp_data >> PVT_TEMP_ALARM_EXTERNAL_STATUS_BIT) & 0x1) == 1);
}

// Check if the temperature engine alarm is triggered
bool VdfDriver::IsTempAlarmEngineSet() {
  int ret_val;
  uint32_t temp_data;
  ret_val = RegRead(PVT_TEMP_ALARM_ENGINE_REG_OFFSET, temp_data);

  if (ret_val != 0) {
    fprintf(stderr, "GetTempAlarmEngine bad reg read %d\n", ret_val);
    return true;
  }

  return (((temp_data >> PVT_TEMP_ALARM_ENGINE_STATUS_BIT) & 0x1) == 1);
}

bool VdfDriver::IsTempAlarmSet() {
  bool eng_alarm = IsTempAlarmEngineSet();
  bool ext_alarm = IsTempAlarmExternalSet();
  return (eng_alarm || ext_alarm);
}

// Set the PVT temperature external alarm threshold
bool VdfDriver::SetTempAlarmExternal(double temp) {
  uint32_t temp_code = TempToValue(temp);
  if ((temp_code < 0x0) || (temp_code > 0x3FF)) {
    fprintf(stderr, "SetTempAlarmExternal bad code %X from temp %lf\n",
            temp_code, temp);
    return false;
  }
  RegWrite(PVT_TEMP_ALARM_EXTERNAL_REG_OFFSET, temp_code);
  return true;
}

// Set the PVT temperature engine alarm threshold
bool VdfDriver::SetTempAlarmEngine(double temp) {
  uint32_t temp_code = TempToValue(temp);
  if ((temp_code < 0x0) || (temp_code > 0x3FF)) {
    fprintf(stderr, "SetTempAlarmEngine bad code %X from temp %lf\n",
            temp_code, temp);
    return false;
  }
  RegWrite(PVT_TEMP_ALARM_ENGINE_REG_OFFSET, temp_code);
  return true;
}

void VdfDriver::ResetPLL() {
  RegWrite(CLOCK_CONTROL_REG_OFFSET, (uint32_t)0x1); // Reset
}

bool VdfDriver::SetPLLFrequency(double frequency, uint32_t entry_index) {
  if (frequency > pll_entries[VALID_PLL_FREQS - 1].freq) {
    fprintf(stderr, "SetPLLFrequency frequency too high %lf\n", frequency);
    return false;
  }

  if (!entry_index) {
    // Reset is necessary for the initial setting of frequency
    Reset(10000);
    while (frequency > pll_entries[entry_index].freq) {
      entry_index++;
    }
  }

  uint32_t divr = pll_entries[entry_index].settings.divr;
  uint32_t divfi = pll_entries[entry_index].settings.divfi;
  uint32_t divq = pll_entries[entry_index].settings.divq;

  double ref_freq = 100.0 / (divr + 1);
  if (ref_freq > pll_filter_ranges[VALID_PLL_FILTER_RANGES - 1]) {
    fprintf(stderr, "SetPLLFrequency ref_freq too high %lf\n", ref_freq);
    return false;
  }

  uint32_t filter_range = 0;
  while (ref_freq >= pll_filter_ranges[filter_range]) {
    filter_range++;
  }

  //printf("Frequency %lf should use entry %d - divr %d fi %d q %d range %d\n",
  //       frequency, entry_index, divr, divfi, divq, filter_range);

  RegWrite(CLOCK_CONTROL_REG_OFFSET, (uint32_t)0x1); // Reset

  RegWrite(CLOCK_PRE_DIVIDE_REG_OFFSET, divr);
  RegWrite(CLOCK_FB_DIVIDE_INTEGER_REG_OFFSET, divfi);
  RegWrite(CLOCK_POST_DIVIDE_REG_OFFSET, divq);
  RegWrite(CLOCK_FILTER_RANGE_REG_OFFSET, filter_range);

  RegWrite(CLOCK_CONTROL_REG_OFFSET, (uint32_t)0x4); // New div

  // Wait for ack on new div
  int ret_val;
  uint32_t pll_status = 0;
  int read_attempts = 0;
  while (((pll_status >> CLOCK_STATUS_DIVACK_BIT) & 0x1) == 0) {
    ret_val = RegRead(CLOCK_STATUS_REG_OFFSET, pll_status);
    read_attempts++;
    usleep(1000);
    if ((read_attempts > 4) || (ret_val != 0)) {
      fprintf(stderr, "SetPLLFrequency pll div never ack'd\n");
      return false;
    }
  }

  // Clear control reg
  RegWrite(CLOCK_CONTROL_REG_OFFSET, (uint32_t)0x0);

  // Wait for lock
  pll_status = 0;
  read_attempts = 0;
  while (((pll_status >> CLOCK_STATUS_LOCK_BIT) & 0x1) == 0) {
    ret_val = RegRead(CLOCK_STATUS_REG_OFFSET, pll_status);
    read_attempts++;
    usleep(1000);
    if ((read_attempts > 4) || (ret_val != 0)) {
      fprintf(stderr, "SetPLLFrequency pll never locked\n");
      return false;
    }
  }

  // Remember current frequency
  this->freq_idx = entry_index;
  this->last_freq_update = vdf_get_cur_time();

  return true;
}

double VdfDriver::GetPLLFrequency() {
  int ret_val;
  uint32_t pll_status = 0;
  ret_val = RegRead(CLOCK_CONTROL_REG_OFFSET, pll_status);

  if (ret_val != 0) {
    fprintf(stderr, "GetPLLFrequency bad reg read %d\n", ret_val);
    return 0.0;
  }

  //printf("GetPLLFrequency status %x\n", pll_status);
  if (((pll_status >> CLOCK_CONTROL_BYPASS_BIT) & 0x1) == 1) {
    return 100.0;
  } else if (((pll_status >> CLOCK_CONTROL_USEREF_BIT) & 0x1) == 1) {
    return 100.0;
  } else if (((pll_status >> CLOCK_CONTROL_RESET_BIT) & 0x1) == 1) {
    return 0.0;
  }

  uint32_t divr;
  uint32_t divfi;
  uint32_t divq;

  ret_val |= RegRead(CLOCK_PRE_DIVIDE_REG_OFFSET, divr);
  ret_val |= RegRead(CLOCK_FB_DIVIDE_INTEGER_REG_OFFSET, divfi);
  ret_val |= RegRead(CLOCK_POST_DIVIDE_REG_OFFSET, divq);

  //printf("Frequency registers - divr %d fi %d q %d\n", divr, divfi, divq);

  if (ret_val != 0) {
    fprintf(stderr, "GetPLLFrequency bad reg read %d\n", ret_val);
    return 0.0;
  }

  double ref_freq = 100.0 / (divr + 1);
  double vco_freq = ref_freq * (divfi + 1) * 4;
  double freq = vco_freq / ((divq + 1) * 2);
  return freq;
}

int VdfDriver::Reset(uint32_t sleep_duration = 1000) {
  int ret_val;
  ret_val = ftdi.SetGPIO(GPIO_PORT2, 0);
  if (ret_val != 0) {
    fprintf(stderr, "Reset failed to set gpio, %d\n", ret_val);
    return ret_val;
  }
  usleep(sleep_duration);
  // Some boards will contain FT4222H chips with the OTP programmed such
  // that GPIO2 (VDF_RST_N) is configured as an open drain output.  In
  // this case, GPIO2 powers up actively driving out low.  In order to
  // release reset, the GPIO2 needs to not be actively driven so the 1.8V
  // 10Kohm pullup on the board causes VDF_RST_N to be high thus removing
  // the reset condition.  This is accomplished not by the method
  // TriGPIO(port=2) but by SetGPIO(port=2, value=1).
  //
  // Other boards may not have their OTP programmed in which case, GPIO2
  // acts as a generic GPIO whose output enable and output value are
  // fully under the control of software.  In this case TriGPIO(port=2)
  // removes the reset condition.  Using SetGPIO(port=2, value=1) would
  // actively drive high which could cause contention between low and
  // high drivers in the event that another reset initiator (voltage
  // regulator or reset button for instance) were driving reset low.
  //
  // A compromise here is to issue SetGPIO(port=2, value=1) followed
  // immediately by TriGPIO(2).  The hope is that the amount of potential
  // low/high contention is limited in the event that the FT4222H has
  // not had its OTP programmed.
  ret_val = ftdi.SetGPIO(GPIO_PORT2, 1);
  if (ret_val != 0) {
    fprintf(stderr, "Reset failed to set gpio, %d\n", ret_val);
    return ret_val;
  }
  ret_val = ftdi.TriGPIO(GPIO_PORT2);
  if (ret_val != 0) {
    fprintf(stderr, "Reset failed to tri-state gpio, %d\n", ret_val);
  }
  usleep(100000);
  return ret_val;
}

int VdfDriver::TurnFanOn() {
  int ret_val;
  ret_val = ftdi.SetGPIO(GPIO_PORT3, 1);
  if (ret_val != 0) {
    fprintf(stderr, "TurnFanOn failed to set gpio, %d\n", ret_val);
  }
  return ret_val;
}

int VdfDriver::TurnFanOff() {
  int ret_val;
  ret_val = ftdi.SetGPIO(GPIO_PORT3, 0);
  if (ret_val != 0) {
    fprintf(stderr, "TurnFanOff failed to set gpio, %d\n", ret_val);
  }
  return ret_val;
}

int VdfDriver::I2CWriteReg(uint8_t i2c_addr, uint8_t reg_addr, uint8_t data) {
  int ret;
  uint8_t wbuf[2];
  uint16_t bytesXfered;

  wbuf[0] = reg_addr;
  wbuf[1] = data;

  ret = ftdi.i2c_TransmitX(1, 1, i2c_addr, wbuf, 2, bytesXfered );
  if (ret != FtdiDriver::I2C_RETURN_CODE_success) {
    fprintf(stderr, "I2CWriteReg reg %x failed %d\n", reg_addr, ret);
    return ret;
  } else if (bytesXfered != 2) {
    fprintf(stderr, "I2CWriteReg reg %x nack b %d\n", reg_addr, bytesXfered);
    return FtdiDriver::I2C_RETURN_CODE_nack;
  }
  //printf("I2CWriteReg %x %x with %x OK\n", i2c_addr, reg_addr, data);

  return FtdiDriver::I2C_RETURN_CODE_success;
}

int VdfDriver::I2CReadReg(uint8_t i2c_addr, uint8_t reg_addr,
                          size_t len, uint8_t* data) {
  int ret;
  uint8_t wbuf[1];
  uint16_t bytesXfered;

  wbuf[0] = reg_addr;

  ret = ftdi.i2c_TransmitX(1, 0, i2c_addr, wbuf, 1, bytesXfered);
  if (ret != FtdiDriver::I2C_RETURN_CODE_success) {
    fprintf(stderr, "I2CReadReg wr reg %x failed %d\n", reg_addr, ret);
    return ret;
  } else if (bytesXfered != 1) {
    fprintf(stderr, "I2CReadReg wr reg %x nack b %d\n", reg_addr, bytesXfered);
    return FtdiDriver::I2C_RETURN_CODE_nack;
  }

  ret = ftdi.i2c_ReceiveX(1, 1, i2c_addr, data, len, bytesXfered);
  if (ret != FtdiDriver::I2C_RETURN_CODE_success) {
    fprintf(stderr, "I2CReadReg rd reg %x failed %d\n", reg_addr, ret);
    return ret;
  } else if (bytesXfered != len) {
    fprintf(stderr, "I2CReadReg rd reg %x nack b %d\n", reg_addr, bytesXfered);
    return FtdiDriver::I2C_RETURN_CODE_nack;
  }

  if (len == 2) {
    uint16_t d = (((uint16_t)data[0]) << 4) | (((uint16_t)data[1]) >> 4);
    data[0] = d & 0xFF;
    data[1] = (d >> 8) & 0xFF;
  }

  return FtdiDriver::I2C_RETURN_CODE_success;
}

double VdfDriver::GetBoardVoltage() {
  uint8_t vid;
  int ret_val = I2CReadReg(VR_I2C_ADDR, 0x7, 1, &vid);
  if (ret_val != 0) {
    fprintf(stderr, "GetBoardVoltage failed to read reg, %d\n", ret_val);
    return 0.0;
  }

  double vr_factor = 100000.0;
  double vr_slope = 625.0;
  double vr_intercept = 24375.0;
  double voltage = (vr_intercept + (vid * vr_slope)) / vr_factor;
  return voltage;
}

int VdfDriver::SetBoardVoltage(double voltage) {
  uint8_t vid;
  double vr_factor = 100000.0;
  uint32_t vr_slope = 625;
  uint32_t vr_intercept = 24375;
  vid =
    (uint8_t)((((uint32_t)(voltage * vr_factor)) - vr_intercept) / vr_slope);

  if ((vid < 0x29) || (vid > 0x79)) {
    fprintf(stderr, "SetBoardVoltage illegal vid %d for voltage %1.3f\n",
            vid, voltage);
    return 1;
  }

  int ret_val = I2CWriteReg(VR_I2C_ADDR, 0x8, 0xe4); // Fixed compensation value
  ret_val |= I2CWriteReg(VR_I2C_ADDR, 0x7, vid);     // Voltage value
  if (ret_val != 0) {
    fprintf(stderr, "SetBoardVoltage failed to write reg, %d\n", ret_val);
  }

  return ret_val;
}

double VdfDriver::GetBoardCurrent() {
  uint16_t cs;

  int ret_val = I2CWriteReg(CS_I2C_ADDR, 0xA, 0x2);
  if (ret_val != 0) {
    fprintf(stderr, "GetBoardCurrent failed to write reg, %d\n", ret_val);
    return 0.0;
  }

  usleep(10000);

  ret_val = I2CReadReg(CS_I2C_ADDR, 0x0, 2, (uint8_t*)(&cs));
  if (ret_val != 0) {
    fprintf(stderr, "GetBoardCurrent failed to read reg, %d\n", ret_val);
    return 0.0;
  }

  double cs_vmax = 440.0; // mv
  double cs_gain = 8.0;
  double cs_adc_max = 4096.0;

  double c = ((double)cs * cs_vmax) / (cs_gain * cs_adc_max);

  return c;
}

double VdfDriver::GetPower() {
  double current = GetBoardCurrent();
  double voltage = GetBoardVoltage();
  return (current * voltage);
}
