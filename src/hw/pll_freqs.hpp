#ifndef _PLL_FREQS_HPP_
#define _PLL_FREQS_HPP_

#include <cstdint>

#define VALID_PLL_FILTER_RANGES 8
#define VALID_PLL_FREQS 8003

/*
struct pll_filter_range {
  double low;
  double high;
};

pll_filter_range pll_filter_ranges[VALID_PLL_FILTER_RANGES] = {
  {5.0, 7.5},
  {7.5, 11.0},
  {11.0, 18.0},
  {18.0, 30.0},
  {30.0, 50.0},
  {50.0, 80.0},
  {80.0, 130.0},
  {130.0, 200,0}
};
*/

extern double pll_filter_ranges[VALID_PLL_FILTER_RANGES];

struct pll_settings {
  uint32_t divr;
  uint32_t divfi;
  uint32_t divq;
};

// freq = 400 * (divfi + 1) / (2 * (divr + 1) * (divq + 1))
struct pll_entry {
  double freq;
  pll_settings settings;
};

extern pll_entry pll_entries[VALID_PLL_FREQS];

#endif // _PLL_FREQS_HPP_
