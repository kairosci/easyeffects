<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Calf Crusher DSP provenance

`CrusherProcessor` is a C++20 adaptation of Calf Studio Gear's Crusher
`bitreduction` and `samplereduction` algorithms. It follows the Calf source at
commit `a1f87a4ebe015f3f79205394ed9ceeacf052a64d`, principally
`src/audio_fx.cpp` and `src/modules_dist.cpp`. Original DSP authors are Markus
Schmidt and Christian Holschuh; Calf contributors retain their copyright.

Calf is LGPL-2.1-only. The license text is included at
`android/third_party/licenses/calf-LGPL-2.1.txt`. EasyEffects' Android
adaptation remains GPL-3.0-or-later. No Calf binary, LV2 host, GTK code, or Linux
dependency is included.

Upstream source: <https://github.com/calf-studio-gear/calf>
