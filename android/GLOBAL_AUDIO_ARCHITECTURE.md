<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Android global audio parity architecture

## Target

Android parity means the desktop plugin graph processes playback from other
apps, in the active output route, while EasyEffects is in the background. The
ordinary APK cannot promise that behavior: session-0 insert effects are
deprecated and vendor controlled. Playback capture is a user-approved copy of
eligible streams, not a replaceable system output route.

If session 0 is rejected, the foreground service can attach a C++ Equalizer to
individual player sessions announced through Android's open/close effect
control-session broadcasts. This is a fallback for cooperative media players,
not a system-wide hook. The service never attaches both paths at once.

The project will support two explicitly different deployment modes:

1. **Consumer APK:** C++ controls the Android framework effects available on
   the device. Report effect state and device limitations; never infer global
   coverage from object creation, `setEnabled`, or readback.
2. **System integration:** a C++ audio effect in the Android Audio Effects HAL
   runs in the selected output path. This requires a source-built ROM/OEM image
   or an equivalent system/vendor integration, its effect factory/configuration,
   and SELinux/service policy. It is not installable as an ordinary APK on a
   locked stock device.

## Processing contract

- Keep DSP and parameter validation in C++20, independent of Qt, JNI, Android
  framework classes, and PipeWire.
- The effect-HAL adapter owns the real-time `process` callback. Allocate memory,
  load impulse responses/models, and build plugin graphs before audio starts;
  parameter changes cross into the callback through bounded lock-free state.
- Do not call JNI, acquire mutexes, log, access storage, or allocate in the
  callback. Preserve the framework-provided sample format, channel layout,
  frame count, and sample rate; handle reconfiguration and bypass without
  dropping or muting audio.
- A plugin is shown as globally available only when the active backend reports
  that it is attached to the selected output route. The consumer APK and system
  integration expose separate capability and status models.
- Keep platform approximations (BassBoost, Virtualizer, LoudnessEnhancer and
  DynamicsProcessing stages) distinct from desktop plugin implementations.

## Delivery gates

1. **Portable graph:** factor reusable C++ DSP and settings out of desktop
   PipeWire/Qt wrappers. Add C++ response, state-transition, channel, format,
   clipping, and real-time allocation tests for every module.
2. **AIDL Effect HAL adapter (Android 14+):** implement the Stable AIDL
   `IEffect`/`IFactory` lifecycle around the C++ graph; add effect identity,
   configuration, command, and FMQ processing. For earlier Android releases,
   evaluate a separate HIDL adapter only when a target device requires it.
3. **Output route attachment:** include the effect library in the system effect
   factory/configuration and configure post-processing on supported output
   devices. Add init/service declarations, permissions, and SELinux policy in
   the target ROM/device tree.
4. **Control plane:** expose plugin settings and backend capabilities through a
   narrow Binder interface. The Android C++ app backend controls that service;
   UI reports missing privilege, unsupported route, lost effect, and active
   route as distinct states.
5. **Plugin ports:** port one desktop algorithm at a time, compare it against
   desktop fixtures, then expose it only after end-to-end route tests pass.
6. **Device qualification:** test speaker, wired, USB, Bluetooth, offload,
   sample-rate changes, background/service death, latency, CPU, and 16 KB pages
   on each supported ROM/device.

## Release criteria

The C++ host suite must pass. Android instrumentation is C++ and must verify
the HAL lifecycle and parameter mapping. End-to-end tests must play known
external-app PCM and capture the post-effect output on the active route, with
the effect bypassed and enabled, and compare the measured response to the
configured curve. Framework readback and listening tests are supplementary.
The global parity claim applies only to qualified system images/devices.

## Platform references

- [Android AudioEffect API](https://developer.android.com/reference/android/media/audiofx/AudioEffect)
- [AOSP audio effects and device effects](https://source.android.com/docs/core/audio/audio-effects)
- [AIDL Audio HAL and Effects HAL interfaces](https://source.android.com/docs/core/audio/aidl-implement)
- [AOSP Effects HAL AIDL definitions](https://android.googlesource.com/platform/hardware/interfaces/+/main/audio/aidl/android/hardware/audio/effect/)
