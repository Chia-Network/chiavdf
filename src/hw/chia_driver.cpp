#include "chia_driver.hpp"

void ChiaDriver::SerializeJob(uint8_t *buf,
                              uint32_t job_id,
                              uint64_t iteration_count,
                              mpz_t a,
                              mpz_t f,
                              mpz_t d,
                              mpz_t l) {
  size_t offset = 0;
  const size_t reg_size = REG_BYTES;
  offset += write_bytes(reg_size, offset, buf, job_id);
  offset += write_bytes(2 * reg_size, offset, buf, iteration_count);
  offset += write_bytes(reg_size * CHIA_VDF_CMD_A_MULTIREG_COUNT,
                        offset, buf, a, NUM_2X_COEFFS);
  offset += write_bytes(reg_size * CHIA_VDF_CMD_F_MULTIREG_COUNT,
                        offset, buf, f, NUM_2X_COEFFS);
  offset += write_bytes(reg_size * CHIA_VDF_CMD_D_MULTIREG_COUNT,
                        offset, buf, d, NUM_4X_COEFFS);
  offset += write_bytes(reg_size * CHIA_VDF_CMD_L_MULTIREG_COUNT,
                        offset, buf, l, NUM_1X_COEFFS);
  // The last word is 0x1 to start the job
  offset += write_bytes(reg_size, offset, buf, (uint32_t)0x1);
}

void ChiaDriver::DeserializeJob(uint8_t *buf,
                                uint32_t &job_id,
                                uint64_t &iteration_count,
                                mpz_t a,
                                mpz_t f) {
  size_t offset = 0;
  const size_t reg_size = REG_BYTES;
  offset += read_bytes(reg_size, offset, buf, job_id);
  offset += read_bytes(2 * reg_size, offset, buf, iteration_count);
  offset += read_bytes(reg_size * CHIA_VDF_STATUS_A_MULTIREG_COUNT,
                       offset, buf, a, NUM_2X_COEFFS);
  offset += read_bytes(reg_size * CHIA_VDF_STATUS_F_MULTIREG_COUNT,
                       offset, buf, f, NUM_2X_COEFFS);
}
