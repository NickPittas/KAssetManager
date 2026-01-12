# tlRender Integration Plan for KAssetManager

## ⚠️ CRITICAL ISSUES IDENTIFIED (2026-01-13)

**Current State: BROKEN - 0.5 FPS playback instead of 24+ FPS**

### Root Cause Analysis

From app.log:
```
[00:24:04.867] currentTimeChanged: frame 1 pos 41 ms  
[00:24:05.867] currentTimeChanged: frame 2 pos 83 ms   <- 1 SECOND between frames!
[00:24:06.868] currentTimeChanged: frame 3 pos 125 ms  <- Should be 41ms apart
```

The ContextObject's 5ms timer is supposed to drive `context->tick()` which advances the player.
Something is blocking or the timer isn't firing correctly.

---

## Architecture Mindmap (CORRECT tlRender Qt Integration)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         tlRender Qt Integration Architecture                 │
└─────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ INITIALIZATION (Once at app startup, BEFORE QApplication)                    │
│                                                                              │
│   ftk::Context::create()                                                     │
│         │                                                                    │
│         ▼                                                                    │
│   tl::qtwidget::init(context, OpenGL_4_1_CoreProfile)                       │
│         │    └── Sets QSurfaceFormat, registers Qt metatypes                │
│         ▼                                                                    │
│   tl::qt::ContextObject(context, qApp)  <── SINGLETON                       │
│         │    └── Internal QTimer (5ms) calls context->tick()                │
│         │    └── This drives ALL player timing!                             │
│         ▼                                                                    │
│   tl::qtwidget::initFonts(context)  (After QApplication created)            │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ MEDIA LOADING (Per file/sequence)                                            │
│                                                                              │
│   ftk::Path(filePath)                                                        │
│         │                                                                    │
│         ▼                                                                    │
│   tl::Timeline::create(context, path, options)                              │
│         │    └── Handles video files, image sequences, .otio                │
│         ▼                                                                    │
│   tl::Player::create(context, timeline, playerOptions)                      │
│         │    └── Native C++ player with threading & caching                 │
│         ▼                                                                    │
│   tl::qt::PlayerObject(context, player)  <── Qt wrapper                     │
│         │    └── Wraps native Player                                        │
│         │    └── Emits Qt signals: currentTimeChanged, playbackChanged, etc │
│         │    └── Has slots: play(), pause(), seek(), setSpeed()             │
│         │                                                                    │
│         │    IMPORTANT: PlayerObject observes Player state via callbacks.   │
│         │    When context->tick() is called, Player advances time,          │
│         │    PlayerObject detects changes and emits Qt signals.             │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ RENDERING (Viewport)                                                         │
│                                                                              │
│   tl::qtwidget::Viewport(context, style)                                    │
│         │    └── QOpenGLWidget subclass                                     │
│         │    └── Internal rendering loop (not timer-based!)                 │
│         │    └── Observes PlayerObject for video frames                     │
│         │                                                                    │
│         ▼                                                                    │
│   viewport->setPlayer(QSharedPointer<PlayerObject>)                         │
│         │    └── Viewport connects to PlayerObject::currentVideoChanged     │
│         │    └── When new video frame available, viewport redraws           │
│         │                                                                    │
│         ▼                                                                    │
│   viewport->setOCIOOptions(ocioOptions)                                     │
│         │    └── Color management applied during GL rendering               │
│                                                                              │
│   CRITICAL: Viewport does NOT have its own player. It USES the shared       │
│   PlayerObject. There should be ONE PlayerObject per media file.            │
└──────────────────────────────────────────────────────────────────────────────┘

