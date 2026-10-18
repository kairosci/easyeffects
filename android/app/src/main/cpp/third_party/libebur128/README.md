<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# libebur128 dependency

This directory contains the unmodified libebur128 1.2.6 C sources
(`ebur128.c`, `ebur128.h`) used by EasyEffects desktop Autogain to measure
EBU R128 / ITU-R BS.1770 loudness. The upstream files retain Jan Kokemüller's
copyright and license headers. The permissive MIT license is copied to
`android/third_party/licenses/libebur128-COPYING`.

`AutogainProcessor` links this library instead of the desktop system package so
the Android build measures loudness with the same algorithm and version as the
desktop plugin. No LV2 host, Linux, or PipeWire code is included.

Source archive: <https://github.com/jiixyj/libebur128/tree/v1.2.6>
