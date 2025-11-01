# Live Scrubbing Tasks

| ID | Task | Owners | Status | Notes |
|----|------|--------|--------|-------|
| T1 | Ship a full-featured FFmpeg build and integrate it into the app | Codex | TODO | Build FFmpeg with PNG/ProRes/DNxHD/MXF support, update CMake, and package the new DLLs |
| T2 | Harden `LivePreviewManager` for FFmpeg-first decoding (pool, caching, cancellation) | Codex | In Progress | Current manager handles requests but still needs robust error handling + codec reporting |
| T3 | Build resilient image-sequence streaming (EXR/DPX) with watchdogs and throttled IO | Codex | In Progress | Sequence queue + normalized frame picking live; watchdog + worker pool tuning still needed |
| T4 | Polish asset grid hover scrubbing (Ctrl + wheel, HUD-only overlay) | Codex | In Progress | Overlay renders decoded frames, grabs mouse during scrub, and locks focus until Ctrl released; awaiting user verification |
| T5 | Replace file manager thumbnail flow with live preview delegate & shared overlay | Codex | In Progress | Delegate adjusted for text baseline; dependent on FFmpeg upgrade for visible frames |
| T6 | Remove thumbnail cache UI/actions; add live scrubbing preferences | Codex | In Progress | Settings clears preview cache; preference toggles and terminology cleanup still needed |
| T7 | Documentation updates (developer, user, API references) | Codex | TODO | Describe live scrubbing workflows and FFmpeg requirements |
| T8 | Manual validation per developer guide checklist | Codex + QA | TODO | Capture findings in logs |

## Implementation Notes
- Update this file as tasks move from TODO -> In Progress -> Done.
- Coordinate with user regarding decoder tech choices before finalizing T2.
- Confirm if secondary gestures (e.g. horizontal mouse motion) should remain after Ctrl+wheel primary path lands.
- Coordinate FFmpeg build configuration with pipeline requirements (PNG-in-MOV, ProRes, DNxHD, MXF, AV1, etc.) and document the build recipe.
- LivePreviewManager API now services grids with sequence throttling; hover controllers live on asset/file grids but depend on working FFmpeg decode to render frames.
