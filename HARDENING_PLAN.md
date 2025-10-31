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

## Execution Notes
- Address items T2 and T3 immediately to unblock further UX verification and guard screenshot workflows.
- T4–T8 represent the critical data-integrity layer and should be implemented before lower-priority polish.
- Validation must include manual regression tests for folder imports, sequence creation, DB backup/restore, and bulk asset operations.
- Update this document with completion status as tasks land; reference it in the eventual pull request summary.
