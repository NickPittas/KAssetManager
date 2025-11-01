# Live Scrubbing Tasks

| ID | Task | Owners | Status | Notes |
|----|------|--------|--------|-------|
| T1 | Ship a full-featured FFmpeg build and integrate it into the app | Codex | Done | Gyan full-shared bundle fetched via `scripts/fetch-ffmpeg.ps1`; build script stages the matching DLLs |
| T2 | Harden `LivePreviewManager` for FFmpeg-first decoding (pool, caching, cancellation) | Codex | Done | Live previews stream in-memory with cache eviction, error dedupe, and cursor-clamped scrubbing |
| T3 | Build resilient image-sequence streaming (EXR/DPX) with watchdogs and throttled IO | Codex | Done | Sequence metadata cache drives grouped previews; overlay plays reconstructed frame lists |
| T4 | Polish asset grid hover scrubbing (Ctrl + wheel, HUD-only overlay) | Codex | Done | Shared controller clamps cursor, maps card width to timeline, and paints inset live previews |
| T5 | Replace file manager thumbnail flow with live preview delegate & shared overlay | Codex | Done | File Manager uses the same delegate/overlay; sequence grouping checkbox controls proxy |
| T6 | Remove thumbnail cache UI/actions; add live scrubbing preferences | Codex | Done | UI copy now references live previews; cache clear keeps in-memory store only |
| T7 | Documentation updates (developer, user, API references) | Codex | Done | User/Developer/Tech guides now describe live previews, FFmpeg tooling, and File Manager controls |
| T8 | Manual validation per developer guide checklist | Codex + QA | In Progress | Continue logging coverage for EXR sequences, intraframe codecs, and rapid-scroll scrubbing |

## Implementation Notes
- Update this file as tasks move from TODO -> In Progress -> Done.
- Coordinate with user regarding decoder tech choices before finalizing T2.
- Confirm if secondary gestures (e.g. horizontal mouse motion) should remain after Ctrl+wheel primary path lands.
- Coordinate FFmpeg build configuration with pipeline requirements (PNG-in-MOV, ProRes, DNxHD, MXF, AV1, etc.) and document the build recipe.
- LivePreviewManager API now services grids with sequence throttling; hover controllers live on asset/file grids but depend on working FFmpeg decode to render frames.
