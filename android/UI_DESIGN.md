<!-- Copyright © 2026 Wellington Wallace
     Copyright © 2026 Alessio Attilio
     SPDX-License-Identifier: GPL-3.0-or-later -->
# Android equalizer UI direction

## Concept

Use EasyEffects' desktop visual language as the skin and BOOM 3D's equalizer
workflow as the mobile interaction reference. The result is a compact control
surface, not a miniature desktop window: a large EQ plot is the home screen;
band handles select the band; a left slide-in sidebar contains only plugin
navigation, while every implemented control stays in the main scrollable
content. The reference informs hierarchy and direct graph editing, while all
EasyEffects labels, colors, and behavior remain native to EasyEffects.

The desktop dark interface is the visual source: an anthracite `#202326` canvas,
charcoal `#292C30` controls, a `#1F1F1F` graph surface, and thin grey
outlines (`#4B4E52`). Use a restrained desaturated blue (`#294D61` selection, `#4C9AB8`
focus) for navigation and selected controls. Reserve EasyEffects green (`#22D478`)
for the EQ response and audio activity; use a quieter related green (`#69C99D`)
to distinguish the second channel. The mobile adaptation uses square geometry,
fine borders, strong alignment, and deliberate 8/16/24 dp spacing. Corners are
0–2 dp; actions have at least 52 dp height and 10 dp vertical separation. Do not
use pills, rounded cards, gradients, or large glow effects.

## Layout

1. Status and navigation insets remain outside the app content; the page applies
   its own safe padding and nothing overlays system bars.
2. Do not reserve header space for a repeated plugin title. Put the drawer toggle
   at the left and the microphone/output target icons at the right. This header
   is shown only on the Plugin destination; App and About use the full content
   height without route controls. Exactly one
   audio target is selected at a time; the selected icon opens its device chooser.
   Device names stay in accessible descriptions and chooser content, not visible
   header text. Selecting Input disables every output effect. If Android cannot
   provide the selected input path, state that clearly and leave processing off.
3. The bottom navigation has three destinations: Plugin, App, and About. The
   plugin drawer is available only while Plugin is selected. App contains global
   enable/bypass, presets, and reset actions; it explains Android's per-app audio
   limits, and states that output processing uses Android's no-root effect API
   with device-dependent coverage. About shows the desktop EasyEffects logo,
   version and license details, plus direct GitHub and GitHub Sponsors links.
4. The EQ graph is the first work area in the Plugin scroll view. It shows
   logarithmic frequency with the desktop 1/3-octave landmarks, localized
   decimal separators, dB grid, response, and draggable handles positioned on
   the calculated curve. Labels shrink on compact screens to keep the axis clear.
5. A left-side collapsible drawer is the plugin sidebar. It contains only the
   vertical plugin list. The app bar remains above the drawer, and its menu icon
   changes to a close icon while open so the same control can close it.
   Selecting an entry closes the drawer and moves the main content to that
   plugin's controls. Each Android-backed plugin, plus the Autogain and Bass
   Enhancer previews, has its own entry:
   Equalizer, Bass Boost, Virtualizer, Loudness Enhancer, Compressor, Expander,
   Gate, Limiter, Autogain, and Bass Enhancer. Autogain and Bass Enhancer are
   portable C++ DSPs driven through an app-owned AAudio preview rather than a
   global route. Selecting an entry jumps
   to that plugin's own parameter
   group below. Desktop plugins without a working Android backend stay in the
   parity catalog and roadmap; they are not shown as empty controls.
6. The graph and active plugin controls remain in the main content; the drawer
   is only for plugin navigation. The Plugin page starts with the graph, then
   shows preamp, band controls, and working Android effect parameters. Dragging
   a point changes frequency horizontally and gain vertically; show exact values
   in the selected-band summary and keep type, Q, channel, enable, and solo
   explicit. The App page owns global controls and preset actions. Use separators
   and section labels to group each page.
7. Controls remain in the content bounds at small widths, large font scale, and
   display cutouts. The graph and the beginning of the EQ controls are visible
   on launch; remaining controls are reached by vertical scrolling.

## Typography and colors

Use Android's system font and desktop's existing localized message IDs. Titles
are compact and bold; values are tabular where possible; section labels use the
existing accent. The canvas and graph use the desktop dark colors above. Keep
the response green, give the second channel a quieter green, and keep text
neutral grey/white. Navigation focus and selected plugin states use the
desktop-like blue selection; graph handles remain high contrast and touchable.

The adaptive launcher icon uses the same three-slider mark as the desktop icon,
with the Android-safe inset, charcoal background, neutral rails, and the
EasyEffects response green for the controls. Its monochrome layer repeats that
geometry so themed launchers retain a recognizable mark.

## Behavior

- Dragging a graph point updates the filter frequency and gain; tapping selects
  the band and loads its actual frequency, gain, Q, type, channel, enabled, and
  solo state.
- All visible actions call an implemented model/backend or Android file picker.
- The Plugin/App/About bottom navigation switches between implemented pages;
  it must not duplicate plugin navigation or present placeholder tabs.
- Do not show player controls, genre presets, surround modes, a live spectrum,
  or desktop plugin controls without an implemented Android operation.
- Android vendor effects are identified as system effects and are not named as
  equivalent desktop plugins.

## Qt port decision

The desktop product uses Qt Quick/QML, and a Qt Quick Android port is the target
implementation when the Qt Android kit, Android Qt runtime, and Gradle
integration are available. The current workstation has desktop Qt but no Qt
Android kit (`qt-cmake`, Android Qt libraries, and Android Qt mkspec are absent).
The shipped screen therefore remains the connected Android implementation;
this document is not a claim that QML is packaged. A real port must expose the
C++ backend through a narrow `QObject` facade, replace desktop-only
Kirigami/PipeWire/KConfig services, reuse desktop translations, and prove an
APK build and device run before replacing this screen.

## Acceptance

- Angular EasyEffects dark styling and BOOM-like graph-first interaction.
- Clear gaps between actions; no ambiguous duplicate controls.
- Graph handles follow the response curve and stay touchable.
- All screen content remains inside safe insets and scrolls where required.
- Qt/QML packaging is claimed only after a Qt Android build and runtime check.
- Every exposed audio feature has a working C++ backend mapping and tests; no
  UI label alone counts as plugin parity.
