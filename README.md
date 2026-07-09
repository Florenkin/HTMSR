# HTMSR

HTMSR is a Qt6 desktop application scaffold for offline stereo calibration, laser centerline extraction, and point-cloud reconstruction.

The `references` directory is kept as read-only business logic reference material. The new implementation lives under `src` and is split into a pure C++ core library and a Qt Widgets application.

## Build

Open the repository folder in Visual Studio 2022 and select one of the CMake presets:

- `vs2022-x64-debug`
- `vs2022-x64-release`

The third-party packages must be discoverable through `CMAKE_PREFIX_PATH`, package manager integration, or your Visual Studio CMake settings:

- Qt6 Widgets and Concurrent
- OpenCV
- Eigen3
- PCL common/io/visualization
- VTK Qt components for the embedded point-cloud viewer

If VTK Qt components are not found, the application still builds with a placeholder point-cloud view.

## Layout

- `src/core`: calibration, laser extraction, reconstruction, point-cloud IO, logging, and shared data contracts.
- `src/app/services`: Qt-facing configuration and log sink services.
- `src/app/ui`: Qt Widgets main window and panels.
- `src/app/acquisition`: extension points for future online acquisition.

## Notes

The first version is an offline processing tool. Online camera support is intentionally represented by interfaces and a placeholder page so that SDK-specific code can be added later without changing the reconstruction core.
