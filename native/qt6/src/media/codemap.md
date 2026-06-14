# native/qt6/src/media/

## Responsibility

Wayland-first video playback and raster presentation for the Qt UI.

## Components

- `player_lab_player.cpp/.h`: compatibility facade used by existing UI call sites; delegates video playback to `FFmpegMovPlayer`.
- `ffmpeg_mov_player.cpp/.h`: active in-process FFmpeg/libav video decoder, playback clock, seek/scrub controller, and Qt Multimedia audio bridge.
- `player_lab_viewport.cpp/.h`: Wayland raster QWidget; pulls `QImage` frames from `PlayerLabPlayer`/`FFmpegMovPlayer` and paints them with `QPainter`.
- `media_player_types.h`: shared media state structs/enums used by the facade and FFmpeg player.

## Active Playback Path

1. `MainWindow` or `PreviewOverlay` owns a `PlayerLabPlayer` facade and a `PlayerLabViewport`.
2. `PlayerLabPlayer::loadMedia()` accepts video files and loads them into `FFmpegMovPlayer`.
3. `FFmpegMovPlayer` opens media with FFmpeg/libav, decodes on its worker thread, converts frames with swscale, and stores the current `QImage`.
4. `PlayerLabViewport` updates on player frame signals/timer ticks, calls `PlayerLabPlayer::getCurrentFrame()`, and paints the current image in `paintEvent()`.

## Integration

- Consumed by: File Manager preview, Project Manager preview, and `PreviewOverlay`.
- Audio: `FFmpegMovPlayer` uses Qt `QMediaPlayer`/`QAudioOutput` for audio playback.
- Build: tlRender may still be present as dependency plumbing for bundled FFmpeg/minizip/OIIO, but it is not an active playback renderer.
