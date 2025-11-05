# Changelog

All notable changes to KAsset Manager will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Planned Features
- Keyboard shortcuts for common operations
- Batch export functionality
- Custom metadata fields
- Smart folders with saved searches
- Collection sharing and export
- Undo/redo system
- Crash reporting

## [1.0.5] - 2025-11-05

### Fixed
- **Critical**: Database persistence issue where database was deleted during app updates
  - Database now stored in persistent user data location (`AppData/Roaming/KAsset/KAsset Manager Qt/`)
  - Database survives app updates and reinstallations
  - Automatic migration from old location (`appDir/data/`) to new persistent location
  - Old database preserved for safety during migration

### Changed
- Crash dump location moved to persistent user data directory
- Updated all documentation to reflect new database location

### Technical
- Changed database storage from `applicationDirPath() + "/data/"` to `QStandardPaths::AppDataLocation`
- Added migration logic in `main.cpp` to detect and copy existing databases
- Updated Windows resource file version to 1.0.5.0

## [0.2.0] - 2025-10-29

### Added
- File Manager: “Add to Library” now supports selected folders in addition to files; when folders are selected, their subfolder hierarchy is preserved and recreated in the Asset Manager (matches drag-and-drop behavior)
- File Manager: Folders-first sorting in both Grid and List views (folders always listed above files regardless of sort column or order)
- File Manager: Folder tree now stays in sync when navigating via the right-pane file view (double-clicking a folder expands/selects it in the left tree)

### Fixed
- Crash when using “Add to Library” due to passing proxy indexes to the source `QFileSystemModel`. Selection indexes are now mapped correctly, preventing hard crashes

### Improved
- Preview overlay: When closing full-size preview (Esc or close button), focus and selection return to the previously selected item (Asset Manager or File Manager) so arrow-key navigation continues immediately
- Installer/Build: Release outputs `dist/KAssetManager-Setup-0.2.0.exe` and a `.sha256` hash file

### Notes
- Scrubbable video thumbnails on hover are feasible with the current Qt/FFmpeg stack; to be implemented in a future release after perf tuning


## [0.1.0] - 2024-01-XX

### Added

#### Core Features
- **Virtual folder system** for organizing assets without moving files on disk
- **Hierarchical folder structure** with create, rename, delete, and move operations
- **Drag-and-drop import** for files and folders with recursive scanning
- **Multi-select support** with Ctrl+Click (toggle) and Shift+Click (range)
- **Tagging system** with create, rename, delete, and merge operations
- **5-star rating system** for assets
- **Search and filter** by name, tags (AND/OR mode), rating, and file type
- **Grid view** with adjustable thumbnail size (100-400px)
- **List view** with sortable columns (Name, Type, Size, Date, Rating)
- **Full-screen preview** for images, videos, and image sequences
- **Context menus** for quick access to common operations

#### Preview Features
- **Image viewer** with zoom and pan controls
- **Video player** with timeline, play/pause, and volume controls
- **Image sequence playback** at 24fps with frame navigation
- **HDR/EXR support** with color space selection (Linear, sRGB, Rec.709)
- **Keyboard navigation** with arrow keys (next/previous asset)
- **Automatic format detection** for images, videos, and sequences

#### Professional Format Support
- **OpenImageIO integration** for EXR, HDR, PSD, IFF, RAW, and 16/32-bit TIFF
- **FFmpeg backend** for video playback (MOV, MP4, AVI, MKV, WMV)
- **Image sequence detection** with automatic pattern recognition
- **Tone mapping** for HDR images with exposure control

#### Performance Optimizations
- **Multi-threaded thumbnail generation** (2-8 threads based on CPU)
- **Smart thumbnail caching** (1000-item memory cache, ~250MB)
- **Database indexes** on frequently queried columns
- **Lazy loading** for on-demand thumbnail generation
- **Optimized queries** with prepared statements

#### Database Management
- **SQLite database** with automatic migrations
- **Export database** for backups
- **Import database** to restore from backup
- **Clear database** to start fresh
- **Automatic schema updates** for new features

#### UI/UX
- **Windows Explorer-like interface** with familiar navigation
- **Three-panel layout** (folders, assets, filters/info)
- **Resizable panels** with splitters
- **Hover effects** for better visual feedback
- **Progress dialogs** for long operations
- **Info panel** showing selected asset details
- **Toolbar** with view mode toggle and thumbnail size slider

