# RiddickFix: Escape From Butcher Bay on modern systems

Drop-in fix (`winmm.dll`) for the PC release of *The Chronicles of Riddick: Escape From Butcher Bay* (2004).
No ASI loader and no modified game files: the DLL sits next to the engine and is loaded at run time.

*Deutsche Fassung: [LIESMICH.md](LIESMICH.md)*

**Download:** ready-built packages are on the [Releases](https://github.com/frankyfife/riddick-efbb_fix/releases) page.

## Compatibility

| | |
|---|---|
| **Tested with** | PC retail release (Vivendi Universal, Europe, English/German/French) with **patch 1.1** |
| **Requires** | the 64-bit branch `System\Win64_AMD64\` (part of patch 1.1; `Riddick.exe` starts it automatically on 64-bit Windows) |
| **Operating system** | Windows 10/11, 64-bit |
| **Graphics** | any current OpenGL card; shader level "2.0++" (SM3) is switched on automatically on NVIDIA only |
| **Not supported** | 32-bit builds (`Win32_x86*`, SecuROM-protected; the copy protection is deliberately left alone), version 1.0 without the 64-bit branch, *Assault on Dark Athena* (different engine version, see [riddick-aoda-toolkit](https://github.com/frankyfife/riddick-aoda-toolkit)) |

Reference files of the tested installation:

| File | Size (bytes) | SHA-256 (first 16) |
|---|---|---|
| `Riddick.exe` | 167 936 | `9E44ED5386F4EAB7` |
| `System\Win64_AMD64\SbzEngine.exe` (1.0.0.1) | 965 632 | `5056FFE030C2BA86` |
| `System\Win64_AMD64\MSystem.dll` | 3 876 352 | `B59EB2CC98EA40E9` |
| `System\Win64_AMD64\RndrGL.dll` | 795 648 | `400DC6B50F65C9B8` |
| `Content\GameWorld_Win64_AMD64.dll` | 2 837 504 | `749D018C8DCDED11` |
| `Content\GameClasses_Win64_AMD64.dll` | 3 712 000 | `263C03A430F070EB` |

Every code patch checks the original bytes before writing. If a file differs, that patch is skipped and noted
in `RiddickFix.log` instead of crashing the game.

## Installation

Copy `winmm.dll` and `RiddickFix.ini` (for EAX/surround also `dsound.dll`, `dsoal-aldrv.dll` and `alsoft.ini`)
to `<Game>\System\Win64_AMD64\` and start the game as usual via `Riddick.exe`.

To remove it, delete those files and the `RiddickFix_cache` folder. The game files stay untouched. The one
exception is `Environment.cfg`, where the fix writes `VID_MODE`, `LANGUAGE`, `XR_SHADERMODE` and `xr_lodoffset` -
values the game writes itself as well.

## What is fixed or added

- **Crash on start:** the OpenGL extension string of current drivers is too long, so the engine concluded that
  DXT textures were unsupported.
- **"Only a quarter of the image" / fisheye look at high resolutions:** the renderer capped textures at 2048
  pixels, which cut off its screen copies.
- **Resolutions:** everything up to 3840×2160; the default is the desktop resolution and refresh rate
  (e.g. 4K/120 Hz). The engine used to see only the first 512 display modes.
- **Xbox controller via XInput** in game and menus (including the on-screen keyboard), mapped like the Xbox
  version. The right stick goes through the mouse path, so looking around is smooth per frame.
- **Vibration:** the PC release has rumble playback compiled out. The game reads the effect name and discards it,
  and no PC module loads `Content/Feedback/feedback.xrg` (the 51 envelopes of the Xbox version). The fix reads
  the curves itself, catches the effect name and drives both motors (channel 0 left, channel 1 right). The
  in-game "Vibration" option still applies.
- **EAX and 5.1/7.1:** since Vista Windows has no DirectSound hardware voices and no EAX, so 3D sounds are only
  spread in stereo. With [DSOAL](https://github.com/kcat/dsoal) and
  [OpenAL Soft](https://github.com/kcat/openal-soft) (`dsound.dll`, `dsoal-aldrv.dll`, `alsoft.ini` next to
  `SbzEngine.exe`) DirectSound reports 128 hardware 3D voices and EAX 2/3 again, and the fix switches EAX on
  automatically (`[Audio] EAX=auto`). Output follows the Windows speaker setup (5.1/7.1); headphones get HRTF.
  Build: `tools\build_audio.ps1`.
- **Windows HDR:** with HDR on, the fix skips the game's gamma ramp and the mode switch at native resolution, and
  restores the original ramp on exit (Alt+F4 included). The HDR desktop used to stay discoloured after playing.
- **Xbox button icons:** coloured glyphs in running text, console-style menu hints, help in the HUD on the d-pad.
- **2D aspect ratio:** menus and HUD are a fixed 640×480 canvas. Fonts and button icons are generated
  pre-squeezed and come out undistorted on 16:9/21:9.
- **Graphics:** shader level "2.0++" (SM3 path) on NVIDIA, model LOD at the highest level.
- **Other:** skip the intro logos, German texts and subtitles, Alt+F4 quits cleanly through the engine's `quit`
  console command.

All options are commented in [`dist/RiddickFix.ini`](dist/RiddickFix.ini); a short German guide is in
[`dist/LIESMICH.txt`](dist/LIESMICH.txt).

## Building

Requirements: Visual Studio 2022 Build Tools (x64) and Python 3.

```bat
build.cmd
```

Output: `build\winmm.dll`.

Layout:

- `src/`: proxy and hooks.
- `tools/`: generators and test helpers. The test helpers write to `%RIDDICKFIX_TESTDIR%` or
  `%TEMP%\RiddickFixTest`.
- `art/`: own button artwork (LB/RB/LT/RT etc.). A/B/X/Y are taken from the game's `GUI.xtc` at run time;
  the repository contains no game data.

## Third-party components

`dsound.dll` (DSOAL) and `dsoal-aldrv.dll` (OpenAL Soft) are built unmodified from the sources of
[kcat/dsoal](https://github.com/kcat/dsoal) and [kcat/openal-soft](https://github.com/kcat/openal-soft).
Both are licensed under the GNU LGPL 2.1 or later and are built with `tools\build_audio.ps1`
(output in `build\audio\`). The release package `RiddickFix-*-EAX-DSOAL.zip` contains the licence texts and the
exact upstream commits.
