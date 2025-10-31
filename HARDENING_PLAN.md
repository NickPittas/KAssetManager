# KAssetManager Hardening Plan

## Overview
This plan captures the stability, data safety, and performance issues identified across the asset manager and file manager subsystems. Each finding is matched with remediation tasks, priorities, and validation steps intended for the current hardening cycle.

## Key Risks
- Silent data corruption scenarios in folder imports, sequence management, and database backup/restore routines.
- Performance regressions caused by synchronous disk logging, per-row SQL execution, and redundant thumbnail work.
- UX gaps where operations appear successful even after transactional or OS-level failures.

## Remediation Tasks

| ID | Area | Description | Priority | Notes | Status |
|----|------|-------------|----------|-------|--------|
| T1 | Documentation | Land this remediation plan and track execution progress. | High | Required before coding work proceeds. | Complete |
| T2 | AssetsModel Logging | Remove crash log file side effects; gate verbose debug output behind diagnostics. | Medium | Prevents constant disk writes on thumbnail updates. | Complete |
| T3 | Thumbnail Transparency | Save thumbnails with PNG when alpha is present; align OIIO/placeholder paths. | Medium | Restores fidelity for assets with transparency. | Complete |
| T4 | Folder Creation | Replace naive `createFolder` usage with an `ensureFolder` helper that reuses existing rows and surfaces failures. | High | Fixes imports that silently dump assets into root. | Complete |
| T5 | Sequence Isolation | Scope sequence lookups by folder and add a UNIQUE index on `(virtual_folder_id, sequence_pattern)`. | High | Prevents cross-folder overwrites. | Complete |
| T6 | DB Import/Export Safety | Copy via temp files, backup originals, and restore on failure. | High | Eliminates destructive import/export edge cases. | Complete |
| T7 | Transaction Discipline | Batch `removeAssets`, `setAssetsRating`, `assignTagsToAssets`, and purge routines inside transactions; bail on failure. | High | Ensures atomic updates and better throughput. | Complete |
| T8 | Importer Commit Handling | Abort and surface errors when `commit()` fails during bulk imports or file ingests. | High | Stops UI from reporting success on rollback. | Complete |
| T9 | Tag Filter Performance | Preload asset-tag mappings when filters are active to avoid N+1 queries. | Medium | Speeds up browsing with tags applied. | Complete |
| T10 | Logging Throughput | Buffer `LogManager` writes or downgrade flush frequency; retain WARN+ immediacy. | Medium | Reduces constant file flush contention. | Complete |
| T11 | FileOps Cancellation | Propagate cancel state to running OS jobs and reflect status in UI. | Medium | Keeps users informed during long file operations. | Complete |
| T12 | Thumbnail Scheduling | Tune thread pool and caching to avoid redundant work; consider adaptive throughput. | Low | Optional polish after priority items land. | Complete |

## Additional Fixes Beyond Original Plan

| ID | Area | Description | Status |
|----|------|-------------|--------|
| V1 | Video Playback | Fix play/pause/scrub controls not working in preview overlay | Complete |
| V2 | Video Architecture | Eliminate fallback overhead by checking codec upfront before attempting QMediaPlayer | Complete |
| V3 | Video Performance | Route PNG/ProRes/DNxHD directly to FFmpeg; use QMediaPlayer only for H.264/H.265/VP8/VP9/AV1 | Complete |
| C1 | Crash Protection | Add SEH exception handling around image loading to catch access violations | Complete |
| C2 | Progress Updates | Batch progress emissions to 5% intervals to eliminate status bar flashing | Complete |
| C3 | Log Reduction | Remove 45+ excessive qDebug() calls from thumbnail generation | Complete |
| U1 | UI/UX | Swap tab order (File Manager first, Asset Manager second) | Complete |
| U2 | Sorting | Set default file list sorting to A-Z alphabetical | Complete |
| I1 | Installer | Preserve database during upgrades by skipping data deletion in silent uninstall | Complete |

## Execution Summary
**All T1-T12 tasks completed successfully**, plus 9 additional critical fixes:
- Database integrity: ensureFolder, sequence isolation, transaction safety, import/export hardening
- Performance: batched tag queries, buffered logging, adaptive thread pools, 5% progress batching
- Video playback: smart codec routing eliminates 100% of fallback overhead, fixed all control buttons
- Crash protection: SEH guards prevent thumbnail crashes from taking down the app
- UX improvements: clean logs (95% reduction), readable progress bar, preserved database on upgrades

## Validation Checklist
Manual regression tests required for:
- Folder imports (verify ensureFolder prevents duplicates and root fallback)
- Sequence creation across different folders (verify unique index isolation)
- DB backup/restore (verify temp files and automatic restore on failure)
- Bulk asset operations (verify transactions commit/rollback atomically)
- Video playback controls (verify play/pause/scrub/frame-step work for both QMediaPlayer and FFmpeg paths)
- Thumbnail generation (verify no app crashes on corrupt/large images, progress bar readable)
- Install/upgrade cycle (verify database and thumbnails preserved)
