<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Android feature parity inventory

The complete desktop plugin set, controls, presets, translations, and behavior is
the required Android parity target. Android keeps its own system integration,
navigation, lifecycle, and responsive layout, while each desktop processor must
have a matching C++ DSP implementation and a dedicated Android plugin screen and
activator. A vendor AudioEffect is not accepted as a substitute for a desktop
plugin unless its DSP and behavior are shown to match.

| Desktop module | Android status |
| --- | --- |
| Autogain | Desktop ITU-R BS.1770 loudness measurement with vendored libebur128 1.2.6, reference modes, target, silence, maximum history, force silence, peak ceiling, attack/release, common gains, bypass, reset, and level meters implemented and tested in C++; Android screen drives an app-owned AAudio preview of the ported DSP; global system route unavailable to ordinary Android apps |
| Autotune | Not implemented |
| Bass Enhancer | Calf Bass Enhancer DSP (lowpass split, TAP saturation with 2x oversampling, optional highpass floor), desktop parameter mapping, common gains, bypass, reset, and level/harmonics meters implemented and tested in C++; Android screen drives an app-owned AAudio preview of the ported DSP; global system route unavailable to ordinary Android apps |
| Bass Loudness | MDA equal-loudness contour DSP, desktop parameter mapping, Link, bypass, and common gains implemented in C++; Android plugin screen and global route pending |
| Compressor | Reduced Android `DynamicsProcessing` stage; independent desktop C++ processor and full controls pending |
| Convolver | Not implemented |
| Crossfeed | Desktop presets, controls, bypass, common gains, and input/output meters represented in C++; Android screen and system-route activator pending |
| Crosstalk Canceller | Six desktop correction filters, delay, decay, phantom-center mode, common gains, bypass, reset, and meters represented in C++; Android screen and system-route activator pending |
| Crusher | Bit/sample reduction, modes, morph, DC, anti-aliasing, LFO, bypass, common gains, reset, and meters represented in C++; Android screen and system-route activator pending |
| Crystalizer | Not implemented |
| DeepFilterNet | Not implemented |
| Deesser | Not implemented |
| Delay | Per-channel samples/time/distance, temperature, dry/wet, phase, common gains, bypass, reset, and meters represented in C++; Android screen and system-route activator pending |
| Echo Canceller | Not implemented |
| Equalizer | Partial: Android global output effect, 32-band parametric UI/DSP |
| Exciter | Not implemented |
| Expander | Reduced Android `DynamicsProcessing` stage; independent desktop C++ processor and full controls pending |
| Filter | Not implemented |
| Gate | Reduced Android `DynamicsProcessing` stage; independent desktop C++ processor and full controls pending |
| Harmonizer | Not implemented |
| Limiter | Reduced Android `DynamicsProcessing` stage; independent desktop C++ processor and full controls pending |
| Loudness | Not implemented |
| Maximizer | Not implemented |
| Mid/Side Equalizer | C++ IIR Mid/Side processor and tests implemented; Android system route, screen, LSP modes/topologies, and desktop fixtures pending |
| Multiband Compressor | Not implemented |
| Multiband Gate | Not implemented |
| Pitch | Not implemented |
| RNNoise | Not implemented |
| Reverb | Not implemented |
| Speex | Not implemented |
| Stereo Tools | Not implemented |
| Voice Suppressor | Not implemented |

The Bass Loudness processor ports the MDA/Stevens-Davis contour equations and
EasyEffects' dB-to-linear LV2 parameter conversion, plus Link behavior, common
gain controls, and bypass. Its C++ tests cover contour response, Link, bypass,
and sample-rate validation. It is not yet connected to an Android plugin page or
system output route.

The Mid/Side processor separates and decodes stereo Mid/Side audio, supports
linked or independent 32-band IIR curves, separate pitch shifts, balance,
input/output gain, and bypass. It is a portable C++ DSP module with host tests;
it is not exposed in the app because no supported Android global route can run
its custom PCM processor. FIR/FFT/SPM modes and several LSP filter details still
need a faithful port.

