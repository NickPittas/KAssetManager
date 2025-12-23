# KAsset Manager 1.5.2 Release Notes

**Release Date:** December 19, 2025

## Overview

Version 1.5.2 is a maintenance release focused on improving video scrubbing performance and stability across all grid views (Asset Manager, File Manager, and Project Manager).

---

## Major Improvements

### 🎬 Enhanced Video Scrubbing System

#### In-Place Thumbnail Scrubbing
- **Direct Thumbnail Updates**: Ctrl+mouse scrubbing now updates thumbnails directly instead of showing intrusive overlays
- **Unified Experience**: Consistent scrubbing behavior across Asset Manager, File Manager, and Project Manager
- **Progress Indicator**: Thin progress bar at the bottom of thumbnails shows scrub position

#### Performance Optimizations
- **GStreamer Pipeline Caching**: Added `PipelineCache` class to reuse GStreamer pipelines for faster video frame extraction
- **KEY_UNIT Seeking**: Optimized seeking strategy for better performance with MOV/MP4 files
- **Async Frame Filtering**: Prevents timeline jumping by filtering out stale frame responses during fast scrubbing

#### Stability Improvements
- **Frame Ordering**: Fixed async frame delivery issues that caused frames to jump to previous positions
- **Delegate Logic Fixes**: Corrected File Manager delegate logic that was overwriting scrub frames
- **Registry Pattern**: Implemented `ScrubFrameRegistry` singleton for reliable frame storage and retrieval

### 🛠️ Technical Enhancements

#### Backend Improvements
- **ScrubFrameRegistry**: Singleton registry for managing active scrub frames across views
- **GridScrubController**: Enhanced controller with position tracking and stale frame filtering
- **LivePreviewManager**: Improved async frame request handling with position validation

#### Code Quality
- **Threading Rules**: Maintained proper Qt threading patterns with queued signals
- **Memory Management**: Efficient frame caching with LRU eviction (512MB limit)
- **Error Handling**: Robust handling of async frame requests and responses

---

## Bug Fixes

### Video Scrubbing Issues
- Fixed timeline jumping during fast scrubbing due to out-of-order async frame delivery
- Resolved File Manager scrub frames being overwritten by file preview code
- Eliminated intrusive overlay behavior in favor of in-place thumbnail updates

### Performance Issues
- Improved initial frame pickup speed for MOV/MP4 files
- Reduced GStreamer pipeline creation overhead through caching
- Optimized frame extraction with KEY_UNIT seeking strategy

---

## System Requirements

No changes from version 1.5.1.

---

## Installation

Download and run the installer: `KAssetManager-Setup-1.5.2.exe`

---

## Known Issues

No new known issues introduced in this release.

---

## Previous Versions

- [Version 1.5.1 Release Notes](RELEASE_NOTES_1.5.1.md)
- [Version 1.5.0 Release Notes](RELEASE_NOTES_1.5.0.md)</content>
<parameter name="filePath">e:\KAssetManager\RELEASE_NOTES_1.5.2.md