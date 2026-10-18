/*
 * Copyright © 2026 Wellington Wallace
 * Copyright © 2026 Alessio Attilio
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bass_loudness_processor.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {
void require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

auto rms(const std::vector<float>& pcm, std::size_t offset) -> float {
  double sum = 0.0;
  std::size_t count = 0;
  for (std::size_t i = offset * 2U; i < pcm.size(); i += 2U) {
    sum += static_cast<double>(pcm[i]) * static_cast<double>(pcm[i]);
    sum += static_cast<double>(pcm[i + 1U]) * static_cast<double>(pcm[i + 1U]);
    count += 2U;
  }
  return static_cast<float>(std::sqrt(sum / static_cast<double>(count)));
}

auto sine(float frequency, float sample_rate, std::size_t frames) -> std::vector<float> {
  std::vector<float> pcm(frames * 2U);
  for (std::size_t i = 0; i < frames; ++i) {
    const float sample = 0.2F * std::sin(6.28318530718F * frequency * static_cast<float>(i) / sample_rate);
    pcm[i * 2U] = sample;
    pcm[i * 2U + 1U] = sample;
  }
  return pcm;
}
}  // namespace

int main() {
  constexpr float sample_rate = 48000.0F;
  constexpr std::size_t frames = 48000U;
  ee::mobile::BassLoudnessProcessor processor;
  require(processor.prepare(sample_rate), "accepts common audio sample rate");
  require(!processor.prepare(0.0F), "rejects invalid sample rate");
  require(processor.prepare(sample_rate), "can prepare after invalid rate");

  auto bypass = sine(1000.0F, sample_rate, 256U);
  const auto original = bypass;
  processor.set_parameters({-3.1F, -6.0F, 6.0F, 9.0F, -9.1F, true});
  processor.process(bypass.data(), 256U);
  require(bypass == original, "bypass leaves the signal bit-identical");

  auto low = sine(60.0F, sample_rate, frames);
  auto high = sine(4000.0F, sample_rate, frames);
  processor.set_parameters({0.0F, 0.0F, 0.0F, 0.0F, -100.0F, false});
  processor.process(low.data(), frames);
  processor.reset();
  processor.process(high.data(), frames);
  require(rms(low, frames / 2U) > rms(high, frames / 2U) * 1.25F,
          "positive loudness boosts bass more than upper frequencies");

  auto unlinked = sine(1000.0F, sample_rate, frames);
  auto linked = unlinked;
  processor.set_parameters({0.0F, 0.0F, 0.0F, 0.0F, -100.0F, false});
  processor.process(unlinked.data(), frames);
  processor.reset();
  processor.set_parameters({0.0F, 0.0F, 0.0F, 0.0F, 0.0F, false});
  processor.process(linked.data(), frames);
  require(rms(unlinked, frames / 2U) > rms(linked, frames / 2U) * 3.0F,
          "link constrains output gain to preserve tonal balance");

  std::cout << "PASS: Bass Loudness bypass, contour response, link, and sample-rate handling\n";
  return EXIT_SUCCESS;
}
