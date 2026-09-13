# Troubleshooting

## Start with a diagnostic report

Open **Diagnostics** in the controller. The assessment identifies the failing
boundary and includes the Media Foundation HRESULT, negotiated mode, received
frames, copy errors, empty stream ticks, last-frame age, processing time, and
shared-memory publish counts. Reports contain device identifiers but never frame
contents.

If the controller cannot start, run:

```powershell
SubliminalCamVcamCtl.exe diagnose .\SubliminalCam-diagnostics.txt
SubliminalCamVcamCtl.exe probe
```

The rotating application log is stored at
`%LOCALAPPDATA%\SubliminalCam\SubliminalCam.log` and is capped at approximately
2 MB plus one previous log.

## The virtual camera flickers or briefly shows the offline slate

1. Install the newest build, then completely exit the video-call application.
   Camera Frame Server can retain an older media-source DLL until every camera
   consumer has closed.
2. Start SubliminalCam before selecting **SubliminalCam** in the call application.
3. Keep the Program meter near 30 FPS. Full-frame blur is the most expensive
   current effect; reduce its strength if processing time approaches 33 ms.
4. Avoid opening the same physical camera directly in a second application. Many
   webcams and drivers cannot serve two independent Media Foundation readers.
5. If the picture still alternates between two images, remove and reinstall the
   virtual camera from an elevated terminal, then restart Windows:

   ```powershell
   SubliminalCamVcamCtl.exe remove
   SubliminalCamVcamCtl.exe install .\VirtualCameraMediaSource.dll
   ```

The media source deliberately holds the last complete frame during short
shared-memory collisions. After output has been unavailable for two seconds, it
switches once to a static offline slate.

If a camera is connected after the controller starts, press **Refresh** beside
the camera picker. Capture prioritizes the widely supported native 640×480 at
30 FPS path, then falls back through 360p, 720p, and 15 FPS modes when a driver
rejects a format.

## The image is torn, upside down, or has diagonal corruption

Update to the latest build. The controller copies Media Foundation frames using
the buffer's reported row stride, including negative stride, instead of assuming
that every camera returns tightly packed top-down rows. If one camera still fails,
include its exact model and driver version in a bug report.

## SubliminalCam is missing from the camera list

- Windows 11 build 22621 or newer is required.
- Run the installer, or register the camera from an elevated terminal.
- Fully restart the call application after installation.
- In browsers, reload the page and re-open its camera picker.
- Check Windows **Privacy & security → Camera** and allow camera access for
  desktop applications.

## The controller preview is slow

- Use 1280 × 720 output and reduce blur strength.
- Close other software using the physical camera.
- Update the GPU and camera drivers.
- Check the Program meter: consistent processing above 33 ms cannot sustain
  30 FPS and will look like stutter even when the transport itself is stable.

## Background replacement

The current presets are full-frame effects. Person-aware portrait blur and
replacement remain planned because they require a redistributable segmentation
model or compatible Windows Studio Effects hardware. The app does not silently
download a model or upload frames.
