#ifndef HIT_H
#define HIT_H

#include <cstdint>

struct CAENEvent {
  uint32_t TimeTag;
  uint32_t Extras;
  uint16_t ChargeShort;
  uint16_t ChargeLong;
  int16_t  Baseline;
  uint8_t  channel; // (channel number) | (digitizer number) << 4
  std::vector<uint16_t> waveform;

  inline uint64_t timestamp() const {
    uint64_t result = Extras & (0x7fff << 16); // bits 16 to 31
    result <<= 31 - 16;
    result |= TimeTag;
    result <<= 10;
    result |= Extras & 0x3ff; // bits 0 to 9
    return result;
  };

  inline uint16_t baseline() const {
    return (uint16_t)-Baseline / 4;
  };
};

struct Hit {
  uint64_t time;
  uint16_t charge_short;
  uint16_t charge_long;
  uint16_t baseline;
  uint8_t  channel;
  bool     has_waveform;

  // only valid if has_waveform is true
  const uint16_t* waveform() const {
    return reinterpret_cast<const uint16_t*>(
        reinterpret_cast<const uint8_t*>(this) + sizeof(*this)
    );
  };

  // only valid if has_waveform is true
  uint16_t* waveform() {
    return const_cast<uint16_t*>(const_cast<const Hit*>(this)->waveform());
  };

  // only valid if has_waveform is true
  uint16_t nsamples() const {
    return waveform()[-1];
  };
};

#endif