┌──────────────────────────────────────────────────────────────────────────────┐
│ DATA FLOW DURING PLAYBACK                                                    │
│                                                                              │
│   QTimer (5ms, in ContextObject)                                            │
│         │                                                                    │
│         ▼                                                                    │
│   context->tick()                                                           │
│         │                                                                    │
│         ▼                                                                    │
│   Player internal thread advances currentTime                               │
│         │                                                                    │
│         ▼                                                                    │
│   PlayerObject observers detect change                                      │
│         │                                                                    │
│         ▼                                                                    │
│   PlayerObject emits currentTimeChanged(RationalTime)                       │
│   PlayerObject emits currentVideoChanged(vector<VideoFrame>)                │
│         │                                                                    │
│         ▼                                                                    │
│   Viewport receives currentVideoChanged                                     │
│         │                                                                    │
│         ▼                                                                    │
│   Viewport calls update() -> paintGL() -> renders frame                     │
└──────────────────────────────────────────────────────────────────────────────┘
```

---

## WHAT WENT WRONG (Previous Implementation)

1. **Created TLRenderPlayer wrapper that duplicates PlayerObject functionality**
   - Has its own QTimer (m_updateTimer) that calls onUpdateTimer()
   - This timer was STOPPED when PlayerObject was created
   - But ContextObject's timer should be the ONLY driver

2. **Possible context tick issue**
   - ContextObject is created with `qApp` as parent
   - But is this object actually running its timer?
   - Need to verify ContextObject is alive and ticking

3. **TLRenderViewport wraps qtwidget::Viewport correctly**
   - But may not be receiving PlayerObject signals properly
   - The shared PlayerObject approach was correct, but timing is broken

4. **Overcomplication**
   - TLRenderPlayer does too much: wraps Player AND PlayerObject AND has its own signals
   - Should just expose PlayerObject directly and let the viewport use it

---

## CORRECT IMPLEMENTATION (Simplified)

### Option A: Minimal Wrapper (Recommended)

```cpp
// TLRenderPlayer should be THIN - just load media and return PlayerObject
class TLRenderPlayer : public QObject {
    static void initialize();  // Call once at startup
    static std::shared_ptr<ftk::Context> sharedContext();
    
    void loadMedia(const QString& path);
    QSharedPointer<tl::qt::PlayerObject> playerObject() const;
    
    // OCIO settings - apply to DisplayOptions, not PlayerObject
    void setOCIOOptions(const tl::OCIOOptions& options);
    
private:
    std::shared_ptr<tl::Timeline> m_timeline;
    std::shared_ptr<tl::Player> m_player;
    QSharedPointer<tl::qt::PlayerObject> m_playerObject;
};

// Usage in PreviewOverlay:
m_tlPlayer->loadMedia(path);
m_viewport->setPlayer(m_tlPlayer->playerObject());

// Playback control - use PlayerObject directly!
m_tlPlayer->playerObject()->forward();   // Play
m_tlPlayer->playerObject()->stop();      // Pause
m_tlPlayer->playerObject()->seek(time);  // Seek
```

### Option B: Remove TLRenderPlayer entirely

Just use tlRender's native classes directly in PreviewOverlay:
- Create Timeline
- Create Player  
- Create PlayerObject
- Pass PlayerObject to Viewport

---

## Overview

Replace GStreamer video playback with tlRender (mrv2's playback engine) to enable full ACES/OpenColorIO color management support.

## User Decisions

| Decision | Choice |
|----------|--------|
| Build approach | (A) Build tlRender from source; fallback to vcpkg if fails |
| GStreamer | (B) Remove entirely - do not keep |
| RAM Preview | (C) Replace with tlRender's native sequence playback |
| Scope | (D) Playback only for now (no export/render) |
| Graphics API | (E) Vulkan preferred; OpenGL 4.1 fallback |

## Build Status ✅

tlRender built successfully on 2026-01-12 with:
- **OpenColorIO 2.5.0** - ACES color management
- **OpenImageIO 3.1.7.0** - Image I/O with OCIO integration
- **FFmpeg** - Video codecs (libavcodec, libavformat, libavdevice)
- **OpenEXR 3.3.6** - EXR format support
- **OpenTimelineIO** - Timeline/OTIO support
- **OpenGL 4.1** backend (ftk_API=GL_4_1)

### Install Location
```
E:\KAssetManager\third_party\tlRender-build\install-Release\
├── bin/
│   ├── tlplay.exe        # Test player
│   └── tlbake.exe        # Batch renderer
├── include/tlRender/     # Headers
├── lib/                  # Static libraries (~48 .lib files)
└── lib/cmake/            # CMake configs for integration
```

### Key Libraries
| Library | Size | Purpose |
|---------|------|---------|
| tlTimeline.lib | 18.8 MB | Player, Timeline, ColorOptions |
| tlGL.lib | 1.9 MB | OpenGL 4.1 rendering |
| tlIO.lib | 13.6 MB | I/O (FFmpeg, OIIO, EXR, WMF) |
| tlCore.lib | 5.2 MB | Core utilities (Audio, Time, URL) |
| tlUI.lib | 14.3 MB | UI widgets (TimelineWidget, Viewport) |
| OpenColorIO.lib | 78.5 MB | ACES color management |
| OpenImageIO.lib | 121.2 MB | Image I/O with OCIO |

---

## Architecture

### Current GStreamer Architecture (TO BE REMOVED)
```
MainWindow
  └── PreviewOverlay
        └── VideoPlayerWidget (QWidget)
              └── GStreamerPlayer
                    └── GStreamer pipeline (playbin)
