---
status: investigating
trigger: "Investigate the remaining Phase 06 blocker around Project Manager path normalization and case sensitivity on Linux. Focus: Research indicated some Project Manager path normalization lowercases paths. On Linux this is risky because filesystems are case-sensitive. Read the current Phase 06 docs, inspect ProjectDB and related Project Manager path mapping helpers, identify every place where PM path normalization or matching may lower-case or otherwise break Linux case-sensitive behavior, determine the concrete user-visible risk and the smallest safe fix, and return findings first with file/function references, then a minimal fix plan and validation checklist. Do not edit code."
created: 2026-04-14T00:00:00Z
updated: 2026-04-14T00:18:00Z
---

## Current Focus

hypothesis: Confirmed: active PM import/resync/navigation flows contain lowercase or case-insensitive path matching that can collapse distinct Linux paths and select the wrong asset/folder.
test: Synthesize findings into concrete runtime risks and smallest safe fix scope.
expecting: A minimal fix plan limited to PM folder-path maps and PM asset-path lookup, with dormant duplicate code called out separately.
next_action: return findings with file/function references, minimal fix plan, and validation checklist

## Symptoms

expected: Project Manager should preserve exact filesystem path case on Linux and distinguish case-different paths correctly.
actual: Research indicates some PM normalization lowercases paths, risking incorrect behavior on case-sensitive Linux filesystems.
errors: none reported
reproduction: Inspect PM import/folder mapping helpers and trace path normalization/matching behavior.
started: identified during Phase 06 Linux Project Manager verification research on 2026-04-14

## Eliminated

## Evidence

- timestamp: 2026-04-14T00:05:00Z
  checked: AGENTS.md, codemap.md, native/qt6/src/codemap.md, Phase 06 roadmap/state/research/plan docs
  found: Phase 06 research already identified PM lowercasing in importer and ProjectDB as a Linux-sensitive blocker, and the repository workflow requires tracing runtime code before proposing changes.
  implication: Investigation should focus on the actual PM import/folder-target pipeline rather than speculative cross-module fixes.

- timestamp: 2026-04-14T00:05:00Z
  checked: grep for toLower/case-insensitive/path-mapping usages under native/qt6/src
  found: PM-relevant lowercasing/matching appears in importer.cpp, project_db.cpp, project_import_worker.cpp, and mainwindow.cpp; most other toLower matches are extension/type handling and not path mapping.
  implication: The likely Linux case-sensitivity risk is concentrated in a small set of PM path normalization helpers and one PM asset-path comparison site.

- timestamp: 2026-04-14T00:10:00Z
  checked: project_db.cpp ensureFolderForPath/resyncAssetFolders, importer.cpp importFolderContents, project_import_worker.cpp doImport, mainwindow.cpp PM destination/navigation helpers, project_manager_watcher.cpp
  found: PM import and folder-resolution code uses lowercased normalized-path hash keys and prefix checks, while mainwindow navigation compares PM asset file paths with Qt::CaseInsensitive.
  implication: Linux risk is not limited to a single helper; both DB folder mapping and UI selection flows can collapse distinct case-different filesystem paths.

- timestamp: 2026-04-14T00:18:00Z
  checked: mainwindow.cpp PM refresh/new-files/removed-files paths, project_db.cpp createFolder/ensureChildFolder/removeAssetsByPath/updateAssetPath, project_import_worker usage search
  found: Active PM runtime uses Importer::importFolderContents plus ProjectDB::ensureFolderForPath/resyncAssetFolders and MainWindow::navigateToProjectAsset; remove/update SQL lookups are exact-match and safe, and ProjectImportWorker is currently unused but repeats the same lowercase folder-map pattern.
  implication: The smallest safe fix is to stop lowercasing PM path keys/comparisons in active folder-mapping helpers and to make PM asset-path navigation exact on Linux; dormant worker code should be kept aligned if revived.

## Resolution

root_cause: 
root_cause: Active Project Manager path-mapping code normalizes filesystem paths with toLower() in importer and ProjectDB helpers, and PM asset navigation compares file paths with Qt::CaseInsensitive. On Linux, two real paths that differ only by case can therefore be treated as the same path, causing wrong folder resolution, wrong asset selection, or silent path collapse during resync/import flows.
fix: Smallest safe fix is to preserve case in PM normalized-path keys and comparisons for active PM flows: use QDir::cleanPath() without toLower() in Importer::importFolderContents and ProjectDB::{ensureFolderForPath,resyncAssetFolders}, and replace Qt::CaseInsensitive PM file-path comparison in MainWindow::navigateToProjectAsset with exact/path-native comparison. Keep ProjectImportWorker aligned if that path is re-enabled.
verification: Static tracing confirmed the active PM runtime uses the affected helpers for initial import, watcher add/remove reconciliation, manual folder resync, and notification-driven asset navigation. Exact-match SQL path updates/removals were also checked and do not introduce the Linux case-collapse issue.
files_changed: []