The Autogain port vendors libebur128 1.2.6, the same measurement library and
modes the desktop uses, and reproduces the desktop reference selection, target
correction, `1 / peak` safe ceiling, 0.1/0.4 s attack and release, silence
handling, force-silence floor, common gains, bypass, reset, and counter. Its host
C++ tests compare momentary, short-term and integrated loudness against a fresh
libebur128 state and cover range clamping, the peak ceiling, force silence,
bypass, reset, and chunk-boundary stability. Because ordinary Android apps still
cannot insert a processor into other apps' output, the Android Autogain screen
drives an app-owned AAudio preview: the app generates its own 997 Hz stereo tone
and runs it through the ported C++ Autogain DSP, so each control acts on a real
DSP backend. It does not transform other apps' playback; that remains blocked by
the platform boundary below. The preview engine compiles into both ABIs but has
not yet been run on a device here.

Android system effects (Bass Boost, Virtualizer, and Loudness Enhancer) are
currently platform-specific controls, not desktop plugin parity. Their global
session behavior is vendor dependent. Each desktop plugin still requires its
own C++ processor, parameters, activator, preset mapping, and verification.

The Bass Enhancer port reproduces Calf's Bass Enhancer DSP: the two-cascaded
lowpass split (`Scope`), the TAP saturation stage with its 2x oversampling
filters and peak-hold `Harmonics` meter, the optional highpass `Floor`, the
`Blend` harmonic balance, `Amount` wet mix, `Listen` monitoring, common gains,
bypass and reset, with the desktop kcfg ranges. Its host C++ tests cover range
clamping, sample-rate validation, bypass, dry-mix passthrough, the lowpass and
highpass responses, saturation metering and its decay, and callback block
stability. The Android Bass Enhancer screen drives the same app-owned AAudio
preview category as Autogain: the app generates its own 60 Hz stereo tone and
runs it through the ported C++ DSP, so every control acts on a real backend. It
does not transform other apps' playback; that remains blocked by the platform
boundary below. The preview engine compiles into both ABIs but has not yet been
run on a device here.

On Android devices where `DynamicsProcessing` is available, compressor,
expander, gate, and limiter controls currently map to reduced shared Android
stages. Their C++ backend can keep those stages active while the EQ curve is
disabled, and the debug JNI check covers that lifecycle. They do not count as
desktop parity until their independent behavior and full parameter ranges are
implemented and verified against desktop reference fixtures.

The curve editor currently uses a custom Android `View`; it is visible on
launch and connected to the native C++ response. The left drawer contains only
the vertical selector for the nine Android-backed plugin components. The graph
and implemented controls live in the main scrolling content, so parameters are
present by default without opening the drawer. Selecting a plugin closes the
drawer, updates the header, and scrolls the main content to its parameters. The
current APK does not embed the desktop QML runtime. The Qt Quick adaptation is
tracked in the roadmap.

Each of those pages has its own activator at the top of the page. The equalizer
switch changes only the equalizer; Bass Boost, Virtualizer, Loudness Enhancer,
Compressor, Expander, Gate, and Limiter each change only their own backend state.
Autogain's page uses a preview toggle that starts and stops the app-owned AAudio
preview of its DSP. The background service tracks whether any output plugin
remains active, so switching one processor off does not turn off the others.
Desktop processors that have not been ported are not exposed as inactive or
placeholder switches.

The debug APK also has a C++ AAudio PCM playback proof. On the Pixel 7a, its
generated 1 kHz signal passed through the C++ parametric equalizer and produced
the configured +12 dB energy gain while rendering through AAudio. This
proves app-owned PCM processing; it does not prove global processing of other
apps' playback.

The Autogain screen uses the same app-owned category: its preview toggle opens an
AAudio output stream, generates a 997 Hz stereo tone in the native callback, runs
the ported `AutogainProcessor` on it, and prints live loudness and peak meters.
This is a real DSP backend for the controls; it is still app-owned PCM and no
device run is recorded here. A device is required to confirm audio output and
that the meter readings track the target. A debug-only `runAutogainPreviewTest`
intent runs the preview for up to eight seconds and reports whether the measured
output gain converged to the target within 1.5 dB.

The Bass Enhancer screen reuses the same app-owned preview: its toggle generates a
60 Hz stereo tone in the native callback, runs the ported `BassEnhancerProcessor`
on it, and prints input/output peak and peak-hold harmonics meters. A debug-only
`runBassEnhancerPreviewTest` intent runs the preview and reports whether it
rendered at least one second of frames and produced finite non-silent output with
saturation harmonics. No device run is recorded here.

To repeat the device check with a connected Pixel 7a, build the debug APK and
launch the C++ check from the app:

