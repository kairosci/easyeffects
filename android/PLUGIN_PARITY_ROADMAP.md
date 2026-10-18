<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Android plugin integration plan

## Goal and platform constraint

The target is the set of desktop plugin functions listed below, with the same
EasyEffects concepts and parameters where the Android backend can represent
them. Android does not let an ordinary app insert an arbitrary C++ PCM chain
into every other app's output. Android's legacy global insert effects, including
Equalizer, Bass Boost and Virtualizer on session `0`, are deprecated; effect
availability and behavior are vendor controlled. The current module therefore has two honest
paths: vendor supplied system effects, and plugins that remain unavailable for
global output until a supported system/OEM route exists. A C++ DSP test alone
does not prove global playback integration.

## Implemented Android output controls

These are Android platform effects controlled and owned through the C++ JNI
backend. They are not desktop plugin ports. The device may refuse them, may not
grant control, or may implement global session processing differently.
The Pixel 7a C++ test proves object creation, parameter calls, enabled-state
readback and teardown; it does not measure effects on another app's PCM.

| Android effect | Android API | Parameters | State |
| --- | --- | --- | --- |
| Bass Boost | `BassBoost` | strength 0–1000 | C++ JNI state test passes on Pixel 7a |
| Virtualizer | `Virtualizer` | strength 0–1000 | C++ JNI state test passes on Pixel 7a |
| Loudness Enhancer | `LoudnessEnhancer` | target gain 0 to +24 dB | C++ JNI state test passes on Pixel 7a |
| Equalizer | `DynamicsProcessing` or `Equalizer` | 32 band response mapped to device | C++ JNI band/update test passes on Pixel 7a |
| Dynamics stages | `DynamicsProcessing` | Compressor, expander, gate and limiter via shared 3-band MBC | C++ JNI configuration and standalone-stage check pass on Pixel 7a (2026-10-18) |

The Android system-effect controls live in the main scrollable content under
“Android system effects.” The left drawer is reserved for plugin navigation, so
vendor effects are not presented as desktop Calf/LSP plugins.

## In-app PCM playback proof

The debug build contains a C++ AAudio playback check. It opens a real Android
output stream, generates a 1 kHz tone in the native callback, processes the PCM
through `ParametricEqualizer` with a known +12 dB peaking band, renders it to the
active Android output route for 1.6 seconds, and checks pre/post DSP energy.
Pixel 7a passed on 2026-10-18: 77,472 stereo frames rendered and 11.997086 dB
measured PCM energy gain. Metrolist was paused for the 1.6 second test and
resumed immediately afterward. The host C++ CTest suite also passes. This
verifies audible **app-owned** PCM processing through the C++ implementation;
it is not evidence that other apps' PCM is transformed by the global session-0
effects.

The Autogain plugin screen uses the same app-owned AAudio category. Its preview
toggle opens an output stream, generates a 997 Hz stereo tone in the native
callback, runs the ported `AutogainProcessor` (vendored libebur128 1.2.6) on it,
and republishes live momentary/short-term/integrated/relative/range/loudness,
output-gain and peak meters. Every screen control maps to the desktop parameter
set and acts on this DSP backend. A debug-only `runAutogainPreviewTest` intent
runs the preview for up to eight seconds and checks that it rendered at least one
second of frames and that the measured output gain converged to the target within
1.5 dB. No device run of the preview is recorded here; a connected device is
needed to confirm audible output and that the meters track the target. Global
processing of other apps' playback remains unavailable to ordinary apps, so
Autogain stays app-owned.

## Desktop plugin inventory

