# Roadmap

- [x] **Phase 1: Establish Fedora 43 Wayland App Build Baseline**
- [x] **Phase 2: Prove tlRender Video And Sequence Playback On Wayland**
- [x] **Phase 3: Preserve Annotation On Video And Sequences**
- [x] **Phase 4: Preserve Thumbnail Generation And Scrubbing**
- [x] **Phase 5: Port Linux File Operations And File Manager Reveal**
- [x] **Phase 6: Verify Project Manager And Batch Rename**
- [x] **Phase 7: Bundle And Verify Converters**
- [ ] **Phase 8: Package The Fedora 43 Wayland AppImage**
- [ ] **Phase 9: Document Linux/Fedora 43 Wayland Support**

| Phase | Plans | Status | Completed |
|-------|-------|--------|-----------|
| 1     | 0/0   | Complete | 2026-04-10 |
| 2     | 0/0   | Complete | 2026-04-10 |
| 3     | 1/1   | Complete | 2026-04-18 |
| 4     | 0/0   | Complete | 2026-04-18 |
| 5     | 0/0   | Complete | 2026-04-14 |
| 6     | 1/1   | Complete | 2026-04-18 |
| 7     | 1/1   | Complete | 2026-04-18 |
| 8     | 0/0   | Pending | |
| 9     | 0/0   | Pending | |

### Phase 1: Establish Fedora 43 Wayland App Build Baseline
**Goal:** Enable Linux-safe configure/build/startup on Fedora 43 Wayland.
**Requirements:** FEDORA43-BASELINE
**Plans:** 0/0 plans complete

### Phase 2: Prove tlRender Video And Sequence Playback On Wayland
**Goal:** Restore stable video and sequence playback on Fedora 43 Wayland.
**Requirements:** FEDORA43-PLAYBACK
**Plans:** 0/0 plans complete

### Phase 3: Preserve Annotation On Video And Sequences
**Goal:** Restore fullscreen preview overlay annotation behavior, playback UI correctness, frame sync, and exports for both video and image sequences under Wayland.
**Requirements:** FEDORA43-ANNOTATION
**Plans:** 1/1 plans complete

Status note: User runtime verification accepted fullscreen/video annotation behavior, playback UI state, and annotation export on Linux after the focused Wayland preview/annotation fixes landed.

Plans:
- [x] **03-01** Reproduce, fix, and validate fullscreen preview overlay annotation-tool failures and playback UI regressions on Wayland

### Phase 4: Preserve Thumbnail Generation And Scrubbing
**Goal:** Restore thumbnails and scrubbing responsiveness on Linux.
**Requirements:** FEDORA43-THUMBNAILS
**Plans:** 0/0 plans complete

Status note: User runtime verification accepted thumbnails and scrubbing responsiveness on Linux.

### Phase 5: Port Linux File Operations And File Manager Reveal
**Goal:** Match Linux file manager semantics while preserving app UX.
**Requirements:** FEDORA43-FILEOPS
**Plans:** 0/0 plans complete

Status note: Completed from the current worktree fixes; user-reported working on Linux to the best of available manual verification.

### Phase 6: Verify Project Manager And Batch Rename
**Goal:** Validate project workflows and only fix Linux-specific issues found.
**Requirements:** FEDORA43-PROJECTS
**Plans:** 1/1 plans complete

Status note: User runtime verification accepted Project Manager and Batch Rename behavior on Linux after the focused verification/fix slice landed.

Plans:
- [x] **06-01** Add the narrow Project Manager verification slice: PM batch rename, selected-folder destinations, refresh rescan, and focused regression coverage

### Phase 7: Bundle And Verify Converters
**Goal:** Prefer bundled ffmpeg and magick paths in Linux runtime flows.
**Requirements:** FEDORA43-CONVERTERS
**Plans:** 1/1 plans complete

Status note: User runtime verification accepted bundled converter behavior on Linux, and focused automated converter coverage remains in place (`test_media_converter_worker` with CTest registration).

Plans:
- [x] **07-01** Resolve bundled Linux converter lookup first, keep ffprobe aligned with the chosen ffmpeg bundle, and verify the affected conversion flows

### Phase 8: Package The Fedora 43 Wayland AppImage
**Goal:** Produce and validate a Fedora 43 Wayland-capable AppImage.
**Requirements:** FEDORA43-APPIMAGE
**Plans:** 0/0 plans complete

### Phase 9: Document Linux/Fedora 43 Wayland Support
**Goal:** Capture Linux build, validation, and packaging guidance.
**Requirements:** FEDORA43-DOCS
**Plans:** 0/0 plans complete