```

### New tlRender Architecture
```
MainWindow
  └── PreviewOverlay
        └── TLRenderWidget (QOpenGLWidget)
              └── TLRenderPlayer
                    ├── tl::timeline::Player
                    ├── tl::timeline::Timeline
                    ├── tl::gl::Render (OpenGL 4.1)
                    └── OCIO color management
```

---

## Files to Create

### 1. TLRenderPlayer (`native/qt6/src/tlrender_player.h/cpp`)

Qt wrapper around tlRender's `tl::timeline::Player` providing:

**Signals:**
- `positionChanged(qint64 msec)`
- `durationChanged(qint64 msec)`
- `playbackStateChanged(PlaybackState)`
- `volumeChanged(float)`
- `error(QString)`
- `frameReady(const QImage&)` - for thumbnail extraction
- `ocioConfigChanged(QString)`

**Slots:**
- `setSource(QString path)` - Load video/sequence/timeline
- `play()`, `pause()`, `stop()`
- `seek(qint64 msec)`
- `setVolume(float 0.0-1.0)`
- `setPlaybackRate(float)`
- `setLoopMode(LoopMode)` - Once, Loop, PingPong

**OCIO Methods:**
- `setOCIOConfig(QString configPath)`
- `setInputColorspace(QString)`
- `setDisplay(QString)`
- `setView(QString)`
- `getAvailableColorspaces() -> QStringList`
- `getAvailableDisplays() -> QStringList`
- `getAvailableViews(QString display) -> QStringList`

### 2. TLRenderWidget (`native/qt6/src/tlrender_widget.h/cpp`)

QOpenGLWidget subclass for rendering:

**Features:**
- OpenGL 4.1 context management
- Renders frames from TLRenderPlayer
- Handles resize, aspect ratio
- Mouse/keyboard input forwarding
- Overlay support (annotations, HUD)

**Key Methods:**
- `setPlayer(TLRenderPlayer*)`
- `setFitMode(FitMode)` - Fit, Fill, 1:1
- `setBackgroundColor(QColor)`

### 3. OCIO Settings UI (`native/qt6/src/ocio_settings_widget.h/cpp`)

Widget for OCIO configuration:
- Config file selector (QFileDialog)
- Input colorspace dropdown
- Display dropdown
- View dropdown
- Exposure/Gamma sliders (optional)

---

## Files to Modify

### 1. CMakeLists.txt (`native/qt6/CMakeLists.txt`)

```cmake
# Add tlRender
set(TLRENDER_ROOT "${CMAKE_SOURCE_DIR}/../../third_party/tlRender-build/install-Release")
list(APPEND CMAKE_PREFIX_PATH "${TLRENDER_ROOT}")

find_package(tlRender REQUIRED)
find_package(OpenColorIO REQUIRED)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    tlRender::tlTimeline
    tlRender::tlGL
    tlRender::tlIO
    tlRender::tlCore
    OpenColorIO::OpenColorIO
)

