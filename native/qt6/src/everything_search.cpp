#include "everything_search.h"
#include "db.h"
#include <QFileInfo>
#include <QDir>
#include <QDebug>
#include <windows.h>

// Everything SDK function pointer typedefs
typedef void (__stdcall *Everything_SetSearchW_t)(LPCWSTR lpString);
typedef void (__stdcall *Everything_SetMatchCase_t)(BOOL bEnable);
typedef void (__stdcall *Everything_SetMatchWholeWord_t)(BOOL bEnable);
typedef void (__stdcall *Everything_SetRegex_t)(BOOL bEnable);
typedef void (__stdcall *Everything_SetMax_t)(DWORD dwMax);
typedef void (__stdcall *Everything_SetOffset_t)(DWORD dwOffset);
typedef BOOL (__stdcall *Everything_QueryW_t)(BOOL bWait);
typedef DWORD (__stdcall *Everything_GetNumResults_t)();
typedef LPCWSTR (__stdcall *Everything_GetResultFileNameW_t)(DWORD index);
typedef LPCWSTR (__stdcall *Everything_GetResultPathW_t)(DWORD index);
typedef BOOL (__stdcall *Everything_GetResultSize_t)(DWORD index, LARGE_INTEGER *lpSize);
typedef BOOL (__stdcall *Everything_GetResultDateModified_t)(DWORD index, FILETIME *lpDateModified);
typedef DWORD (__stdcall *Everything_GetResultAttributes_t)(DWORD index);
typedef DWORD (__stdcall *Everything_GetLastError_t)();
typedef DWORD (__stdcall *Everything_GetMajorVersion_t)();
typedef DWORD (__stdcall *Everything_GetMinorVersion_t)();
typedef DWORD (__stdcall *Everything_GetRevision_t)();
typedef BOOL (__stdcall *Everything_IsDBLoaded_t)();

EverythingSearch& EverythingSearch::instance() {
    static EverythingSearch instance;
    return instance;
}

EverythingSearch::EverythingSearch()
    : m_library(nullptr)
    , m_available(false)
    , m_setSearch(nullptr)
    , m_setMatchCase(nullptr)
    , m_setMatchWholeWord(nullptr)
    , m_setRegex(nullptr)
    , m_setMax(nullptr)
    , m_setOffset(nullptr)
    , m_query(nullptr)
    , m_getNumResults(nullptr)
    , m_getResultFileName(nullptr)
    , m_getResultPath(nullptr)
    , m_getResultSize(nullptr)
    , m_getResultDateModified(nullptr)
    , m_getResultAttributes(nullptr)
    , m_getLastError(nullptr)
    , m_getMajorVersion(nullptr)
    , m_getMinorVersion(nullptr)
    , m_getRevision(nullptr)
    , m_isDBLoaded(nullptr)
{
}

EverythingSearch::~EverythingSearch() {
    unloadDLL();
}

bool EverythingSearch::initialize() {
    if (m_available) {
        return true;
    }
    
    if (!loadDLL()) {
        qWarning() << "[EverythingSearch] Failed to load Everything DLL";
        qWarning() << "[EverythingSearch] Please download Everything SDK from https://www.voidtools.com/support/everything/sdk/";
        qWarning() << "[EverythingSearch] Extract Everything64.dll to application directory or system PATH";
        return false;
    }
    
    if (!loadFunctions()) {
        qWarning() << "[EverythingSearch] Failed to load Everything functions";
        unloadDLL();
        return false;
    }
    
    // Check if Everything service is running
    if (!isEverythingRunning()) {
        qWarning() << "[EverythingSearch] Everything service is not running";
        qWarning() << "[EverythingSearch] Please install and run Everything from https://www.voidtools.com/";
        unloadDLL();
        return false;
    }
    
    m_available = true;
    qInfo() << "[EverythingSearch] Initialized successfully - Version:" << getVersion();
    return true;
}

