<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Easy Effects for Android

Android uses a native C++ backend that asks Android's audio framework to attach
an effect to the system output mix. The app itself does not capture or replay
audio. The C++ backend first tries Android `DynamicsProcessing`, which can
apply 32 bands on the tested Pixel, and falls back to the legacy global
`Equalizer` when the device refuses it. It maps the Easy Effects response to
the effect exposed by the device. JNI calls the public Android
audio-effect API; the UI and foreground service only manage user controls and
keep an explicitly enabled effect alive.

The Android notification service restores explicitly enabled output plugins
after reboot, after an app update, and after Android recreates the service.
Equalizer, Bass Boost, Virtualizer, Loudness Enhancer, Compressor, Expander,
Gate, and Limiter keep their state while the app is in the background. Dynamics
stages can run without enabling the EQ curve. Selecting input processing or
turning a plugin off stops its output effect. The route icons are shown only on
the Plugin page; App and About use the full content area.

Android documents output-mix insert effects on session 0 as deprecated. A
device can refuse them or provide no global effect engine. The app checks
control ownership and enabled state and reports platform failure without
claiming system-wide processing. When session 0 is unavailable, the background
service also listens for Android's documented open/close session broadcasts
and attaches the native C++ Equalizer to sessions announced by cooperative
media players. This fallback only covers players that publish their audio
session; it cannot process every app. The service prefers session 0 whenever
Android accepts it, to avoid applying the curve twice.

## Build dependencies

- Android Studio, Android Gradle Plugin, and Android SDK Platform 36.
- Android NDK 27.2 and CMake 3.22.1 (install them through SDK Manager).
- JDK 17.
- AndroidX Activity 1.13.0 and Core 1.18.0.

Build from this directory with `./gradlew :app:assembleDebug`, or open the
directory in Android Studio. Native ELF load segments are linked with 16 KB
page alignment. Verify the APK with `zipalign -c -P 16 4 app-debug.apk`.
The portable DSP suite is C++ only and covers transfer response, all supported
filter types, channel routing, bypass, PCM16 saturation, and invalid input.
It also verifies Crossfeed against the vendored libbs2b reference, including
desktop presets, stereo crossfeed, chunk continuity, gains, bypass, and limits.
Crusher tests cover linear/logarithmic quantization, sample hold, morph, LFO
continuity, bypass, gain, and desktop parameter bounds. Its DSP provenance and
LGPL notice are recorded in `app/src/main/cpp/third_party/calf/README.md`.
Delay tests cover independent stereo settings, LSP time/distance/sample
conversion, temperature, phase, dry/wet, gains, bypass, reset, meters, limits,
and callback continuity. Source revisions and the LGPL notice are in
`app/src/main/cpp/third_party/lsp/README.md`.
Crosstalk Canceller tests cover the desktop six-filter correction, delayed
stereo cancellation, phantom-center mode, gains, bypass, meters, reset, sample
rate validation, and callback continuity.
Mid/Side Equalizer tests cover linked/split 32-band IIR curves, center and
side-only routing, pitch shifts, bypass, balance, and gain behavior. Its custom
PCM processor is not connected to Android's system output route.
Run it with CMake and CTest:

```sh
cmake -S app/src/main/cpp -B build/native-tests -DBUILD_TESTING=ON
cmake --build build/native-tests
ctest --test-dir build/native-tests --output-on-failure
```

The C++ host suite does not verify vendor audio-effect behavior on a device.
The Pixel 7a owner reports a perceptible level increase; the old output-mix
`Visualizer(0)` diagnostic did not match that listening result. Device-level
checks are implemented in C++ and can be invoked in a debug build without a
Kotlin test suite:

```sh
adb shell am start -n com.github.wwmm.easyeffects/.MainActivity --ez runCppBackendChecks true
adb logcat -s EasyEffectsNativeTests:I
```

This check validates Android effect creation, enabled state, reported band
count, band readback, and update ownership. On 2026-10-18, the Pixel 7a also
passed the Compressor-with-EQ-off lifecycle check. The foreground service
remained active after Easy Effects was sent to the background while Metrolist
continued playing; this verifies service lifetime, not universal processing
across apps or Android devices.
The desktop root CMake build is unchanged.

## UI implementation status

The current Android APK still renders its mobile screen with Android Views.
The equalizer curve and parameter panel are real and connected to the C++
response/backend, but desktop QML is not embedded yet. Desktop `Equalizer.qml`
imports `ee.ui`, Qt Graphs, Kirigami, and KDE form-card controls, so it cannot
be loaded unchanged in this app. The intended migration is to embed an adapted
Qt Quick page with Qt Quick for Android's `QtQuickView`, keeping the Android
activity and foreground service around it. The Qt Android SDK and Gradle
integration required to build that path are not installed in this environment.

## Structure

- `app/src/main/cpp/parametric_equalizer.*`: C++ filter model, response, and
  per-band curve calculations.
- `app/src/main/cpp/android_global_equalizer.*`: C++ owner of Android global
  effect creation, curve mapping, activation, and release.
- `app/src/main/cpp/jni_bridge.cpp`: JNI boundary to Android's audio-effect API.
- `app/src/main/kt/.../GlobalEqualizerService.kt`: foreground lifecycle for
  a user-enabled system effect.
- `app/src/main/kt/.../AudioEffectStartupReceiver.kt`: restores an enabled
  output effect after boot or an app update.
- `app/src/main/kt/.../MainActivity.kt`: mobile controls, curve editor, and
  preset import/export.

The global equalizer page provides
up to 32 configurable bands with peaking, low and
high shelves, low-pass, high-pass, band-pass, notch, all-pass, and off filter
types. It supports frequency, Q, gain through ±24 dB, bypass, solo, per-band
left/right channel selection, preamp, logarithmic frequency spacing, and band
sorting. Presets
are saved automatically, imported/exported as the app's versioned JSON format,
and imported/exported as Equalizer APO text. GraphicEQ is interpolated onto 32
peaking-band centers; this is an approximation, not an exact filter fit.

Android presents each available system effect in its own plugin screen. EQ band
details open in a dedicated parameter dialog; the response graph and preamp stay
on the Equalizer screen. The system-effect plugins use Android's native audio
framework implementations, so device support, processing semantics, and
system-wide coverage differ from their desktop counterparts. This is the set of
currently implemented Android plugins, not full desktop plugin parity. The App
page includes an adaptation of the desktop
`OS-Subwoofer` preset at
`app/src/main/assets/presets/OS-Subwoofer-Android.json`. It translates the 60 Hz
second-order high-pass and −6 dB preamp. The desktop Bass Enhancer and Limiter
are identified as omitted because Android's similarly named APIs do not provide
equivalent processing.

Parameter updates are published atomically. The system output effect is
configured outside any audio render callback. The Android platform effects
(Bass Boost, Virtualizer and Loudness Enhancer) and Android's DynamicsProcessing
compressor, expander, noise gate and limiter stages are described in
`PLUGIN_PARITY_ROADMAP.md`; they are not ports of similarly named desktop
plugins. The build packages arm64-v8a, armeabi-v7a, and x86_64.
