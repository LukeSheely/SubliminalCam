# Implementation roadmap

## Focused controller (complete)

- Camera picker, reconnect button, mirror option, live preview, and output toggle
- One editable message with a manual show/hide button
- No background compositor, scheduler, animation, or fade pipeline
- Persistent local settings and deterministic native tests

## Physical-camera pipeline (complete)

- Read physical-camera frames with `IMFSourceReader` (complete)
- Convert camera samples into BGRA preview frames (complete)
- Move capture and message rendering off the UI thread (complete)
- Reuse capture/output buffers, respect camera row stride, and hold the last complete
  virtual-camera frame during shared-memory contention (complete)
- Add structured capture-stage logging, device capability inspection, automatic
  format fallback, hot-plug rescan, and exportable health reports (complete)

## Milestone 3 — Windows virtual camera (implemented; compatibility verification pending)

- Add a Media Foundation `IMFMediaSourceEx`/`IMFMediaStream2` COM DLL (complete)
- Publish a 720p30 RGB32/NV12 stream (complete); add 1080p30 after performance profiling
- Transport processed frames from the current-user controller to Camera Frame Server through a secured named shared-memory channel (complete)
- Register and remove the current-user software camera with `MFCreateVirtualCamera` (complete)
- Emit a stable diagnostic frame if the controller or physical camera is unavailable (complete)

## Release packaging (implemented; certificate and compatibility validation pending)

- Build an Inno Setup `.exe` installer with clean virtual-camera uninstall behavior (complete)
- Add optional Authenticode signing in GitHub Actions (complete; certificate secrets required)
- Verify Zoom, Teams, Discord, Chrome, Edge, and the Windows Camera application
- Publish checksums, third-party notices, and troubleshooting guidance (complete)
