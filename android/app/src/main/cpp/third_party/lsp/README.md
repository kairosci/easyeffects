<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# LSP Compensation Delay provenance

`DelayProcessor` follows the user-visible behavior of LSP Compensation Delay
x2 Stereo 1.0.39: independent channel modes, time/sample/distance conversion,
temperature-dependent sound speed, phase, dry/wet gains, common gains, bypass,
and input/output peak meters. The behavior was checked against
`lsp-plugins-comp-delay` commit `f021265d5e264626fd9f1ae3080296243b807405`
and `lsp-dsp-units` commit `8dd2a6905ca7ccefd2c3624560235680cb8761e2`.

The source projects are LGPL-3.0-or-later; the license is included at
`android/third_party/licenses/lsp-LGPL-3.0.txt`. This Android implementation is
an independent C++20 processor and includes no LV2 host, Linux, or LSP binary.

Sources: <https://github.com/lsp-plugins/lsp-plugins-comp-delay> and
<https://github.com/lsp-plugins/lsp-dsp-units>