# Remove GStreamer
# find_package(GStreamer) - REMOVE
# target_link_libraries(... gstreamer...) - REMOVE
```

### 2. PreviewOverlay (`native/qt6/src/preview_overlay.cpp`)

- Replace `VideoPlayerWidget` with `TLRenderWidget`
- Replace `GStreamerPlayer` with `TLRenderPlayer`
- Add OCIO controls to toolbar/menu
- Update signal/slot connections

### 3. LivePreviewManager (`native/qt6/src/live_preview_manager.cpp`)

- For still images: Use OIIO with OCIO for color-managed thumbnails
- For sequences: Can use tlRender for animated preview
- Update `HAVE_GSTREAMER` guards to `HAVE_TLRENDER`

### 4. SettingsDialog (`native/qt6/src/settings_dialog.cpp`)

Add OCIO settings page:
- Default OCIO config path
- Default input colorspace
- Default display/view
- Per-project override option

### 5. MainWindow (`native/qt6/src/mainwindow.cpp`)

- Update menu items for OCIO controls
- Add View > Color Management submenu
- Keyboard shortcuts for OCIO presets

---

## Files to Remove

### Source Files
- `native/qt6/src/gstreamer_player.h`
- `native/qt6/src/gstreamer_player.cpp`
- `native/qt6/src/video_player_widget.h`
- `native/qt6/src/video_player_widget.cpp`

### Third-Party
- `third_party/gstreamer/` (~500MB of DLLs)

### CMake References
- All `HAVE_GSTREAMER` conditionals
- GStreamer find_package and link commands
- GStreamer DLL deployment in install rules

---

## Implementation Order

### Phase 1: Core Player (Priority: HIGH)
1. Create `tlrender_player.h/cpp` with basic playback
2. Create `tlrender_widget.h/cpp` with OpenGL rendering
3. Update `CMakeLists.txt` to find/link tlRender
4. Test basic video playback

### Phase 2: OCIO Integration (Priority: HIGH)
5. Add OCIO methods to TLRenderPlayer
6. Create OCIO settings widget
7. Wire up OCIO controls in PreviewOverlay
8. Test with ACES config

### Phase 3: Sequence Support (Priority: MEDIUM)
9. Add image sequence loading to TLRenderPlayer
10. Update LivePreviewManager for OCIO thumbnails
11. Replace RAM preview with tlRender playback
12. Test with EXR sequences

### Phase 4: Cleanup (Priority: LOW)
13. Remove all GStreamer code
14. Remove `third_party/gstreamer/`
15. Update documentation
16. Update installer/packaging

---

## API Reference

### tlRender Key Classes

```cpp
// Timeline loading
auto timeline = tl::timeline::Timeline::create(path, context);

// Player creation
tl::timeline::PlayerOptions playerOptions;
playerOptions.cache.readAhead = tl::otime::RationalTime(2.0, 1.0);
playerOptions.cache.readBehind = tl::otime::RationalTime(0.5, 1.0);
auto player = tl::timeline::Player::create(timeline, context, playerOptions);

// Playback control
player->setPlayback(tl::timeline::Playback::Forward);
player->setPlayback(tl::timeline::Playback::Stop);
player->seek(tl::otime::RationalTime(frame, fps));

// OCIO color management
tl::timeline::OCIOOptions ocioOptions;
ocioOptions.enabled = true;
ocioOptions.fileName = "path/to/config.ocio";
ocioOptions.input = "ACES - ACEScg";
ocioOptions.display = "sRGB";
ocioOptions.view = "ACES 1.0 - SDR Video";

tl::timeline::DisplayOptions displayOptions;
displayOptions.ocio = ocioOptions;
player->setDisplayOptions({displayOptions});

// OpenGL rendering
auto render = tl::gl::Render::create(context);
render->begin(size);
render->drawVideo({videoData}, {box}, {displayOptions});
render->end();
```

### ACES Config (Bundled)
```
E:\KAssetManager\OpenColorIO-Config-ACES-1.2\aces_1.2\config.ocio
```

Common colorspaces:
- Input: `ACES - ACEScg`, `ACES - ACEScct`, `Utility - Linear - sRGB`
- Display: `sRGB`, `Rec.709`, `P3-D65`
- View: `ACES 1.0 - SDR Video`, `Un-tone-mapped`, `Raw`

---

## Testing Checklist

- [ ] Video playback (MP4, MOV, MKV)
- [ ] Image sequence playback (EXR, PNG, JPEG)
- [ ] Audio playback and sync
- [ ] Seek/scrub performance
- [ ] OCIO config loading
- [ ] Colorspace selection
- [ ] Display/View selection
- [ ] Exposure/Gamma adjustment
- [ ] Loop modes (Once, Loop, PingPong)
- [ ] Playback rate adjustment
- [ ] Frame-accurate seeking
- [ ] Thumbnail generation with OCIO
- [ ] Memory usage under load
- [ ] Multi-monitor support

---

## Risk Mitigation

| Risk | Mitigation |
|------|------------|
| OpenGL compatibility | Test on various GPUs; OpenGL 4.1 is widely supported |
| Large dependency size | Static linking reduces DLL count; use `-DBUILD_SHARED_LIBS=OFF` |
| OCIO config complexity | Ship bundled ACES 1.2 config; allow custom configs |
| Performance regression | Profile early; tlRender is optimized for playback |
| Qt OpenGL conflicts | Use dedicated QOpenGLWidget; manage context carefully |

---

## References

- tlRender GitHub: https://github.com/darbyjohnston/tlRender
- mrv2 GitHub: https://github.com/ggarra13/mrv2
- OpenColorIO: https://opencolorio.org/
- ACES: https://acescentral.com/
- Qt OpenGL: https://doc.qt.io/qt-6/qopenglwidget.html
