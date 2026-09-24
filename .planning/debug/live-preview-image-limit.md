---
status: investigating
trigger: "Investigate the live preview/image loading error: '[LivePreview] failed ... Image too large for live preview' and '[OIIOImageLoader] Image too large: 16000 x 9142'. Determine exact limit logic, whether it matches intended 256 MB policy, blockers, and minimal fix strategy. Do not edit code."
created: 2026-04-14T00:00:00Z
updated: 2026-04-14T00:10:00Z
---

## Current Focus

hypothesis: Confirmed: the current worktree enforces a hardcoded 100-megapixel cap in OIIO-based image loads, and live preview uses the same pixel-count heuristic. There is no implemented 256 MB decoded-memory policy in these paths.
test: Summarize exact call paths, blockers, and minimal fix strategy.
expecting: Findings should show mismatch between intended policy and current implementation.
next_action: return findings with file/function references and severity

## Symptoms

expected: Image should be allowed when it satisfies the intended 256 MB hard limit policy.
actual: Live preview rejects image with 'Image too large for live preview' and OIIO logs 'Image too large: 16000 x 9142'.
errors: [LivePreview] failed ... "Image too large for live preview"; [OIIOImageLoader] Image too large: 16000 x 9142
reproduction: Attempt to load the reported large image in live preview.
started: Reported in current worktree; historical start unknown.

## Eliminated

## Evidence

- timestamp: 2026-04-14T00:03:00Z
  checked: native/qt6/src/oiio_image_loader.cpp::OIIOImageLoader::loadImage
  found: The loader rejects any image where width * height > 100000000 via kMaxPixels and logs '[OIIOImageLoader] Image too large: <w> x <h>'.
  implication: The current rejection is based on raw pixel count, not compressed file size.

- timestamp: 2026-04-14T00:04:00Z
  checked: native/qt6/src/live_preview_manager.cpp::LivePreviewManager::loadImageFrame
  found: Live preview calls OIIOImageLoader::loadImage(request.filePath, request.targetSize.width(), request.targetSize.height()). If that returns null, it probes dimensions via QImageReader and rejects with 'Image too large for live preview' only when pixelCount > 100000000.
  implication: Live preview mirrors the same 100-megapixel heuristic and surfaces it as a preview-specific error.

- timestamp: 2026-04-14T00:07:00Z
  checked: repo search plus preview_overlay.cpp, image_preview_overlay.cpp, file_manager_pane.cpp, thumbnail_generator_worker.cpp
  found: No application code in the current worktree implements a 256 MB image hard limit. Non-live-preview image paths call OIIOImageLoader::loadImage(..., 0, 0), which still passes through the same 100-megapixel OIIO guard.
  implication: The current behavior is governed by a shared pixel-count cap, not a 256 MB policy, and the same blocker can affect full preview/file-manager image loads.

- timestamp: 2026-04-14T00:08:00Z
  checked: native/qt6/src/oiio_image_loader.cpp::OIIOImageLoader::loadImage internals
  found: The function reads with ImageBuf::read(..., TypeDesc::FLOAT) before applying resize logic, meaning it materializes full float image data first and only then optionally downsizes.
  implication: Even thumbnail/live-preview requests are not bounded by requested output size; the implementation is not using an output-memory-based policy and may consume much more memory than a 256 MB decoded-output rule would suggest.

- timestamp: 2026-04-14T00:09:00Z
  checked: reported dimensions 16000 x 9142 against intended policy
  found: 16000 * 9142 = 146,272,000 pixels, which exceeds the current 100,000,000-pixel cap. At 4 bytes/pixel RGBA8 decoded size this is about 585 MB; at 3 bytes/pixel RGB8 about 439 MB; at float read time it is much larger still.
  implication: The observed rejection is expected under the current code, but it does not align with a stated policy based on file size and only aligns with a 256 MB policy if that policy is defined as decoded in-memory image budget.

## Resolution

root_cause: 
root_cause: OIIO-based image loading is gated by a hardcoded 100-megapixel limit and live preview duplicates that same threshold as its own 'too large' decision. No 256 MB file-size or decoded-memory policy is implemented in these paths.
fix: Minimal strategy is to replace the shared 100-megapixel heuristic with a single explicit image-memory policy check based on decoded bytes (or exact approved heuristic), and apply it consistently before user-facing rejection. Live preview should report the same reason from the shared check rather than a second divergent threshold.
verification: Investigation only; no code changes made.
files_changed: []
