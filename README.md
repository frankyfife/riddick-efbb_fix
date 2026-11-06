# RiddickFix: Escape From Butcher Bay auf modernen Systemen

Drop-in-Fix (`winmm.dll`) für die PC-Fassung von *The Chronicles of Riddick: Escape From Butcher Bay* (2004).
Kein ASI-Loader und keine veränderten Spieldateien. Die DLL wird neben die Engine gelegt und zur Laufzeit geladen.

## Kompatibilität

| | |
|---|---|
| **Getestet mit** | PC-Retail-Fassung (Vivendi Universal, Europa, Englisch/Deutsch/Französisch) mit **Patch 1.1** |
| **Benötigt** | den 64-Bit-Zweig `System\Win64_AMD64\` (gehört zu Patch 1.1 und wird von `Riddick.exe` unter 64-Bit-Windows automatisch gestartet) |
| **Betriebssystem** | Windows 10/11, 64 Bit |
| **Grafik** | jede aktuelle OpenGL-Karte; die SM3-Stufe „2.0++“ wird automatisch nur auf NVIDIA eingeschaltet |
| **Nicht unterstützt** | 32-Bit-Builds (`Win32_x86*`, SecuROM-geschützt; der Kopierschutz wird bewusst nicht angefasst), Version 1.0 ohne 64-Bit-Zweig, *Assault on Dark Athena* (eigene Engine-Version, siehe dort) |

Referenzdateien der getesteten Installation:

| Datei | Größe (Bytes) | SHA-256 (Anfang) |
|---|---|---|
| `Riddick.exe` | 167 936 | `9E44ED5386F4EAB7` |
| `System\Win64_AMD64\SbzEngine.exe` (1.0.0.1) | 965 632 | `5056FFE030C2BA86` |
| `System\Win64_AMD64\MSystem.dll` | 3 876 352 | `B59EB2CC98EA40E9` |
| `System\Win64_AMD64\RndrGL.dll` | 795 648 | `400DC6B50F65C9B8` |
| `Content\GameWorld_Win64_AMD64.dll` | 2 837 504 | `749D018C8DCDED11` |
| `Content\GameClasses_Win64_AMD64.dll` | 3 712 000 | `263C03A430F070EB` |

Alle Code-Patches prüfen vor dem Schreiben die Original-Bytes. Weicht eine Datei ab, wird der betreffende Patch
übersprungen und in `RiddickFix.log` vermerkt, statt das Spiel zum Absturz zu bringen.

## Installation

`winmm.dll` und `RiddickFix.ini` (optional für EAX/Surround zusätzlich `dsound.dll`, `dsoal-aldrv.dll` und
`alsoft.ini`) nach `<Spiel>\System\Win64_AMD64\` kopieren und das Spiel wie gewohnt über
`Riddick.exe` starten.

Entfernen: diese Dateien und den Ordner `RiddickFix_cache` löschen. Die Spieldateien bleiben unverändert.
Ausnahme ist `Environment.cfg`, in die der Fix `VID_MODE`, `LANGUAGE`, `XR_SHADERMODE` und `xr_lodoffset` schreibt;
das Spiel schreibt diese Werte auch selbst.

## Was behoben bzw. ergänzt ist

- **Absturz beim Start:** Die OpenGL-Erweiterungsliste moderner Treiber ist zu lang. Die Engine hielt deshalb
  DXT-Texturen für nicht unterstützt.
- **„Nur ein Viertel des Bildes“ bzw. Fischaugen-Effekt bei hohen Auflösungen:** Der Renderer begrenzte Texturen
  auf 2048 Pixel, dadurch wurden Bildschirmkopien abgeschnitten.
- **Auflösungen:** alle bis 3840×2160, Standard ist die Desktop-Auflösung mit Desktop-Bildrate (z. B. 4K/120 Hz).
  Die Engine sah vorher nur die ersten 512 Anzeigemodi.
- **Xbox-Controller über XInput** in Spiel und Menüs (inklusive Bildschirmtastatur), Belegung wie auf der Xbox.
  Der rechte Stick läuft über den Mauspfad, damit das Umsehen pro Bild flüssig ist.
- **Vibration:** Die Original-Hüllkurven des Spiels steuern beide Motoren an. Die Engine hängte Effekte nur an ein
  DirectInput-Pad, deshalb meldet der Fix das XInput-Pad als aktiv.
- **EAX und 5.1/7.1:** Windows hat seit Vista keine DirectSound-Hardwarestimmen und kein EAX mehr, 3D-Klänge
  werden nur in Stereo verteilt. Mit [DSOAL](https://github.com/kcat/dsoal) und
  [OpenAL Soft](https://github.com/kcat/openal-soft) (`dsound.dll`, `dsoal-aldrv.dll`, `alsoft.ini` neben
  `SbzEngine.exe`) meldet DirectSound wieder 128 Hardware-3D-Stimmen sowie EAX 2/3. Der Fix schaltet EAX dann
  automatisch ein (`[Audio] EAX=auto`). Die Ausgabe folgt der Windows-Lautsprecherkonfiguration (5.1/7.1),
  bei Kopfhörern wird HRTF verwendet. Bauen: `tools\build_audio.ps1`.
- **Windows-HDR:** Unter HDR übergeht der Fix die Gamma-Rampe des Spiels und den Moduswechsel bei nativer
  Auflösung. Beim Beenden, auch mit Alt+F4, stellt er die ursprüngliche Rampe wieder her. Vorher war der
  HDR-Desktop nach dem Spiel verfälscht.
- **Xbox-Knopfsymbole:** farbige Glyphen im Fließtext, Menü-Hinweise wie auf der Konsole, Hilfe im HUD über das
  Steuerkreuz.
- **Seitenverhältnis der 2D-Ebene:** Menüs und HUD sind ein festes 640×480-Raster. Schriften und Knopfsymbole
  werden vorgestaucht erzeugt und erscheinen auf 16:9/21:9 unverzerrt.
- **Grafik:** Shader-Stufe „2.0++“ (SM3-Pfad) auf NVIDIA, Modell-LOD auf höchste Stufe.
- **Sonstiges:** Intro-Logos überspringen, deutsche Texte und Untertitel, Alt+F4 beendet das Spiel.

Alle Optionen sind in [`dist/RiddickFix.ini`](dist/RiddickFix.ini) kommentiert, die deutsche Kurzanleitung steht in
[`dist/LIESMICH.txt`](dist/LIESMICH.txt).

## Bauen

Voraussetzungen: Visual Studio 2022 Build Tools (x64) und Python 3.

```bat
build.cmd
```

Ergebnis: `build\winmm.dll`.

Aufbau:

- `src/`: Proxy und Hooks.
- `tools/`: Generatoren und Testhelfer. Die Testhelfer schreiben nach `%RIDDICKFIX_TESTDIR%` bzw.
  `%TEMP%\RiddickFixTest`.
- `art/`: eigene Knopfgrafiken (LB/RB/LT/RT usw.). A/B/X/Y werden zur Laufzeit aus der `GUI.xtc` des Spiels
  übernommen; das Repo enthält keine Spieldaten.

## Drittkomponenten

`dsound.dll` (DSOAL) und `dsoal-aldrv.dll` (OpenAL Soft) stammen unverändert aus den Quellen von
[kcat/dsoal](https://github.com/kcat/dsoal) und [kcat/openal-soft](https://github.com/kcat/openal-soft).
Beide stehen unter der GNU LGPL 2.1 oder neuer und werden mit `tools\build_audio.ps1` gebaut
(Ergebnis in `build\audio\`).
