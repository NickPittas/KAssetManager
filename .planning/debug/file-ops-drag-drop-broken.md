---
status: investigating
trigger: "Investigate why file operations are broken: Copy/Cut/Paste/Delete/Rename and drag-and-drop in files and folders are not working. Trace from MainWindow and file manager views/models down to FileOps/DragUtils/VirtualDrag/file utils. Look for platform-specific assumptions, especially Windows shell APIs or unsupported behavior on Linux/Qt6. Do not edit code yet."
created: 2026-04-14T00:00:00Z
updated: 2026-04-14T00:20:00Z
---

## Current Focus

hypothesis: Linux failures come from two concrete gaps: FileOpsQueue and VirtualDrag only execute on Windows, and rename has no refresh/error reporting so successful or failed renames are invisible in the File Manager UI
test: correlate MainWindow/FileManagerPane action wiring with non-Windows branches and inspect rename path for post-action refresh/error handling
expecting: copy/cut/paste/delete/drop will terminate in Windows-only backends; drag start will return before Qt fallback; rename will bypass FileOpsQueue but do no verification or model refresh
next_action: consolidate traced pipeline and root-cause evidence into diagnosis and fix strategy

## Symptoms

expected: File Manager copy, cut, paste, delete, rename, and drag-and-drop work for files and folders on Linux/Qt6
actual: Copy/Cut/Paste/Delete/Rename and drag-and-drop in files and folders are not working
errors: none provided
reproduction: use File Manager file operations or drag files/folders between locations
started: unknown

## Eliminated

## Evidence

- timestamp: 2026-04-14T00:04:00Z
  checked: initial repository instructions and codemaps
  found: File Manager operations are documented as flowing through MainWindow, FmGridViewEx/FmListViewEx, DragUtils, VirtualDrag, FileOps, and FileUtils
  implication: investigation should trace UI actions from MainWindow into those concrete classes rather than assuming Qt default behavior

- timestamp: 2026-04-14T00:11:00Z
  checked: MainWindow file operation handlers, fm_views_ex, FileOpsQueue, DragUtils, VirtualDrag, FileUtils
  found: MainWindow copy/cut/paste/delete route into FileOpsQueue for execution; fm view drags call VirtualDrag::startAdaptivePathsDrag directly; non-Windows FileOpsQueue always returns "OS-level file operations not supported on this platform" and non-Windows VirtualDrag functions are stubbed to return false
  implication: Linux has an apparent UI pipeline for operations, but the execution layer is either Windows-only or a no-op for drag start, which can explain the broad failure across copy/cut/paste/delete and file drag operations

- timestamp: 2026-04-14T00:18:00Z
  checked: MainWindow eventFilter drop handling, FileManagerPane secondary-pane signals, fm_views_ex drag code, rename handlers
  found: drops on File Manager views/tree also enqueue FileOpsQueue; FmGridViewEx/FmListViewEx contain a Qt QDrag fallback but it is dead code after an unconditional return following VirtualDrag::startAdaptivePathsDrag; rename uses QFile::rename/QDir::rename directly without checking return values or refreshing fmDirModel/fmEverythingTreeModel/secondary pane
  implication: internal/external drag is guaranteed to fail on Linux because the only executed drag path is a stub, while rename can appear broken because the UI never confirms failure or refreshes visible models after the filesystem change

## Resolution

root_cause: File Manager file operations were ported incompletely to Linux. Copy/move/delete and File Manager drop handling all terminate in FileOpsQueue, but FileOpsQueue only performs real work inside _WIN32 and returns an unsupported-platform error on Linux. File Manager drag start is similarly hard-wired to Windows-only VirtualDrag; on Linux the called stub returns false, and the existing Qt QDrag fallback is unreachable because startDrag returns before it. Rename bypasses FileOpsQueue, but its handlers do not check rename success or refresh the File Manager models/tree, so on Linux it can fail silently or appear to do nothing.
fix: Replace the non-Windows FileOpsQueue branch with Qt/QFile/QDir-based copy/move/delete implementations using the existing helper functions, restore a real Qt mime-data drag fallback for non-Windows in FmGridViewEx/FmListViewEx (or in VirtualDrag), and add result checking plus explicit File Manager refresh/tree refresh after rename.
verification: Re-run each File Manager operation on Linux in both primary and secondary panes, verify on-disk changes and visible model refresh, and verify drag start/drop works between File Manager views, tree targets, and external file managers.
files_changed: []
