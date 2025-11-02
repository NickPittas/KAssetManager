# Everything Search Integration - Implementation Guide

## Overview
This document explains how to complete the Everything Search integration for KAssetManager. The core EverythingSearch class has been implemented, but requires the Everything SDK DLL to function.

## Status
✅ **COMPLETED:**
- EverythingSearch singleton class with DLL loading
- Function pointer declarations for Everything SDK API
- Search methods with result parsing
- Error handling and fallback logic

⏳ **REMAINING:**
- Download and integrate Everything SDK DLL
- Create hybrid search UI combining database + filesystem results
- Add bulk import from search results
- Test with actual Everything service

## Prerequisites

### 1. Install Everything
Download and install Everything from: https://www.voidtools.com/
- Everything must be running for the search to work
- Recommended: Enable "Run as administrator" for full disk access

### 2. Download Everything SDK
Download Everything-SDK.zip from: https://www.voidtools.com/support/everything/sdk/

Extract the SDK and copy the appropriate DLL:
- For 64-bit builds: Copy `Everything64.dll` to `E:\KAssetManager\third_party\everything\`
- For 32-bit builds: Copy `Everything.dll` to `E:\KAssetManager\third_party\everything\`

Alternatively, copy the DLL to the application's bin directory after building.

## Implementation Details

### EverythingSearch Class
Located in: `native/qt6/src/everything_search.{h,cpp}`

**Key Features:**
- Singleton pattern for global access
- Dynamic DLL loading with multiple search paths
- Function pointer resolution for all Everything SDK functions
- Blocking search with configurable max results
- File type filtering support
- Result metadata: path, size, date modified, folder flag

**Usage Example:**
```cpp
EverythingSearch& search = EverythingSearch::instance();
if (search.initialize()) {
    // Search for all EXR files
    QVector<EverythingResult> results = search.searchWithFilter("", "exr", 1000);
    
    for (const auto& result : results) {
        qDebug() << result.fullPath << result.size << result.isImported;
    }
}
```

### Everything SDK Functions Used
- `Everything_SetSearchW` - Set search query
- `Everything_QueryW` - Execute search (blocking)
- `Everything_GetNumResults` - Get result count
- `Everything_GetResultFileNameW` - Get file name
- `Everything_GetResultPathW` - Get directory path
- `Everything_GetResultSize` - Get file size
- `Everything_GetResultDateModified` - Get modification date
- `Everything_GetResultAttributes` - Get file attributes (folder flag)
- `Everything_IsDBLoaded` - Check if Everything service is running

## Next Steps

### Step 1: Add Everything DLL to Build
1. Create `third_party/everything/` directory
2. Download Everything SDK and extract `Everything64.dll`
3. Copy DLL to `third_party/everything/Everything64.dll`
4. Update CMakeLists.txt to copy DLL to output directory:

```cmake
# Copy Everything DLL to output directory
if(EXISTS "${CMAKE_SOURCE_DIR}/../../third_party/everything/Everything64.dll")
    install(FILES "${CMAKE_SOURCE_DIR}/../../third_party/everything/Everything64.dll"
            DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
endif()
```

### Step 2: Add EverythingSearch to Build
Add to `native/qt6/CMakeLists.txt`:
```cmake
src/everything_search.h
src/everything_search.cpp
```

### Step 3: Create Hybrid Search UI
Create a new search dialog that combines:
- Database search results (existing assets)
- Everything filesystem search results (all files on disk)
- Visual indicators for imported vs. not-imported files
- Bulk import button for selected files

**Suggested UI Layout:**
```
┌─────────────────────────────────────────────────┐
│ Search: [________________] [Search] [Filters▼]  │
├─────────────────────────────────────────────────┤
│ ☑ Search Database  ☑ Search Filesystem (Everything) │
├─────────────────────────────────────────────────┤
│ Results (1,234 found)                           │
│ ┌───────────────────────────────────────────┐   │
│ │ ✓ shot_001.exr    (Imported)   2.3 MB    │   │
│ │ ○ shot_002.exr    (Not imported) 2.4 MB  │   │
│ │ ○ shot_003.exr    (Not imported) 2.5 MB  │   │
│ └───────────────────────────────────────────┘   │
├─────────────────────────────────────────────────┤
│ [Import Selected (234)] [Cancel]                │
└─────────────────────────────────────────────────┘
```

### Step 4: Implement Hybrid Search
```cpp
QVector<SearchResult> performHybridSearch(const QString& query) {
    QVector<SearchResult> results;
    
    // 1. Search database
    // ... existing database search code ...
    
    // 2. Search filesystem with Everything
    if (EverythingSearch::instance().isAvailable()) {
        auto fsResults = EverythingSearch::instance().search(query);
        
        // Mark which files are already imported
        for (auto& result : fsResults) {
            result.isImported = DB::instance().getAssetIdByPath(result.fullPath) > 0;
        }
        
        // Merge results
        results.append(fsResults);
    }
    
    return results;
}
```

### Step 5: Add Bulk Import
```cpp
void importFromEverythingResults(const QVector<EverythingResult>& results, int targetFolderId) {
    QStringList filePaths;
    for (const auto& result : results) {
        if (!result.isImported && !result.isFolder) {
            filePaths.append(result.fullPath);
        }
    }
    
    // Use existing importer
    importer->importFiles(filePaths, targetFolderId);
}
```

## Testing

### Manual Testing Steps
1. Install Everything and ensure it's running
2. Copy Everything64.dll to application directory
3. Launch KAssetManager
4. Check logs for "[EverythingSearch] Initialized successfully"
5. Open hybrid search dialog
6. Search for a file type (e.g., "*.exr")
7. Verify results show both imported and non-imported files
8. Select non-imported files and click "Import Selected"
9. Verify files are imported successfully

### Error Handling
The implementation handles these scenarios:
- Everything DLL not found → Graceful fallback, search disabled
- Everything service not running → Warning message, search disabled
- Query fails → Error logged, empty results returned
- Invalid results → Skipped, logged

## Performance Considerations
- Everything searches are extremely fast (< 100ms for millions of files)
- Limit max results to 1000-5000 to avoid UI lag
- Use file type filters to narrow results
- Consider pagination for large result sets

## Future Enhancements
1. **Real-time search** - Update results as user types
2. **Advanced filters** - Date range, size range, path filters
3. **Search history** - Save and recall previous searches
4. **Saved searches** - Create named search presets
5. **Watch folders** - Auto-import new files matching search criteria

## References
- Everything Homepage: https://www.voidtools.com/
- Everything SDK: https://www.voidtools.com/support/everything/sdk/
- Everything Search Syntax: https://www.voidtools.com/support/everything/searching/