```sh
cd android
ANDROID_HOME=/path/to/Android/Sdk ./gradlew :app:assembleDebug
/path/to/Android/Sdk/platform-tools/adb install -r app/build/outputs/apk/debug/app-debug.apk
/path/to/Android/Sdk/platform-tools/adb shell am force-stop com.github.wwmm.easyeffects
/path/to/Android/Sdk/platform-tools/adb shell am start -n com.github.wwmm.easyeffects/.MainActivity --ez runCppBackendChecks true --ez runPcmPlaybackTest true --ez runAutogainPreviewTest true --ez runBassEnhancerPreviewTest true
/path/to/Android/Sdk/platform-tools/adb logcat -s EasyEffectsNativeTests:I EasyEffectsPcmPreview:I EasyEffectsAutogainPreview:I EasyEffectsBassEnhancerPreview:I
```

Before running, turn off any enabled Android platform or DynamicsProcessing
plugin. The backend log must contain `PASS: C++ JNI backend enabled`; this checks Android
effect activation, enabled-state readback, band access, and an EQ update.
`PARTIAL` may contain that EQ pass while an optional OEM effect is unavailable.
The PCM check plays a short 1 kHz tone: keep media volume above zero and listen
for it. The app refuses to call the run audible when media is muted. Its C++ log
reports drained output frames, selected Android output-device ID, and measured
pre/post DSP energy. It requires the measured gain to be within 1 dB of the
configured +12 dB band. It waits for AAudio's stopped state after its asynchronous
stop request, so it passes only after the queued output has drained ([AAudio
API](https://developer.android.com/ndk/reference/group/audio#audiostream_requeststop)). A `SKIP` means another media stream was active; it is not a test
pass. This verifies the app-owned AAudio PCM path, not acoustic loopback or
arbitrary system playback.

## Platform boundary

The desktop app processes live input/output streams in its PipeWire/GStreamer
pipeline. A regular Android app cannot capture arbitrary system output PCM and
send a transformed mix back to the system. Android documents `CAPTURE_AUDIO_OUTPUT`
as unavailable to third-party apps. `AudioPlaybackCapture` only captures eligible
streams from apps that allow capture, and does not provide a system-wide replaceable
output pipeline. Android's `AudioEffect` API can attach standard effects to session
0, but documents global insert effects as deprecated and leaves behavior to the
vendor audio engine. These APIs cannot host the desktop plugin graph as-is.

Sources: [Android AudioEffect](https://developer.android.com/reference/android/media/audiofx/AudioEffect),
[CAPTURE_AUDIO_OUTPUT](https://developer.android.com/reference/android/Manifest.permission#CAPTURE_AUDIO_OUTPUT),
[AudioPlaybackCaptureConfiguration](https://developer.android.com/reference/android/media/AudioPlaybackCaptureConfiguration).

## Absolute parity milestones

1. **C++ test foundation** — completed for the portable parametric EQ DSP. Tests
   run with CMake/CTest on the host. Android vendor effect behavior still needs a
   native C++ device test runner.
2. **One C++ module per desktop plugin** — port each desktop DSP algorithm and
   its parameter model independently. Keep shared code in narrowly scoped C++
   utilities; do not combine unrelated processors in one source file.
3. **One Android plugin page and activator per plugin** — expose the full desktop
   control set, presets, reset behavior, enabled/bypassed state, and localized
   labels. Disable the activator with a clear capability reason until its backend
   is complete.
4. **Audio graph and routing** — implement and validate the Android system
   integration required to process external playback and capture, including
   session lifecycle, route changes, latency, and background operation.
5. **Desktop parity verification** — compare every processor against desktop
   reference fixtures in C++, then test Android routing and audible behavior on
   supported devices. UI state or an effect object's enabled flag is insufficient.
6. **Presets, translations, and release** — support desktop-compatible preset
   data where semantics match, reuse desktop message IDs/translations, and test
   accessibility, 16 KB pages, background recovery, performance, and battery.

Absolute global parity requires both the complete C++ processor set and a
supported Android system audio route. The current ordinary-app APIs do not offer
a general replacement pipeline for arbitrary other-app PCM. This is an open
platform blocker, not a reason to lower the parity target or label a partial
mapping complete. See `PLUGIN_PARITY_ROADMAP.md` for the staged execution plan.
