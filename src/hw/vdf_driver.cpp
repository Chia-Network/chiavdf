// Copyright Supranational LLC

#include <vector>
#include <cstring>
#include <cassert>
#include "vdf_driver.hpp"

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

