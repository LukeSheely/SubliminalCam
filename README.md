# SubliminalCam

SubliminalCam is a small Windows 11 virtual camera for video calls. It passes a
physical camera through and lets the operator show or hide one visible text
message with a button.

This repository does not implement timed, fading, covert, or below-awareness messaging.

## Current status

The focused application is implemented:

- Simple Qt 6 controller with a large live preview
- Physical-camera enumeration through Media Foundation
- One message field and an immediate **Show message / Hide message** toggle
- Optional camera mirroring; background effects are intentionally left to video-call software
- Local settings persistence
- Native unit tests and CI configuration
- Explicit start/stop output control with a stable offline slate
- Live diagnostics with capture-stage HRESULTs, frame/dropout counters, negotiated
  camera modes, transport health, rotating local logs, and report export
- Camera hot-plug rescan plus automatic 720p/480p and 30/15 FPS format fallback

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

Generate a read-only diagnostic report even when the controller cannot start:

```powershell
SubliminalCamVcamCtl.exe diagnose .\SubliminalCam-diagnostics.txt
SubliminalCamVcamCtl.exe probe
```

`probe` runs a bounded physical-camera capture test and reports the exact stage,
negotiated format, frames received, empty stream ticks, and copy failures.

See [TROUBLESHOOTING.md](TROUBLESHOOTING.md) for camera flicker, stale DLL, and app-discovery checks.

CI signing is optional. Configure the repository secrets
`WINDOWS_SIGNING_CERTIFICATE_BASE64` (a base64-encoded PFX) and
`WINDOWS_SIGNING_CERTIFICATE_PASSWORD` to sign the application binaries and installer.

## Privacy and safety

- Frames and messages are processed locally.
- No recording, telemetry, analytics, or network calls are present.

## License

Original project code is distributed under the MIT License. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) before adding runtimes or segmentation models.
