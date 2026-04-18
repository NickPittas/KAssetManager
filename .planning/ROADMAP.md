# Roadmap

- [ ] **Phase 1: Establish Fedora 43 Wayland App Build Baseline**
- [ ] **Phase 2: Prove tlRender Video And Sequence Playback On Wayland**
- [ ] **Phase 3: Preserve Annotation On Video And Sequences**
- [ ] **Phase 4: Preserve Thumbnail Generation And Scrubbing**
- [x] **Phase 5: Port Linux File Operations And File Manager Reveal**
- [ ] **Phase 6: Verify Project Manager And Batch Rename**
- [ ] **Phase 7: Bundle And Verify Converters**
- [ ] **Phase 8: Package The Fedora 43 Wayland AppImage**
- [ ] **Phase 9: Document Linux/Fedora 43 Wayland Support**

| Phase | Plans | Status | Completed |
|-------|-------|--------|-----------|
| 1     | 0/0   | Complete | 2026-04-10 |
| 2     | 0/0   | Complete | 2026-04-10 |
| 3     | 0/1   | Pending | |
| 4     | 0/0   | Pending | |
| 5     | 0/0   | Complete | 2026-04-14 |
| 6     | 0/1   | Verification Pending | |
| 7     | 0/1   | Verification Pending | |
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
**Plans:** 0/1 plans complete (06-01 active)

Plans:
- [ ] **03-01** Reproduce, fix, and validate fullscreen preview overlay annotation-tool failures and playback UI regressions on Wayland

### Phase 4: Preserve Thumbnail Generation And Scrubbing
**Goal:** Restore thumbnails and scrubbing responsiveness on Linux.
**Requirements:** FEDORA43-THUMBNAILS
**Plans:** 0/0 plans complete

### Phase 5: Port Linux File Operations And File Manager Reveal
**Goal:** Match Linux file manager semantics while preserving app UX.
**Requirements:** FEDORA43-FILEOPS
**Plans:** 0/0 plans complete

Status note: Completed from the current worktree fixes; user-reported working on Linux to the best of available manual verification.

### Phase 6: Verify Project Manager And Batch Rename
**Goal:** Validate project workflows and only fix Linux-specific issues found.
**Requirements:** FEDORA43-PROJECTS
**Plans:** 0/1 plans complete

Status note: The current worktree contains the planned Phase 06 implementation slice (Project Manager batch rename entry point, selected-folder paste/new-folder targeting, refresh-triggered watcher rescan, Linux-sensitive path handling, and watcher/import scope alignment), but manual/user Linux workflow verification is still pending before Phase 06 can be closed.

Plans:
- [ ] **06-01** Add the narrow Project Manager verification slice: PM batch rename, selected-folder destinations, refresh rescan, and focused regression coverage

### Phase 7: Bundle And Verify Converters
**Goal:** Prefer bundled ffmpeg and magick paths in Linux runtime flows.
**Requirements:** FEDORA43-CONVERTERS
**Plans:** 0/1 plans complete

Status note: Plan 07-01 implementation is present in the current worktree, and focused automated converter test coverage is in place (`test_media_converter_worker` with CTest registration), but manual Linux bundled-converter verification is still pending; Phase 06 still awaits manual Linux workflow verification before closure.

Plans:
- [ ] **07-01** Resolve bundled Linux converter lookup first, keep ffprobe aligned with the chosen ffmpeg bundle, and verify the affected conversion flows

### Phase 8: Package The Fedora 43 Wayland AppImage
**Goal:** Produce and validate a Fedora 43 Wayland-capable AppImage.
**Requirements:** FEDORA43-APPIMAGE
**Plans:** 0/0 plans complete

### Phase 9: Document Linux/Fedora 43 Wayland Support
**Goal:** Capture Linux build, validation, and packaging guidance.
**Requirements:** FEDORA43-DOCS
**Plans:** 0/0 plans complete
