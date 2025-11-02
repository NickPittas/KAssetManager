#ifndef EVERYTHING_SEARCH_H
#define EVERYTHING_SEARCH_H

#include <QString>
#include <QVector>
#include <QDateTime>
#include <QLibrary>
#include <functional>

// Everything SDK function pointers
// Download Everything SDK from: https://www.voidtools.com/support/everything/sdk/
// Extract Everything.dll and Everything.lib to third_party/everything/

struct EverythingResult {
    QString fullPath;
    QString fileName;
    QString directory;
    qint64 size;
    QDateTime dateModified;
    bool isFolder;
    bool isImported; // Set to true if already in database
};

class EverythingSearch {
public:
    static EverythingSearch& instance();
    
    // Initialize Everything SDK
    bool initialize();
    bool isAvailable() const { return m_available; }
    
    // Search operations
    QVector<EverythingResult> search(const QString& query, int maxResults = 1000);
    QVector<EverythingResult> searchWithFilter(const QString& query, const QString& fileTypes, int maxResults = 1000);
    
    // Utility functions
    bool isEverythingRunning() const;
    QString getVersion() const;
    
private:
    EverythingSearch();
    ~EverythingSearch();
    EverythingSearch(const EverythingSearch&) = delete;
    EverythingSearch& operator=(const EverythingSearch&) = delete;
    
    bool loadDLL();
    void unloadDLL();
    bool loadFunctions();

    QLibrary* m_library;
    bool m_available;

    // Function pointers (stored as void* to avoid Windows type dependencies in header)
    void* m_setSearch;
    void* m_setMatchCase;
    void* m_setMatchWholeWord;
    void* m_setRegex;
    void* m_setMax;
    void* m_setOffset;
    void* m_query;
    void* m_getNumResults;
    void* m_getResultFileName;
    void* m_getResultPath;
    void* m_getResultSize;
    void* m_getResultDateModified;
    void* m_getResultAttributes;
    void* m_getLastError;
    void* m_getMajorVersion;
    void* m_getMinorVersion;
    void* m_getRevision;
    void* m_isDBLoaded;
};

#endif // EVERYTHING_SEARCH_H

