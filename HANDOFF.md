# HANDOFF

## Purpose
This handoff is for the next agent to continue the KAssetManager work without re-reading the full prior conversation.

Primary focus at handoff time:
1. Preserve already fixed Phase 03 fullscreen preview/annotation/player UX regressions.
2. Continue the remaining playback-core work for problematic MOV files under Wayland.
3. Avoid the repeated-analysis loop that consumed excessive time/tokens in the prior session.

---

## Mandatory workflow for next agent
Read and follow these first:
- `AGENTS.md`
- `.agent-workflow.md`
- `codemap.md`
- `native/qt6/src/media/codemap.md`

Operational rules already established in repo/session:
- Do not assume anything.
- Use only explicit paths/scopes from user.
- Use subagents heavily for research/inspection whenever possible.
- One active milestone at a time.
- No repeated rereads unless a file changed or a test/build result points there.
- Every cycle must end in one of: patch, build/test result, or blocker with exact evidence.
- Revert failed fixes immediately.

---

## Project-local fixture scope
Use only project-local fixture roots for playback validation.

Do not search mounted drives, home directories, temp directories, or any external storage trees.

Expected fixture root:
- `native/qt6/tests/fixtures/videos/`

Known ffprobe characteristics from prior session:
- `Blood_Hit_03.mov`: PNG / RGBA alpha MOV stress case.
- `Sunshine 10sec Full Comp v3.mov`: ProRes 25fps target.
- `Atmosphere-019.mov`: 4K-ish ProRes stress case.
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`: HEVC-in-MOV large square case.

---

## What was already fixed successfully
These were user-validated as fixed during the session.

### 1. Phase 03 fullscreen/player UI issues fixed
Confirmed fixed by user:
- wrong center play overlay / side nav arrows
- timeline range staying too long after switching to shorter media
- fullscreen zoom caused by controls hiding
- controls autohide removed / controls always visible
- first annotation-session initialization bug

### 2. Annotation behavior materially improved and fixed enough for reported issue
Confirmed by user after bounded fixes:
- first time opening annotation now works without requiring close/reopen
- Select tool state bug was fixed earlier
- annotation path no longer blocked by the original first-session failure

### Main files that were changed during those successful fixes
Review these first before touching preview/annotation again:
- `native/qt6/src/preview_overlay.cpp`
- `native/qt6/src/preview_overlay.h`
- `native/qt6/src/annotation_layer.cpp`
- `native/qt6/src/annotation_items.h`
- `native/qt6/src/annotation_items.cpp`

### Key successful code areas from earlier work
These were repeatedly cited in-session as the active/fixed paths:
- annotation event forwarding and overlay sync in `native/qt6/src/preview_overlay.cpp:2596-2652`
- first-session deferred overlay resync in `native/qt6/src/preview_overlay.cpp:4421-4439`
- annotation mode entry sync in `native/qt6/src/preview_overlay.cpp:4462-4490`
- video export frame preference in `native/qt6/src/preview_overlay.cpp:4868-4883`
- annotation release geometry update in `native/qt6/src/annotation_layer.cpp:202-210`
- geometry mutation fixes in `native/qt6/src/annotation_items.h:123-126`, `native/qt6/src/annotation_items.h:147-149`, `native/qt6/src/annotation_items.h:169-171`, `native/qt6/src/annotation_items.h:191-196`
- freehand growth fix in `native/qt6/src/annotation_items.cpp:269-277`

### Planning files that were updated earlier
Phase 03 planning/status was revised to reflect real defect-driven scope:
- `.planning/REQUIREMENTS.md`
- `.planning/ROADMAP.md`
- `.planning/STATE.md`
- `.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-PLAN.md`

---

## Build environment status
A build recovery was completed earlier and was required before functional work could continue.

### Important build facts
- The stale `build-linux` tree was tied to another worktree and was not reliable.
- A clean working build tree was established at:
  - `build-linux-recovery-native-qt6`
- The app was rebuilt successfully from that tree multiple times.

### Vendor CMake export fixes were applied earlier
The following generated package/export files were patched to point to this checkout so configure/build would work again:
- `third_party/tlRender-install-Release/lib/cmake/SVT-AV1/SVT-AV1-staticTargets.cmake`
- `third_party/tlRender-install-Release/lib/cmake/SVT-AV1/SVT-AV1-staticTargets-release.cmake`
- `third_party/tlRender-install-Release/lib/cmake/libjpeg-turbo/libjpeg-turboTargets.cmake`
- `third_party/tlRender-install-Release/lib/cmake/libjpeg-turbo/libjpeg-turboTargets-release.cmake`
- `third_party/tlRender-install-Release/lib64/cmake/OpenImageIO/OpenImageIOConfig.cmake`
- `third_party/tlRender-install-Release/lib64/cmake/minizip-ng/minizip-ng.cmake`
- `third_party/tlRender-install-Release/share/opentime/OpenTimeTargets.cmake`
- `third_party/tlRender-install-Release/share/opentime/OpenTimeTargets-release.cmake`
- `third_party/tlRender-install-Release/share/opentimelineio/OpenTimelineIOTargets.cmake`
- `third_party/tlRender-install-Release/share/opentimelineio/OpenTimelineIOTargets-release.cmake`

### Working build/run commands used in session
From repo root:
- build: `cmake --build build-linux-recovery-native-qt6 -j 2`
- app run: `./build-linux-recovery-native-qt6/kassetmanagerqt`

Note: there was repeated discussion that “build succeeded” only meant compile/link succeeded, not that overall quality/warnings were acceptable. Treat build output quality as its own gate.

---

## Playback-core work: what was done, what failed, and what remains
This is the main unfinished workstream.

### Goal
Fix real-time playback under Wayland for problematic MOV files, especially:
- `Atmosphere-019.mov`
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`

