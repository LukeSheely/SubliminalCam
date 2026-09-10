# Implementation roadmap

## Milestone 1 — controller and processing core (complete)

- Native controller window, camera enumeration, settings, prompt controls, and live local preview
- CPU frame compositor and deterministic unit tests
- Consent guardrails enforced in the configuration and scheduler layers

## Milestone 2 — physical-camera effects (substantially complete)

- Read physical-camera frames with `IMFSourceReader` (complete)
- Convert camera samples into BGRA preview frames (complete)
- Move capture and composition off the UI thread
- Add PNG/JPEG background decoding through Windows Imaging Component (complete)

## Milestone 3 — Windows virtual camera (implemented; compatibility verification pending)

- Add a Media Foundation `IMFMediaSourceEx`/`IMFMediaStream2` COM DLL (complete)
- Publish a 720p30 RGB32/NV12 stream (complete); add 1080p30 after performance profiling
- Transport processed frames from the current-user controller to Camera Frame Server through a secured named shared-memory channel (complete)
- Register and remove the current-user software camera with `MFCreateVirtualCamera` (complete)
- Emit a diagnostic frame if the controller or physical camera is unavailable

## Milestone 4 — portrait effects

- Add ONNX Runtime DirectML and an appropriately licensed portrait-matting model
- Perform temporal mask smoothing and edge feathering
- Apply blur or replacement only outside the person mask
- Retain CPU fallback and automatically select 720p when processing cannot sustain 30 FPS

## Milestone 5 — release packaging (implemented; release signing pending)

- Build an Inno Setup `.exe` installer with clean virtual-camera uninstall behavior (complete)
- Add optional Authenticode signing in GitHub Actions
- Verify Zoom, Teams, Discord, Chrome, Edge, and the Windows Camera application
- Publish checksums, third-party notices, and troubleshooting guidance