bool EverythingSearch::loadDLL() {
    // Try to load Everything64.dll from multiple locations
    QStringList searchPaths = {
        "Everything64.dll",                          // Current directory
        "Everything.dll",                            // 32-bit fallback
        QDir::currentPath() + "/Everything64.dll",  // Explicit current path
        "third_party/everything/Everything64.dll",   // Third-party directory
        "C:/Program Files/Everything/Everything64.dll" // Default installation
    };
    
    for (const QString& path : searchPaths) {
        m_library = new QLibrary(path);
        if (m_library->load()) {
            qInfo() << "[EverythingSearch] Loaded DLL from:" << path;
            return true;
        }
        delete m_library;
        m_library = nullptr;
    }
    
    return false;
}

void EverythingSearch::unloadDLL() {
    if (m_library) {
        m_library->unload();
        delete m_library;
        m_library = nullptr;
    }
    m_available = false;
}

bool EverythingSearch::loadFunctions() {
    if (!m_library) return false;
    
    // Load all required function pointers
    m_setSearch = (Everything_SetSearchW_t)m_library->resolve("Everything_SetSearchW");
    m_setMatchCase = (Everything_SetMatchCase_t)m_library->resolve("Everything_SetMatchCase");
    m_setMatchWholeWord = (Everything_SetMatchWholeWord_t)m_library->resolve("Everything_SetMatchWholeWord");
    m_setRegex = (Everything_SetRegex_t)m_library->resolve("Everything_SetRegex");
    m_setMax = (Everything_SetMax_t)m_library->resolve("Everything_SetMax");
    m_setOffset = (Everything_SetOffset_t)m_library->resolve("Everything_SetOffset");
    m_query = (Everything_QueryW_t)m_library->resolve("Everything_QueryW");
    m_getNumResults = (Everything_GetNumResults_t)m_library->resolve("Everything_GetNumResults");
    m_getResultFileName = (Everything_GetResultFileNameW_t)m_library->resolve("Everything_GetResultFileNameW");
    m_getResultPath = (Everything_GetResultPathW_t)m_library->resolve("Everything_GetResultPathW");
    m_getResultSize = (Everything_GetResultSize_t)m_library->resolve("Everything_GetResultSize");
    m_getResultDateModified = (Everything_GetResultDateModified_t)m_library->resolve("Everything_GetResultDateModified");
    m_getResultAttributes = (Everything_GetResultAttributes_t)m_library->resolve("Everything_GetResultAttributes");
    m_getLastError = (Everything_GetLastError_t)m_library->resolve("Everything_GetLastError");
    m_getMajorVersion = (Everything_GetMajorVersion_t)m_library->resolve("Everything_GetMajorVersion");
    m_getMinorVersion = (Everything_GetMinorVersion_t)m_library->resolve("Everything_GetMinorVersion");
    m_getRevision = (Everything_GetRevision_t)m_library->resolve("Everything_GetRevision");
    m_isDBLoaded = (Everything_IsDBLoaded_t)m_library->resolve("Everything_IsDBLoaded");
    
    // Check if all critical functions loaded
    if (!m_setSearch || !m_query || !m_getNumResults || !m_getResultFileName || !m_getResultPath) {
        qWarning() << "[EverythingSearch] Failed to load critical functions";
        return false;
    }
    
    return true;
}

bool EverythingSearch::isEverythingRunning() const {
    if (!m_isDBLoaded) return false;
    auto func = reinterpret_cast<Everything_IsDBLoaded_t>(m_isDBLoaded);
    return func() != 0;
}

QString EverythingSearch::getVersion() const {
    if (!m_getMajorVersion || !m_getMinorVersion || !m_getRevision) {
        return "Unknown";
    }

    auto getMajor = reinterpret_cast<Everything_GetMajorVersion_t>(m_getMajorVersion);
    auto getMinor = reinterpret_cast<Everything_GetMinorVersion_t>(m_getMinorVersion);
    auto getRev = reinterpret_cast<Everything_GetRevision_t>(m_getRevision);

    DWORD major = getMajor();
    DWORD minor = getMinor();
    DWORD revision = getRev();

    return QString("%1.%2.%3").arg(major).arg(minor).arg(revision);
}

