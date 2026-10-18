<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Android UI and feature parity roadmap

This roadmap tracks Android against **complete desktop feature parity**. Android
may adapt navigation, lifecycle, routing, and layout to mobile, but plugin DSP,
parameters, presets, localized terminology, and behavior remain at desktop
scope. A plugin counts as implemented only when its dedicated C++ implementation
is compared to desktop reference behavior and Android output is validated. OEM
effects and an enabled UI switch are not substitutes for processor parity.

## Current prototype

- Plugin / App / About bottom navigation. The left drawer is available only in
  Plugin and contains the Android-backed plugin list.
- Icon-only input/output route selectors with accessible device names.
- EQ response graph, direct band editing, global preamp, band settings, and
  Android effect controls.
- Every plugin shown in the sidebar has its own activator at the top of its
  page. The EQ switch controls only EQ; platform and dynamics switches control
  only their own effect, while the background service stays alive for as long as
  any output plugin remains enabled. Autogain has its own page whose preview
  toggle drives the ported C++ DSP through an app-owned AAudio tone.
- Global enable/bypass and preset import/export actions on App; Android system
  scope is described honestly.
- About page and desktop-derived dark palette, spacing, and angular controls.

## Sprint 1 — Navigation and content fit

**Goal:** make destinations and drawer predictable on phone and tablet.

- Check compact widths, landscape, split screen, cutouts, gesture navigation,
  and font scales 1.0–1.5.
- Keep every control within safe content insets; allow scrolling without hiding
  the active control behind the bottom bar.
- Make route icons and drawer toggle accessible, with TalkBack labels and clear
  focus order.
- Review captures for Plugin closed/open, App, and About in Italian and English.

**Acceptance:** no clipped or overlapping controls at supported sizes; drawer
only on Plugin; bottom navigation remains usable during scroll; accessibility
labels identify destinations and selected state.

## Sprint 2 — Plugin behavior and status

**Goal:** close gaps between visible controls and real Android effect state.

- Audit Equalizer, Bass Boost, Virtualizer, Loudness Enhancer, Compressor,
  Expander, Gate, and Limiter against device APIs and C++ mappings.
- Show capability, enabled state, and unavailable/error state from the backend;
  do not infer success from a toggle click.
- Verify parameter ranges, reset behavior, audio session lifecycle, route
  changes, and background service recovery on supported devices.
- Keep unsupported desktop processors out of the active drawer until an Android
  implementation exists.

**Acceptance:** each listed control has a backend mapping, lifecycle handling,
C++ tests for portable logic, and at least one recorded device validation;
failure states are visible and recoverable.

## Sprint 3 — C++ parity foundation and equalizer family

**Goal:** make the desktop DSP algorithms portable and establish one plugin per
native module.

- Split C++ into a shared DSP support library plus one implementation and test
  file for each plugin. A plugin file owns that plugin's DSP/configuration only.
- Port Equalizer's complete desktop filter set, channel modes, band interactions,
  preamp, bypass, solo, and preset model; retain the Android output activation
  path and verify it against the desktop reference response.
- Port Autogain, Bass Enhancer, Bass Loudness, Loudness, and Maximizer as
  separate processors with their full parameter models and independent
  activators.
- Keep Android BassBoost/LoudnessEnhancer as optional route capabilities, never
  as the processor implementation for these desktop plugins.

**Acceptance:** per-plugin C++ fixtures compare impulse, swept-sine, and
representative audio output against desktop reference fixtures within declared
tolerances; UI activators and persisted settings are independent.

## Sprint 4 — Dynamics and level tools

**Plugins:** Compressor, Expander, Gate, Limiter, Multiband Compressor,
Multiband Gate, Deesser, and Level Meter.

- Port each processor to an independent C++ module with desktop control ranges,
  envelope timing, band/crossover behavior, meters, and reset semantics.
