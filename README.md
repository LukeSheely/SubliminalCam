# SubliminalCam

SubliminalCam is a Windows 11 camera-effects project for video calls.

This repository does not implement covert or below-awareness messaging.

## Current status

The first development milestone is implemented:

- OBS-inspired Qt 6 studio controller with resizable Scenes, Sources, Program, and Inspector panes
- Physical-camera enumeration through Media Foundation
- Live full-frame blur, solid/gradient presets, custom PNG/JPEG backgrounds, portrait-mask composition primitives, and mirroring
- Local settings persistence
- Native unit tests and CI configuration

The controller publishes its processed frames to a Windows Camera Frame Server media-source DLL. `SubliminalCamVcamCtl.exe` and the installer register that source as a selectable Windows virtual camera.

## Requirements

- Windows 11 22H2 (build 22621) or newer
- Visual Studio 2022 or newer with **Desktop development with C++**
- Windows SDK 10.0.22621 or newer
- CMake 3.24 or newer
- Qt 6.5 or newer with the MSVC 2022 x64 kit

## Build

From a Developer PowerShell:

```powershell
cmake -S . -B build -A x64
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The controller will be at `build/Release/SubliminalCam.exe`.

Build the Media Foundation camera component first with `scripts\build-vcam.cmd`. The GitHub workflow also produces `SubliminalCamSetup-x64.exe`, which installs the binaries and registers the camera. Manual registration requires an elevated terminal:

```powershell
SubliminalCamVcamCtl.exe install .\VirtualCameraMediaSource.dll
```

Remove it with `SubliminalCamVcamCtl.exe remove`.

## Privacy and safety

- Frames and messages are processed locally.
- No recording, telemetry, analytics, or network calls are present.

## License

Original project code is distributed under the MIT License. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before adding runtimes or segmentation models.