### Technical Details
- **Qt 6.9.3 Widgets** for native Windows UI
- **C++20** with modern language features
- **CMake build system** with Ninja and Visual Studio support
- **MSVC 2022** compiler
- **vcpkg** for dependency management
- **Portable deployment** with all dependencies bundled

### Known Issues
- Thumbnail generation cannot be cancelled mid-import
- No keyboard shortcuts for rating and selection
- No undo/redo functionality


- Preview navigation wraps around (no boundary check)
- Large folders (1000+ assets) may have initial load delay

## [0.0.x] - Deprecated

### QML Implementation (Abandoned)
- Initial attempt using Qt Quick/QML
- Abandoned due to unreliable mouse event handling
- Modifier keys (Ctrl/Shift) not working consistently
- Complex event propagation issues
- Migrated to Qt Widgets for reliability

---

## Version History Summary

| Version | Date | Description |
|---------|------|-------------|
| 0.2.0 | 2025-10-29 | Folder-preserving Add to Library; folders-first sorting; preview focus restore; tree sync; crash fix |

| 0.1.0 | 2024-01-XX | Initial Qt Widgets release with full feature set |
| 0.0.x | 2023-12-XX | QML prototype (deprecated) |

---

## Upgrade Notes

### From 0.0.x to 0.1.0
- Complete rewrite from QML to Qt Widgets
- Database schema is compatible (no migration needed)
- Thumbnails are regenerated automatically
- All features from QML version are preserved and improved

---

## Contributing

See [DEVELOPER_GUIDE.md](DEVELOPER_GUIDE.md) for contribution guidelines.

---

## Support

- **User Guide**: [USER_GUIDE.md](USER_GUIDE.md)
- **Technical Docs**: [TECH.md](TECH.md)
- **API Reference**: [API_REFERENCE.md](API_REFERENCE.md)
- **GitHub Issues**: Report bugs and request features

---

## License

Proprietary - All rights reserved.

---

## Acknowledgments

- **Qt Framework** - Cross-platform UI framework
- **OpenImageIO** - Professional image I/O library
- **FFmpeg** - Video codec library
- **SQLite** - Embedded database engine

---

## Release Checklist

For maintainers preparing a release:

- [ ] Update version number in CMakeLists.txt
- [ ] Update CHANGELOG.md with release date
- [ ] Update README.md if needed
- [ ] Run full test suite
- [ ] Build release package
- [ ] Test on clean Windows installation
- [ ] Create Git tag
- [ ] Create GitHub release
- [ ] Update documentation links

---

## Future Roadmap

### Version 0.2.0 (Planned)
- Keyboard shortcuts for all operations
- Batch export with presets
- Custom metadata fields
- Smart folders with saved searches
- Improved performance for very large libraries (10,000+ assets)

### Version 0.3.0 (Planned)
- Cloud storage integration (Dropbox, Google Drive)
- Collection sharing and export
- Advanced search with query builder
- Metadata editing (EXIF, XMP, IPTC)
- Batch metadata operations

### Version 1.0.0 (Planned)
- Stable API
- Comprehensive test coverage
- Undo/redo system
- Plugin system for extensibility
- Cross-platform support (macOS, Linux)

---

## Breaking Changes

### 0.1.0
- None (initial release)

---

## Deprecations

### 0.1.0
- QML implementation completely removed
- No deprecated APIs in this release

---

## Security

### 0.1.0
- No known security vulnerabilities
- Database uses parameterized queries to prevent SQL injection
- File paths are validated before access
- No network communication (fully offline application)

---

## Performance Metrics

### 0.1.0
- **Import speed**: 4-8x faster than single-threaded (with multi-threading)
- **Database queries**: 10-100x faster with indexes
- **Thumbnail cache**: 5x larger (1000 vs 200 items)
- **Memory usage**: ~250MB for thumbnail cache (acceptable)
- **Startup time**: <2 seconds on SSD
- **Large folder load**: <1 second for 1000 assets

---

## Credits

Developed by Nick Pittas

Special thanks to:
- Qt Company for the excellent framework
- OpenImageIO contributors
- FFmpeg team
- SQLite developers
- All beta testers and early adopters

---

*Last updated: 2025-10-29*

