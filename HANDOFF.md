# HANDOFF

## Purpose
This handoff is for the next agent/harness to continue the project without rereading large parts of the repo or repeating the same failed investigation loops.

The user explicitly asked for:
- minimal rereads
- no looping on the same files
- bounded patch/build/test cycles only
- use of the exact external media fixture path already provided
- no broad filesystem searches outside the explicitly provided scope

Current date context from the session: `2026-04-18`.

---

## Critical workflow constraints

The next agent must follow these rules immediately:

1. Read `AGENTS.md` first at task start.
2. Read `.agent-workflow.md` at task start.
3. Do **not** broad-search storage paths. The only approved fixture path used in this playback investigation is:
   - `/mnt/ssd2/Tests/Videos/`
4. Avoid repeated rereads of the same files unless a build/test failure points to them.
5. Every cycle must end in one of:
   - code patch
   - build/test result
   - blocker with exact evidence
6. Revert failed fixes immediately.
7. Use subagents for research/inspection as much as possible. Main-thread work should be patch/build/report.
8. The user specifically complained about loops and repeated reading. Do not repeat that behavior.

The user also created a persistent execution contract file:
- `.agent-workflow.md`

---

## High-level project status

### Verified passing from user runtime checks
These were manually verified by the user in the built app:

- Annotation on video: pass
- Annotation export: pass
- Thumbnails/scrubbing: pass
- Project Manager: pass
- Batch Rename: pass
- Bundled converters: pass
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`: pass

### Remaining failing runtime cases
The remaining practical blockers are now narrowed to **video playback only**:

1. `Atmosphere-019.mov`
   - fail
   - user reported it does not play correctly
   - later verification said side preview is also not working, same as full preview

2. `Shot_0140_v005.mov`
   - opens/plays but playback is still very choppy
   - user estimated roughly 7–15 fps
   - this is not acceptable

### Phases effectively verified/passed by user
These still need planning/state updates, but user runtime verification says they are practically good enough:

- Phase 3 (annotation-related runtime checks): mostly passed
- Phase 4: thumbnails/scrubbing passed
- Phase 6: Project Manager passed; Batch Rename passed after fix
- Phase 7: bundled converters passed

### Phases still open beyond playback
- Phase 8: AppImage build/runtime verification not completed
- Phase 9: docs not completed

---

## Planning-layer status
Planning files were updated earlier in the session to reset Phase 03 around the real fullscreen/annotation defects. The exact planning docs involved were:

- `.planning/REQUIREMENTS.md`
- `.planning/ROADMAP.md`
- `.planning/STATE.md`
- `.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-PLAN.md`

Current practical state from user verification:
- Batch Rename: pass
- Annotation: pass
- Export: pass
- Thumbnails/scrubbing: pass
- Bundled converters: pass
- Remaining blocker: MOV playback

The next agent should align planning state only after playback is resolved.

---

## What was fixed earlier and verified by the user

These fixes were implemented earlier in the session and are **not** the current blocker:

### Preview / annotation / UI fixes
- Wrong center overlay / nav arrows issue fixed
- Select tool state bug fixed
- Annotation geometry/release update fixes added
- First annotation-session initialization fixed
- Timeline reset after switching to shorter media fixed
- Controls/zoom bug fixed
- Controls set to always visible
- Annotation on video now works
- Annotation export now works

Key source areas touched earlier included:
- `native/qt6/src/preview_overlay.cpp`
- `native/qt6/src/preview_overlay.h`
- `native/qt6/src/annotation_layer.cpp`
- `native/qt6/src/annotation_items.h`
- `native/qt6/src/annotation_items.cpp`

These are not the current investigation target unless a new regression appears.

### Batch Rename fix
Batch Rename was failing earlier and was later reported by the user as passing.

The fix was applied in:
- `native/qt6/src/bulk_rename_dialog.cpp`

Intent of the fix:
- repair broken file-rename execution path
- add destination-path conflict detection in preview so collisions are caught before apply

User later verified:
- Batch Rename: pass

Do not reopen Batch Rename unless the user reports a new failure.

---

## Playback investigation summary

### Exact external fixture root to use
Only this fixture folder should be used for playback validation:
- `/mnt/ssd2/Tests/Videos/`

Representative files used during the session:
- `/mnt/ssd2/Tests/Videos/Atmosphere-019.mov`
- `/mnt/ssd2/Tests/Videos/Blood_Hit_03.mov`
- `/mnt/ssd2/Tests/Videos/MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
- `/mnt/ssd2/Tests/Videos/Sunshine 10sec Full Comp v3.mov`
- `/mnt/ssd2/Tests/Videos/Shot_0140_v005.mov`