- Do not map multiple desktop plugins onto one reduced `DynamicsProcessing`
  stage. The Android effect may be retained as an optional fallback only when
  its limits are exposed and the desktop algorithm still exists in C++.
- Validate linked and independent channels and process block boundaries.

**Acceptance:** independent plugin activation and state; desktop parity fixtures
for static and time-varying signals; no shared-stage toggles that unexpectedly
disable another plugin.

## Sprint 5 — Stereo, filter, and time effects

**Plugins:** Crossfeed, Crosstalk Canceller, Mid/Side Equalizer, Stereo Tools,
Filter, Delay, Reverb, Convolver, and Echo Canceller.

- Port matrix and channel-routing behavior in C++; use separate modules for each
  processor and explicit channel-layout conversion at the graph boundary.
- Add Android-native file selection and URI permission persistence for impulse
  responses, and an IR loader that runs off the audio callback.
- Define capture versus output semantics for Echo Canceller and expose it only
  in the matching route.

**Acceptance:** channel-isolation, impulse-response, latency, sample-rate, and
route-change fixtures match desktop behavior; loading and errors are recoverable.

## Sprint 6 — Harmonic and creative effects

**Plugins:** Autotune, Crusher, Crystalizer, Exciter, Harmonizer, Pitch, and
Spectrum.

- Port each DSP and analysis path independently; package model/data files with
  licensing and ABI checks where the desktop plugin depends on external code.
- Keep analysis and audio processing off the UI thread and publish bounded,
  allocation-free frames to the graph.
- Preserve desktop parameter semantics and preset values.

**Acceptance:** numerical parity fixtures, stable real-time processing at
supported rates, correct pitch/frequency labels, and independent UI controls.

## Sprint 7 — Speech and noise processing

**Plugins:** DeepFilterNet, Voice Suppressor, RNNoise, and Speex.

- Port or package the compatible C++ runtimes and models, one backend per
  plugin, with explicit model/version/license tracking.
- Benchmark latency, memory, thermal load, and battery consumption on target
  Android devices before enabling a processor by default.
- Match desktop settings and presets; show model availability and processing
  status from the backend.

**Acceptance:** C++ model/runtime tests, measured real-time factor and latency on
Pixel 7a, lifecycle tests in background operation, and parity fixtures against
desktop output.

## Sprint 8 — Presets and persistence

**Goal:** make App page actions safe and understandable.

- Validate EasyEffects/GraphicEQ/APO imports, report unsupported directives,
  and preview changes before applying.
- Persist Android plugin parameters and the selected profile across process
  restarts.
- Add save, rename, duplicate, delete, import, and export flows only when each
  action is implemented.

**Acceptance:** round-trip tests preserve supported values; invalid files do
not partially mutate active settings; users can recover the last saved profile.

## Sprint 9 — Desktop translation and terminology

**Goal:** align copy with EasyEffects desktop without making Android controls
ambiguous.

- Reuse desktop message IDs/translations where available; inventory strings
  without a desktop equivalent.
- Review supported locales for truncation, number formatting, units, and
  TalkBack pronunciation.
- Keep Android-specific limitation copy accurate and concise.

**Acceptance:** no hard-coded user-facing strings in supported screens; locale
review covers Plugin, App, About, and route chooser; labels fit at supported
font scales.

## Sprint 10 — Visual refinement and release gate

**Goal:** remove rough edges through device evidence, not extra decoration.

- Compare phone and tablet captures with desktop colors, graph behavior, and
  control hierarchy.
- Tune spacing, touch targets, selected/focus states, keyboard/TalkBack
  operation, and empty/error/loading states.
- Verify 16 KB page compatibility, background operation, battery behavior,
  and Android-version/device coverage.
- Publish a feature matrix separating implemented, device-dependent, and
  unavailable desktop features.

**Acceptance:** reviewed captures for all primary pages and drawer states; no
dead controls; C++ tests and Android lint/build pass; release notes state
system-wide processing limits and validated devices.
