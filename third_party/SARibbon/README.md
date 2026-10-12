# SARibbon 2.5.5

These amalgamated sources were copied from `C:/Project/UWScan2`:

- `include/mainwindow/SARibbon.h`
- `src/mainwindow/SARibbon.cpp`

Upstream: https://github.com/czyt1988/SARibbon

The only source adjustment is the implementation's include path (`SARibbon.h`).
HTMSR builds a static target against its selected Qt version, using the bundled
SAFramelessHelper (`SARIBBON_USE_3RDPARTY_FRAMELESSHELPER=0`). No UWScan2 binaries
or QWindowKit DLLs are required. Built-in themes and window assets are embedded
in the amalgamation. HTMSR's own icons and styling live in `src/app/resources`.

The upstream MIT license is included in `LICENSE` and copied to the application
runtime's `licenses/SARibbon-LICENSE.txt` on build.