| Desktop plugin | Android integration plan | State |
| --- | --- | --- |
| Autogain | Vendored libebur128 1.2.6 and portable C++ desktop DSP port (references, target, silence, history, force silence, ceiling, gains, bypass, meters) with host tests; Android screen drives an app-owned AAudio preview tone through the DSP | DSP + app-owned preview implemented; output tap still unavailable to ordinary apps |
| Autotune | Port pitch detector/correction DSP to C++; requires global PCM route | Blocked by output tap |
| Bass Enhancer | Android `BassBoost` is only a device dependent approximation | Partial system effect |
| Bass Loudness | MDA/Stevens-Davis contour model, desktop dB-to-LV2 mapping, link, bypass, common gains, and C++ tests implemented | DSP partial; Android screen and system route unavailable |
| Compressor | Map to `DynamicsProcessing.MbcBand` and expose supported controls | Partial Android stage; API 28+ |
| Convolver | Port the C++ convolution engine and URI based IR loading; requires global PCM route | Blocked by output tap |
| Crossfeed | libbs2b 3.1.0 processor, desktop presets/settings, reset, bypass, gains, and level meters implemented and tested in C++ | DSP complete; system route unavailable to ordinary Android apps |
| Crosstalk Canceller | Desktop six-biquad feedback correction, stereo/phantom-center modes, delay, decay, gains, bypass, reset, and meters implemented and tested in C++ | DSP complete; Android system route unavailable to ordinary Android apps |
| Crusher | Calf bit/sample reduction, modes, morph, DC, anti-aliasing, LFO, bypass, gains, reset, and level meters implemented and tested in C++ | DSP complete; system route unavailable to ordinary Android apps |
| Crystalizer | Port Zita DSP or a licensed compatible implementation; requires global PCM route | Blocked by output tap |
| DeepFilterNet | Package compatible model/runtime and run in C++; requires global PCM route | Blocked by output tap/runtime |
| Deesser | Port or package compatible DSP; requires global PCM route | Blocked by output tap |
| Delay | Per-channel LSP samples/time/distance modes, temperature-based speed, phase, dry/wet, common gains, bypass, reset, and peak meters implemented and tested in C++ | DSP complete; system route unavailable to ordinary Android apps |
| Echo Canceller | Android echo cancellation is a capture/communications effect, not a global output plugin | Not applicable to output |
| Equalizer | C++ parametric model mapped into Android global effects | Partial; device dependent |
| Mid/Side Equalizer | Linked or split 32-band IIR curves, separate pitch shifts, balance, gains, bypass, and C++ tests | IIR core partial; LSP mode/topology parity and Android system route pending |
| Exciter | Port nonlinear harmonic DSP; requires global PCM route | Blocked by output tap |
| Expander | Map to Android MBC expander parameters and expose supported controls | Partial Android stage; API 28+ |
| Filter | Port filter bank to C++; requires global PCM route | Blocked by output tap |
| Gate | Map to Android MBC noise gate where the device supports it | Partial Android stage; API 28+ |
| Voice Suppressor | Package compatible suppression DSP; requires global PCM route | Blocked by output tap/runtime |
| Level Meter | Requires reliable system output tap | Blocked by output tap |
| Limiter | Map to Android `DynamicsProcessing.Limiter` | Partial Android stage; API 28+ |
| Loudness | Android `LoudnessEnhancer` is a gain effect, not the desktop loudness model | Partial system effect |
| Maximizer | Requires desktop envelope/gain model and global PCM route | Blocked by output tap |
| Multiband Compressor | Map Android MBC bands and supported parameters | Not yet implemented; API 28+ |
| Multiband Gate | Map Android MBC gate parameters and supported bands | Not yet implemented; API 28+ |
| Output Level | Requires reliable system output tap | Blocked by output tap |
| Pitch | Port pitch shifting to C++; requires global PCM route | Blocked by output tap |
| Speex | Package compatible SpeexDSP and port settings; requires global PCM route | Blocked by output tap/runtime |
| Spectrum | Requires a reliable system output tap | Blocked by output tap |
| Reverb | Android `PresetReverb` is an auxiliary effect; external tracks must explicitly attach to it | Not available in global output |
| RNNoise | Package compatible RNNoise and port settings; requires global PCM route | Blocked by output tap/runtime |
| Stereo Tools | Android `Virtualizer` is not equivalent to Calf Stereo Tools | Partial system effect only |
| Harmonizer | Port pitch detection/harmonization DSP; requires global PCM route | Blocked by output tap |

## Sprint order

1. **Android platform effect adapter — implemented; global audio route remains device dependent.** C++ owns global effect
   instances, validates ranges, applies vendor effects, reports failures, and
   releases them with the native engine. C++ JNI checks pass for all exposed
   effects and dynamics stages on Pixel 7a; external playback processing still
   requires a separate acoustic verification.
2. **C++ app-owned PCM output — implemented and measured on Pixel 7a.** AAudio
   callback test proves the PCM DSP output is rendered with the expected gain.
   Production playback integration must still use this processor only for
   app-owned streams; the desktop app's global graph needs system/OEM routing.
3. **DynamicsProcessing mapping — first subset implemented.** The app exposes
   compressor and expander MBC stages, noise gate, and limiter through the Android
   C++ backend with threshold and ratio controls. Expand to independent
   per-band parameter sets and desktop-compatible ranges. The Android effect
   remains active when one of these stages is enabled with the EQ curve off; the
   debug JNI check now verifies this lifecycle separately from EQ activation.
   Pixel 7a returned PASS for all four mapped stages and for Compressor running
   with the global EQ disabled.
4. **Portable DSP ports — Crossfeed, Crusher, Delay, Crosstalk Canceller, Bass Loudness, and Autogain implemented; Mid/Side IIR core added; C++ tests pass.**
   Crossfeed uses upstream libbs2b 3.1.0; Crusher follows Calf's LGPL DSP
   algorithms; Delay follows the LSP plugin control set; Crosstalk Canceller
   uses the desktop's fitted six-filter correction. Mid/Side has linked/split
   IIR curves, independent pitch shifts, balance, gains, and bypass; LSP FIR/FFT
   modes and desktop filter topology still need porting. Keep Android pages and
   activators pending until a supported PCM route can consume output.
   Bass Loudness ports the MDA equal-loudness contour model and EasyEffects' dB
   to normalized LV2 parameter mapping; Android UI and system routing remain
   pending. Autogain vendors libebur128 1.2.6 and ports the desktop reference,
   target, silence, maximum-history, force-silence, ceiling, gain, bypass, reset,
   and meter behavior; its Android screen drives an app-owned AAudio preview, so
   the controls run a real DSP backend while global routing stays blocked.
5. **Portable desktop DSP ports.** Extract or port the remaining algorithms one at a time to
   C++20 with no Linux/PipeWire dependency. Add dedicated C++ tests and numerical
   comparison fixtures against desktop implementations.
6. **Global routing proof.** For every DSP plugin, demonstrate that the Android
   system route processes eligible external playback. If the OEM route rejects
   the effect or only attaches to app-owned sessions, keep the plugin unavailable
   in global mode.
7. **State, presets, and UI parity.** Reuse desktop terminology/translations;
   persist parameters, support import/export only where formats match, and
   expose no placeholder controls.
8. **Device matrix and release.** Test supported API levels and OEMs, lifecycle,
   background service, route changes, latency, CPU/battery, accessibility, and
   16 KB page compatibility.

## Exit criteria

“Integrated” for a desktop plugin means its algorithm has C++ tests, the Android
parameter mapping is documented, and a device test proves its intended scope.
“Global” additionally requires a measured change to output from another app.
No API object creation/readback or successful APK build counts as proof of audio
processing. Full desktop plugin parity cannot be claimed unless Android exposes
a supported route for every required module.
