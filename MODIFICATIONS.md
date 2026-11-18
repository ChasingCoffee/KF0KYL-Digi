# KF0KYL Digi

KF0KYL Digi is a modified version of **WSJT-X 3.0.2** by Joe Taylor, K1JT, and
the WSJT Development Team. It is maintained by Carson Coordes, KF0KYL, and is
**not** the official WSJT-X or endorsed by its authors. Please send problem
reports about this version to KF0KYL, not to the WSJT-X support groups.

Like WSJT-X, KF0KYL Digi is free software licensed under the GNU General
Public License, version 3 (see [COPYING](COPYING)). Its complete source code
is available at https://github.com/ccoo26/KF0KYL-Digi. In line with the
WSJT trademark policy ([TRADEMARK.md](TRADEMARK.md)), it uses its own name,
icon and version numbers.

## Changes from WSJT-X 3.0.2

| Date | Change |
|------|--------|
| 2026-11-11 | Windows 11 style light and dark themes (Fusion style, Fluent colours, dark title bar) |
| 2026-11-11 | Waterfall embedded in the main window, with an option to detach it |
| 2026-11-11 | Windows build moved from MSYS2 MINGW64 to UCRT64; Hamlib build fix |
| 2026-11-12 | Automatic upload of logged QSOs to the QRZ.com Logbook, with duplicate protection and offline queue |
| 2026-11-13 | Callsign info strip: name, location, photo and QRZ page link for the DX station (QRZ.com XML, FCC data for US calls) |
| 2026-11-13 | Version 1.0.0: renamed to KF0KYL Digi with its own icon; settings and log are copied from WSJT-X on first run |
| 2026-11-13 | Version 1.0.1: first-run copy of WSJT-X settings now works on Windows |
| 2026-11-14 | Version 1.0.2: shorter window title (WSJT-X credit stays in the About box and splash screen) |
| 2026-11-14 | Version 1.1.0: View > Layout with Classic, Big Band Activity and Wide Screen arrangements, visible resize grips, Reset Layout; waterfall Fit option keeps the whole band visible; frequency and clock colour settings; new logo and program icon |
| 2026-11-17 | Version 1.2.0: seven colour schemes with Digi Green as the default for new installs, refreshed look throughout; Conditions window with NOAA space weather, NASA SDO Sun, NCDXF beacons and a zoomable world map (gray line, aurora, rain, cities, grids, heard stations, double-click to call); built-in VOACAP (voacapl) point-to-point chart and band map; SDR and 3D waterfall styles and new palettes; Max tries limit beside each Tx message; text size settings; windows kept on screen |

The full history of every change is in the Git commit log.

## Internal names

Program files keep their WSJT-X names (`wsjtx`, `jt9`, `wsprd`, ...) so the
build and packaging scripts stay compatible. Programs that talk to it over
UDP (JTAlert, GridTracker, loggers) see the name "KF0KYL Digi" with the
WSJT-X version it is based on (3.0.2), so their compatibility checks work.