### Architecture established during investigation
Wayland path uses raster fallback, not the native tlRender widget:
- policy: `native/qt6/src/platform_session.h:38-41`
- media architecture notes: `native/qt6/src/media/codemap.md:36-49`
- raster widget creation: `native/qt6/src/media/tlrender_viewport.cpp:31-41`
- raster frame pull path: `native/qt6/src/media/tlrender_viewport.cpp:153-173`
- player frame acquisition path: `native/qt6/src/media/tlrender_player.cpp` around `getCurrentFrame(...)`

### Playback harness was added
A real playback validation harness was created and is important for the next agent.

Files added/changed for harness:
- `native/qt6/tests/test_tlrender_playback_harness.cpp`
- `native/qt6/tests/CMakeLists.txt`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/src/media/tlrender_player.h`
- `native/qt6/src/media/tlrender_player.cpp`

### What the harness measures
The harness was designed to measure actual viewer-side raster-presented frame progression, not just transport state.
The viewport seam exposes current raster frame / presentation revision for test observation.

Key harness/seam references used throughout session:
- viewport seam: `native/qt6/src/media/tlrender_viewport.h:84-89`
- raster presentation observation: `native/qt6/src/media/tlrender_viewport.cpp:153-194`
- harness file: `native/qt6/tests/test_tlrender_playback_harness.cpp`
- test target wiring: `native/qt6/tests/CMakeLists.txt:225-283`

### Important harness evolution
1. Initial harness version was invalid because it failed even on files the app could play.
2. Harness was corrected to better match the real app path.
3. After correction, it produced usable evidence.

### Most trustworthy harness findings before the session derailed
#### A. Alpha MOV progression path works better than originally feared
`Blood_Hit_03.mov` progression test passed after harness correction.

#### B. 25fps ProRes target under-delivered viewer cadence
`Sunshine 10sec Full Comp v3.mov` failed the 25fps viewer-side cadence threshold.
At one key checkpoint it showed approximately:
- viewer distinct fps: ~21.00–21.50
- render signals: ~48–49
- distinct player frames: ~43–47
- fallback extractions: ~48
- last cached image type: `26`

This led to mapping tlRender image type `26` as:
- `ftk::ImageType::YUV_422P_U16`
from:
- `third_party/tlRender-install-Release/include/ftk/Core/Image.h`

Conclusion from that phase:
- current converter in `native/qt6/src/media/tlrender_player.cpp:72-123` was too narrow.
- cached-frame conversion only handled a few formats such as `L_U8`, `RGB_U8`, `RGBA_U8`.
- ProRes path was repeatedly falling back instead of using a cheap cached conversion.

#### C. Real problematic fixtures were later switched to Atmosphere + MEGY
User clarified the actual problem fixtures were not the small alpha test or mp4 control, but:
- `Atmosphere-019.mov`
- `MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`

Harness was expanded to cover those.

### Critical findings from the later harness runs on actual problematic fixtures
At the point just before the session derailed, the most useful evidence was:

#### Atmosphere
- no longer primarily fallback-bound after one partial conversion improvement pass
- but raster viewport was under-presenting frames badly
- user-visible/path summary in-session: viewport emitted only a tiny number of render signals compared with player frame progression

One explicitly recorded result after a failed attempted patch/revert cycle:
- fixture: `native/qt6/tests/fixtures/videos/Atmosphere-019.mov`
- distinct viewer fps: `2.00`
- distinct frames: `4`
- render signals: `19`
- distinct player frames: `7`
- fallback extractions: `7`
- referenced harness area: `native/qt6/tests/test_tlrender_playback_harness.cpp:223-244`

Interpretation from session:
- Atmosphere remaining gate is primarily the Wayland raster presentation/refresh path in `native/qt6/src/media/tlrender_viewport.cpp`, not just conversion.

#### MEGY
- still associated with unsupported cached conversion / insufficient conversion path quality
- one explicitly recorded failed result after attempted patch/revert cycle:
- fixture: `native/qt6/tests/fixtures/videos/MEGY_comp_4K_LL180_ap0_r709g24_v015.mov`
- distinct viewer fps: `5.00`
- distinct frames: `10`
- render signals: `17`
- distinct player frames: `9`
- fallback extractions: `1`
- referenced harness area: `native/qt6/tests/test_tlrender_playback_harness.cpp:272-293`

Interpretation from session:
- MEGY still needed better cached conversion support for the relevant YUV420 path.

### Failed playback patch that must NOT be blindly trusted
A later bounded playback-core patch was attempted and then explicitly reported as failed and reverted.
According to the last reliable session summary, the following files were attempted then reverted because harness validation still failed:
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/src/media/tlrender_viewport.h`

