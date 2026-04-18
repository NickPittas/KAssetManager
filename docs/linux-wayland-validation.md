# Fedora 43 Wayland Validation

## Baseline

KAsset Manager is validated on Fedora 43 KDE Wayland as the current Linux baseline.

Validated areas from the current Linux worktree/runtime checks:

- video playback on Wayland, including demanding MOV files, within small acceptable variance
- fullscreen/video annotation workflows
- annotation export
- thumbnail generation and scrubbing
- Project Manager workflows
- Batch Rename
- bundled converter runtime behavior
- AppImage startup past database initialization using per-user writable data paths

Current not-yet-closed Linux items:

- final AppImage feature smoke pass by the user in a normal Wayland session
- Linux/AppImage documentation polish
- OCIO/color controls on the non-native video playback paths

## Playback Validation Corpus

The local playback validation corpus used during this Linux/Wayland port is:

```text
/mnt/ssd2/Tests/Videos/
```

Representative validated files include:

- `Atmosphere-019.mov`
- `Shot_0140_v005.mov`
- `Cut1GRAPHICS.mov`
- `Ns Ethereal 1.mp4`
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`

Accepted playback result policy for this port:

- small FPS variance from nominal playback rate is acceptable when it is within practical margin of error
- example accepted results from the current worktree include:
  - `Shot_0140_v005.mov`: about `23.45 fps`
  - `Atmosphere-019.mov`: about `23.38 fps`
  - `Cut1GRAPHICS.mov`: about `29.50 fps`
  - `Ns Ethereal 1.mp4`: about `22.98 fps`

## AppImage Notes

The current packaging path produces:

```text
KAssetManager-x86_64.AppImage
```

Key packaging/runtime notes:

- AppDir staging is built by `scripts/build-linux-appimage.sh`
- AppImage packaging is driven by `scripts/package-appimage.sh`
- official `appimagetool` and `linuxdeploy` AppImages can be used locally when host packages are unavailable
- packaged/AppImage runtime data must use a writable per-user Qt data location rather than the mounted AppImage path

## Smoke Commands

Build/install AppDir:

```bash
./scripts/build-linux-appimage.sh
```

Package AppImage:

```bash
./scripts/package-appimage.sh
```

Run AppImage:

```bash
chmod +x ./KAssetManager-x86_64.AppImage
./KAssetManager-x86_64.AppImage
```

## Known Remaining Linux Gap

The main known Linux/video gap after playback recovery is:

- OCIO/color controls are not fully implemented on the non-native video playback paths used by the current Linux/Wayland runtime

This should be treated as follow-up work, not as part of the accepted playback recovery result.