QVector<EverythingResult> EverythingSearch::search(const QString& query, int maxResults) {
    QVector<EverythingResult> results;
    
    if (!m_available) {
        qWarning() << "[EverythingSearch] Not initialized";
        return results;
    }
    
    // Cast function pointers
    auto setSearch = reinterpret_cast<Everything_SetSearchW_t>(m_setSearch);
    auto setMatchCase = reinterpret_cast<Everything_SetMatchCase_t>(m_setMatchCase);
    auto setMatchWholeWord = reinterpret_cast<Everything_SetMatchWholeWord_t>(m_setMatchWholeWord);
    auto setRegex = reinterpret_cast<Everything_SetRegex_t>(m_setRegex);
    auto setMax = reinterpret_cast<Everything_SetMax_t>(m_setMax);
    auto setOffset = reinterpret_cast<Everything_SetOffset_t>(m_setOffset);
    auto query_func = reinterpret_cast<Everything_QueryW_t>(m_query);
    auto getNumResults = reinterpret_cast<Everything_GetNumResults_t>(m_getNumResults);
    auto getLastError = reinterpret_cast<Everything_GetLastError_t>(m_getLastError);
    auto getResultFileName = reinterpret_cast<Everything_GetResultFileNameW_t>(m_getResultFileName);
    auto getResultPath = reinterpret_cast<Everything_GetResultPathW_t>(m_getResultPath);
    auto getResultSize = reinterpret_cast<Everything_GetResultSize_t>(m_getResultSize);
    auto getResultDateModified = reinterpret_cast<Everything_GetResultDateModified_t>(m_getResultDateModified);
    auto getResultAttributes = reinterpret_cast<Everything_GetResultAttributes_t>(m_getResultAttributes);

    // Set search parameters
    setSearch(reinterpret_cast<LPCWSTR>(query.utf16()));
    setMatchCase(FALSE);
    setMatchWholeWord(FALSE);
    setRegex(FALSE);
    setMax(maxResults);
    setOffset(0);

    // Execute query (blocking)
    if (!query_func(TRUE)) {
        DWORD error = getLastError();
        qWarning() << "[EverythingSearch] Query failed with error:" << error;
        return results;
    }

    // Get results
    DWORD numResults = getNumResults();
    qInfo() << "[EverythingSearch] Found" << numResults << "results for query:" << query;
    
    // Get all asset paths from database for quick lookup
    QSet<QString> importedPaths;
    // Note: This would require a new DB method to get all asset paths efficiently
    // For now, we'll mark all as not imported
    
    for (DWORD i = 0; i < numResults; ++i) {
        EverythingResult result;

        // Get file name
        LPCWSTR fileName = getResultFileName(i);
        if (fileName) {
            result.fileName = QString::fromWCharArray(fileName);
        }

        // Get path
        LPCWSTR path = getResultPath(i);
        if (path) {
            result.directory = QString::fromWCharArray(path);
            result.fullPath = result.directory + "\\" + result.fileName;
        }

        // Get size
        LARGE_INTEGER size;
        if (getResultSize && getResultSize(i, &size)) {
            result.size = size.QuadPart;
        } else {
            result.size = 0;
        }

        // Get date modified
        FILETIME ft;
        if (getResultDateModified && getResultDateModified(i, &ft)) {
            ULARGE_INTEGER ull;
            ull.LowPart = ft.dwLowDateTime;
            ull.HighPart = ft.dwHighDateTime;
            qint64 timestamp = (ull.QuadPart - 116444736000000000LL) / 10000000LL;
            result.dateModified = QDateTime::fromSecsSinceEpoch(timestamp);
        }

        // Check if folder
        DWORD attributes = getResultAttributes ? getResultAttributes(i) : 0;
        result.isFolder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        
        // Check if already imported
        result.isImported = importedPaths.contains(result.fullPath);
        
        results.append(result);
    }
    
    return results;
}

QVector<EverythingResult> EverythingSearch::searchWithFilter(const QString& query, const QString& fileTypes, int maxResults) {
    // Build Everything search query with file type filter
    // Everything syntax: ext:exr;jpg;png
    QString fullQuery = query;
    
    if (!fileTypes.isEmpty()) {
        QStringList types = fileTypes.split(';', Qt::SkipEmptyParts);
        if (!types.isEmpty()) {
            fullQuery += " ext:" + types.join(';');
        }
    }
    
    return search(fullQuery, maxResults);
}