### Important codec notes already observed in the session
From `ffprobe` runs earlier in the session:
- `Blood_Hit_03.mov`
  - codec: png
  - pixel format: rgba
  - likely alpha-heavy stress case
- `Sunshine 10sec Full Comp v3.mov`
  - codec: prores
  - pixel format: yuv422p10le
  - 25 fps validation target
- `Atmosphere-019.mov`
  - codec: prores
  - pixel format: yuv422p10le
  - large/high-resolution stress case
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
  - codec: hevc
  - pixel format: yuv420p
  - now passes in user runtime

### Playback harness implemented
A real playback validation harness was added and is important context for the next agent.

Files added/updated for harnessing:
- `native/qt6/tests/test_tlrender_playback_harness.cpp`
- `native/qt6/tests/CMakeLists.txt`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/tlrender_viewport.cpp`

Harness purpose:
- measure viewer-side rendered frame progression
- avoid relying only on timeline/play/pause state
- validate real playback behavior on the external MOV fixtures

### Important harness findings from earlier passes
These findings drove the later playback investigation:

1. The initial harness version was invalid because it failed on files the app could play.
2. The corrected harness later produced useful results.
3. For `Sunshine 10sec Full Comp v3.mov`, viewer-side distinct frame cadence under the Wayland raster path was below target.
4. Telemetry added earlier showed repeated fallback extraction and unsupported cached image type handling on some paths.
5. `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov` eventually became a pass in user runtime.
6. `Atmosphere-019.mov` and `Shot_0140_v005.mov` remained the real blockers.

### Important tlRender image-type insight already established
One earlier investigation identified `lastCachedImageType = 26` and mapped it to:
- `ftk::ImageType::YUV_422P_U16`

This was relevant for ProRes 10-bit fallback/conversion work.

### Current conclusion after user feedback
Despite various fallback/conversion experiments, the only files still materially blocking the product are:
- `Atmosphere-019.mov`
- `Shot_0140_v005.mov`

The rest of the playback list is either passing or no longer the priority.

---

## Very important architectural conclusion from the late-session debugging

The user forced a crucial clarification that should guide the next agent:

### mpv is intended for video, but is not initializing in the current runtime
User-provided runtime log showed:
- `useMpv=1`
- `mpvAvailable=0`
- repeated message: `Non-C locale detected. This is not supported. Call 'setlocale(LC_NUMERIC, "C");' in your code.`

This means:
- video files are intended to use mpv when available
- but mpv is failing to initialize in the current runtime
- therefore playback falls back to the non-mpv tlRender path

### Consequence
For the user’s current runtime:
- the failing MOV playback behavior is currently happening on the **tlRender fallback path**, not on a working mpv path
- changing mpv rendering behavior was the wrong priority while `mpvAvailable=0`

### User’s final product direction
The user explicitly concluded that continuing to salvage the broken/unreliable tlRender MOV path is likely a waste of effort and pushed toward:
- finding a Wayland-capable alternative for MOV playback across important codec groups
- or at minimum stopping blind attempts to fix generic tlRender behavior without codec-aware handling

The user also argued correctly that not all MOV codecs should share the same playback settings.

### Codec-group requirement from the user
The user explicitly requested codec-aware handling such as:
- interframe compressed codecs
- ProRes / editing mezzanine / 10-bit YUV422
- alpha-heavy codecs / PNG / RGBA / ProRes 4444-style content
- high-bandwidth / uncompressed-like content
- possibly different handling for audio-heavy files

That requirement should be carried forward.

---

## What code was touched during playback work

### Files touched during various playback attempts
These were part of the playback investigation and may contain partial/experimental changes:
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_player.h`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/mpv_player.cpp`
- `native/qt6/src/media/mpv_viewport.cpp`
- `native/qt6/tests/test_tlrender_playback_harness.cpp`

### Important caution
Many playback attempts were made, some were reverted, some were ineffective, and some were superseded.
The next agent should not assume every playback-related modification in the tree is valuable just because it exists.

The safe interpretation is:
- harness work is useful and should likely be kept
- user-verified passes are trustworthy
- mpv-side tweaks made while `mpvAvailable=0` are low-confidence and likely irrelevant until mpv availability is actually fixed
- tlRender fallback tuning was partially informative but did not solve the core remaining blockers

---

## Specific ineffective or superseded directions
The next agent should avoid repeating these mistakes without new evidence:

1. **Patching mpv render/logging behavior while mpv is unavailable**
   - This did not materially fix `Shot_0140_v005.mov`
   - Some mpv-only changes were explicitly reverted after failing validation

2. **Blind tlRender fallback micro-fixes without codec-aware grouping**
   - Some experiments improved specific cases but did not solve the real remaining blockers

3. **Repeated reread loops**
   - This consumed a huge amount of time and user trust
   - The user explicitly demanded that future work avoid this

4. **Assuming all MOV failures are the same**
   - User made it clear different MOV codec families need different handling

---

## Current verified runtime matrix

### Pass
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
- Annotation on video
- Annotation export
- Thumbnails/scrubbing
- Project Manager
- Batch Rename
- Bundled converters

### Fail
- `Atmosphere-019.mov`
- `Shot_0140_v005.mov` (still too choppy)

### Not current priority
- `Blood_Hit_03.mov`
- `Sunshine 10sec Full Comp v3.mov`

These were important in earlier harness work but are not the user’s current blocking cases.

---

## Commands used successfully in the session

### Build app
```bash
cmake --build build-linux-recovery-native-qt6 --target kassetmanagerqt -j 2
```

### Run app
```bash
./build-linux-recovery-native-qt6/kassetmanagerqt
```

### Build harness target
```bash
cmake --build build-linux-recovery-native-qt6 --target test_tlrender_playback_harness -j 2
```

### Run harness from build dir
```bash
./tests/test_tlrender_playback_harness
```

---

## Most important user directives to preserve

These were not preferences; they were explicit behavioral corrections from the user.

1. **Do not assume anything.**
2. **Respect explicit paths and scope boundaries.**
3. **Use subagents for research/inspection as much as possible.**
4. **Do not reread files repeatedly.**
5. **Do not broad-search external storage.** Only use `/mnt/ssd2/Tests/Videos/` for fixtures.
6. **One bounded milestone at a time.**
7. **Patch / build / test / revert** is preferred over analysis loops.
8. **If a fix fails, revert it rather than leaving speculative code hanging.**
9. **The next real engineering focus should be MOV playback only** until the remaining blockers are solved.
10. The user explicitly became frustrated that too much time was spent rereading and not enough time spent implementing.

---

## Best next step for the next agent

Do **not** restart from broad architecture reading.

### Recommended next milestone
Focus only on the remaining playback blockers:
1. `Atmosphere-019.mov`
2. `Shot_0140_v005.mov`

### Recommended approach
Given the final user feedback, the next agent should likely:

1. confirm whether mpv initialization is still failing **after** the locale fix now present in `native/qt6/src/media/mpv_player.cpp`
2. if mpv still does not initialize, stop spending time on mpv behavior tuning and treat the product as effectively running on the tlRender fallback path
3. decide whether to:
   - make tlRender codec-group aware in a more deliberate way, or
   - introduce a different Wayland-capable playback strategy for important MOV codec groups
4. keep validation strictly limited to:
   - `/mnt/ssd2/Tests/Videos/Atmosphere-019.mov`
   - `/mnt/ssd2/Tests/Videos/Shot_0140_v005.mov`

### If using the harness again
Use the existing playback harness, but keep fixture coverage focused on the actual failing files rather than the earlier broader set.

---

## Files the next agent is most likely to need

### Workflow / state
- `AGENTS.md`
- `.agent-workflow.md`
- `HANDOFF.md`

### Planning
- `.planning/REQUIREMENTS.md`
- `.planning/ROADMAP.md`
- `.planning/STATE.md`
- `.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-PLAN.md`

### Verified feature areas
- `native/qt6/src/preview_overlay.cpp`
- `native/qt6/src/preview_overlay.h`
- `native/qt6/src/annotation_layer.cpp`
- `native/qt6/src/annotation_items.h`
- `native/qt6/src/annotation_items.cpp`
- `native/qt6/src/bulk_rename_dialog.cpp`

### Playback core
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_player.h`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/mpv_player.cpp`
- `native/qt6/src/media/mpv_player.h`
- `native/qt6/src/media/mpv_viewport.cpp`
- `native/qt6/src/media/mpv_viewport.h`
- `native/qt6/src/media/codemap.md`
- `native/qt6/tests/test_tlrender_playback_harness.cpp`
- `native/qt6/tests/CMakeLists.txt`

### File classification / preview routing
- `native/qt6/src/file_utils.cpp`
- `native/qt6/src/live_preview_manager.cpp`
- `native/qt6/src/video_metadata.h`
- `native/qt6/src/video_metadata.cpp`

### Third-party type reference used earlier
- `third_party/tlRender-install-Release/include/ftk/Core/Image.h`

---

## Final concise state for the next agent

### Good
- App builds locally
- Annotation path works
- Export works
- Scrubbing/thumbnails work
- Project Manager works
- Batch Rename works
- Bundled converters work
- MEGY playback works

### Bad
- Atmosphere playback still fails
- Shot_0140 playback still too choppy

### Main lesson from this session
Do not waste time on repeated rereads or on mpv tuning unless mpv is proven available at runtime. The remaining work must be tight, codec-aware, and validated only against the real failing files.
