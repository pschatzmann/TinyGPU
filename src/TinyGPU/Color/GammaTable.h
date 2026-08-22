#pragma once
#include <math.h>
#include <stdint.h>

namespace tinygpu {

/**
 * @brief Precomputed 8-bit gamma correction lookup table.
 *
 * Applies out = pow(in / 255, gamma) * 255 to a channel value. This
 * compensates for a display's non-linear brightness response - on some
 * cheap panels, near-black tones show a visible color tint because tiny
 * per-channel differences (e.g. R=33 vs B=32) get amplified into a
 * visible hue by the panel's own response curve, even though the exact
 * same values render correctly at higher, well-saturated brightness.
 *
 * There's no single correct gamma value for an unknown/undocumented
 * panel - try values on both sides of 1.0 and compare on real hardware:
 * gamma < 1.0 lifts (brightens) dark input values before they reach the
 * panel; gamma > 1.0 compresses them further. 1.0 (the default) applies
 * no correction.
 */
class GammaTable {
 public:
  GammaTable() { setGamma(1.0f); }
  explicit GammaTable(float gamma) { setGamma(gamma); }

  void setGamma(float gamma) {
    gamma_ = gamma;
    for (int i = 0; i < 256; ++i) {
      if (gamma == 1.0f) {
        table_[i] = static_cast<uint8_t>(i);
        continue;
      }
      const float normalized = static_cast<float>(i) / 255.0f;
      const float corrected = powf(normalized, gamma);
      int value = static_cast<int>(corrected * 255.0f + 0.5f);
      if (value < 0) value = 0;
      if (value > 255) value = 255;
      table_[i] = static_cast<uint8_t>(value);
    }
  }

  float gamma() const { return gamma_; }

  uint8_t apply(uint8_t value) const { return table_[value]; }

 protected:
  float gamma_ = 1.0f;
  uint8_t table_[256];
};

}  // namespace tinygpu