Therefore: do not assume the last attempted playback patch is present. Re-check actual working tree state before continuing.

### Strongest remaining playback hypotheses at handoff
1. `native/qt6/src/media/tlrender_player.cpp` still needs direct cached conversion support for a missing YUV420 cached frame path relevant to MEGY.
2. `native/qt6/src/media/tlrender_viewport.cpp` needs a more reliable playback-driven raster refresh/presentation path on Wayland for Atmosphere.
3. The earlier partial YUV422/ProRes conversion work may or may not still exist in tree; verify actual current diff before further edits.

### Files the next agent must inspect first for playback continuation
- `native/qt6/src/media/tlrender_player.h`
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/tests/test_tlrender_playback_harness.cpp`
- `third_party/tlRender-install-Release/include/ftk/Core/Image.h`
- `native/qt6/tests/CMakeLists.txt`

---

## What the next agent should do first
Use subagents for research/inspection as requested by user.

### Immediate execution sequence recommended
1. Read `AGENTS.md` and `.agent-workflow.md`.
2. Verify current git working tree state/diff only once.
3. Inspect only the playback files listed above.
4. Confirm whether the reverted failed playback patch is actually absent/present.
5. Build and run the existing harness unchanged first, using only project-local fixture files.
6. From that baseline, patch only:
   - cached YUV420 conversion in `native/qt6/src/media/tlrender_player.cpp`
   - raster presentation refresh behavior in `native/qt6/src/media/tlrender_viewport.cpp`
7. Rebuild harness.
8. Rerun harness.
9. Keep only changes that improve measured viewer-side cadence.
10. Revert immediately if no improvement.

### Validation command previously used successfully in build dir
From `build-linux-recovery-native-qt6`:
- `cmake --build . --target test_tlrender_playback_harness -j 2`
- `./tests/test_tlrender_playback_harness`

---

## User expectations and constraints that must be honored
- User is highly frustrated by repeated rereading/looping.
- User explicitly asked for subagent-heavy workflow for reading/inspection.
- User explicitly asked to avoid assumption and avoid broad search.
- User explicitly requires no writes outside the project folder.
- User wants a useful, sellable product and completion of all project phases.
- User does not want more process noise in `AGENTS.md`; persistent execution rules were moved to `.agent-workflow.md`.
- User demanded that if a patch fails validation it must be reverted immediately.

### Behavioral warning for next agent
This session went badly because the agent repeatedly:
- reread the same files
- restated the same plan
- delayed implementation
- used main-thread analysis instead of subagents

Do not repeat that. Use the execution contract.

---

## Files/documents that matter for full continuation
### Repo policy / workflow
- `AGENTS.md`
- `.agent-workflow.md`

### Planning / project state
- `.planning/REQUIREMENTS.md`
- `.planning/ROADMAP.md`
- `.planning/STATE.md`
- `.planning/phases/03-preserve-annotation-on-video-and-sequences/03-01-PLAN.md`

### Annotation / fullscreen preview
- `native/qt6/src/preview_overlay.cpp`
- `native/qt6/src/preview_overlay.h`
- `native/qt6/src/annotation_layer.cpp`
- `native/qt6/src/annotation_items.h`
- `native/qt6/src/annotation_items.cpp`

### Playback core / tests
- `native/qt6/src/media/tlrender_player.h`
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_viewport.h`
- `native/qt6/src/media/tlrender_viewport.cpp`
- `native/qt6/src/platform_session.h`
- `native/qt6/tests/CMakeLists.txt`
- `native/qt6/tests/test_tlrender_playback_harness.cpp`
- `third_party/tlRender-install-Release/include/ftk/Core/Image.h`

### Architecture references
- `codemap.md`
- `native/codemap.md`
- `native/qt6/codemap.md`
- `native/qt6/src/codemap.md`
- `native/qt6/src/media/codemap.md`

---

## Final concise state
### Confirmed fixed by user
- nav arrows
- timeline reset after shorter media
- fullscreen zoom regression
- controls always visible
- first annotation-session initialization

### Implemented but playback still unfinished
- playback harness and viewport observation seam
- telemetry for player frame acquisition path
- several attempted playback-core fixes, including at least one failed/reverted patch cycle

### Main unresolved issue
Wayland raster playback for the real problematic MOV fixtures remains under target and needs further bounded work in:
- `native/qt6/src/media/tlrender_player.cpp`
- `native/qt6/src/media/tlrender_viewport.cpp`

### Do next
Resume only the playback-core milestone, using the harness and the two real fixtures as the gate.
