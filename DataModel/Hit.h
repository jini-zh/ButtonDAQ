#ifndef HIT_H
#define HIT_H

#include <cstdint>

struct Hit {
  uint64_t time;
  uint16_t charge_short;
  uint16_t charge_long;
  uint16_t baseline;
  uint8_t  channel;
  std::vector<uint16_t> waveform;
};

#endif
