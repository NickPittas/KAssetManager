#ifdef _WIN32
#include "virtual_drag.h"
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <Ole2.h>
#include <objidl.h>
#include <QByteArray>
#include <QString>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QFileInfo>

static UINT CF_FILEDESCRIPTORW_ID() { static UINT id = RegisterClipboardFormatW(CFSTR_FILEDESCRIPTORW); return id; }
static UINT CF_FILECONTENTS_ID()   { static UINT id = RegisterClipboardFormatW(CFSTR_FILECONTENTS);   return id; }
static UINT CF_PREFERREDDROPEFFECT_ID() { static UINT id = RegisterClipboardFormatW(CFSTR_PREFERREDDROPEFFECT); return id; }

static FORMATETC makeFormatEtc(UINT cf, DWORD tymed) {
    FORMATETC fe{}; fe.cfFormat = (CLIPFORMAT)cf; fe.dwAspect = DVASPECT_CONTENT; fe.lindex = -1; fe.tymed = tymed; fe.ptd = nullptr; return fe;
}

class SimpleDropSource : public IDropSource {
    LONG m_refs = 1;
public:
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDropSource) { *ppv = static_cast<IDropSource*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refs); }
    ULONG __stdcall Release() override { ULONG r = InterlockedDecrement(&m_refs); if (!r) delete this; return r; }

    HRESULT __stdcall QueryContinueDrag(BOOL fEscapePressed, DWORD grfKeyState) override {
        if (fEscapePressed) return DRAGDROP_S_CANCEL;
        if (!(grfKeyState & MK_LBUTTON)) return DRAGDROP_S_DROP;
        return S_OK;
    }
    HRESULT __stdcall GiveFeedback(DWORD) override { return DRAGDROP_S_USEDEFAULTCURSORS; }
};


class FormatEtcEnum : public IEnumFORMATETC {
    LONG m_refs = 1;
    FORMATETC m_items[4]{};
    ULONG m_count = 0;
    ULONG m_index = 0;
public:
    FormatEtcEnum(const FORMATETC *items, ULONG count) {
        m_count = (count > 4) ? 4 : count;
        for (ULONG i = 0; i < m_count; ++i) m_items[i] = items[i];
    }
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IEnumFORMATETC) { *ppv = static_cast<IEnumFORMATETC*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refs); }
    ULONG __stdcall Release() override { ULONG r = InterlockedDecrement(&m_refs); if (!r) delete this; return r; }

    HRESULT __stdcall Next(ULONG celt, FORMATETC *rgelt, ULONG *pceltFetched) override {
        if (!rgelt) return E_POINTER;
        ULONG fetched = 0;
        while (fetched < celt && m_index < m_count) {
            rgelt[fetched++] = m_items[m_index++];
        }
        if (pceltFetched) *pceltFetched = fetched;
        return (fetched == celt) ? S_OK : S_FALSE;
    }
    HRESULT __stdcall Skip(ULONG celt) override {
        m_index = (m_index + celt > m_count) ? m_count : m_index + celt;
        return (m_index < m_count) ? S_OK : S_FALSE;
    }
    HRESULT __stdcall Reset() override { m_index = 0; return S_OK; }
    HRESULT __stdcall Clone(IEnumFORMATETC **ppenum) override {
        if (!ppenum) return E_POINTER;
        auto *e = new FormatEtcEnum(m_items, m_count);
        e->m_index = m_index;
        *ppenum = e;
        return S_OK;
    }
};
class VirtualFileDataObject : public IDataObject {
    LONG m_refs = 1;
    FORMATETC m_fmtHdrop = makeFormatEtc(CF_HDROP, TYMED_HGLOBAL);
    QTemporaryDir m_tmpDir; // lifetime tied to this data object
    QVector<QString> m_tmpPaths;
    bool m_tmpCreated = false;

private:
    bool ensureTempFilesCreated() {
        if (m_tmpCreated) return true;
        if (!m_tmpDir.isValid()) {
            // Create a temp dir with a stable prefix
            if (!m_tmpDir.isValid()) return false;
        }
        m_tmpPaths.clear(); m_tmpPaths.reserve(m_files.size());
        for (const auto &vf : m_files) {
            const QString path = m_tmpDir.filePath(vf.name);
            // Ensure parent exists
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
            if (vf.data.size() > 0) f.write(vf.data.constData(), vf.data.size());
            f.close();
            m_tmpPaths.push_back(path);
        }
        m_tmpCreated = true;
        return true;
    }

    QVector<VirtualDrag::VirtualFile> m_files;
    FORMATETC m_fmtDesc = makeFormatEtc(CF_FILEDESCRIPTORW_ID(), TYMED_HGLOBAL);
    FORMATETC m_fmtContents = makeFormatEtc(CF_FILECONTENTS_ID(), TYMED_ISTREAM);
    FORMATETC m_fmtPrefEffect = makeFormatEtc(CF_PREFERREDDROPEFFECT_ID(), TYMED_HGLOBAL);
public:
    VirtualFileDataObject(const QVector<VirtualDrag::VirtualFile> &files)
        : m_files(files) {}

    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refs); }
    ULONG __stdcall Release() override { ULONG r = InterlockedDecrement(&m_refs); if (!r) delete this; return r; }

    // IDataObject
    HRESULT __stdcall GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override {
        if (!pformatetcIn || !pmedium) return E_INVALIDARG;
        if (pformatetcIn->cfFormat == m_fmtDesc.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
            const UINT n = (UINT)m_files.size();
            const SIZE_T sz = sizeof(FILEGROUPDESCRIPTORW) + (n ? (n - 1) : 0) * sizeof(FILEDESCRIPTORW);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sz);
            if (!h) return E_OUTOFMEMORY;
            auto *fgd = static_cast<FILEGROUPDESCRIPTORW*>(GlobalLock(h));
            fgd->cItems = n;
            FILETIME now; GetSystemTimeAsFileTime(&now);
            for (UINT i = 0; i < n; ++i) {
                FILEDESCRIPTORW &fd = fgd->fgd[i];
                const auto &vf = m_files[(int)i];
                const quint64 sz64 = (quint64)vf.data.size();
                fd.dwFlags = FD_FILESIZE | FD_ATTRIBUTES | FD_UNICODE | FD_WRITESTIME;
                fd.nFileSizeHigh = (DWORD)(sz64 >> 32);
                fd.nFileSizeLow  = (DWORD)(sz64 & 0xFFFFFFFF);
                fd.dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
                fd.ftLastWriteTime = now;
                const std::wstring wname = vf.name.toStdWString();
                wcsncpy(fd.cFileName, wname.c_str(), MAX_PATH - 1);
                fd.cFileName[MAX_PATH - 1] = L'\0';

            }
            GlobalUnlock(h);
            pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (pformatetcIn->cfFormat == m_fmtContents.cfFormat && (pformatetcIn->tymed & TYMED_ISTREAM)) {
            LONG idx = pformatetcIn->lindex;
            if (idx < 0) idx = 0;
            if (idx >= (LONG)m_files.size()) return DV_E_LINDEX;
            const auto &vf = m_files[idx];
            HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, vf.data.size());
            if (!mem) return E_OUTOFMEMORY;
            void *ptr = GlobalLock(mem);
            memcpy(ptr, vf.data.constData(), (SIZE_T)vf.data.size());
            GlobalUnlock(mem);
            IStream *stm = nullptr;
            if (FAILED(CreateStreamOnHGlobal(mem, TRUE, &stm))) { GlobalFree(mem); return E_FAIL; }
            LARGE_INTEGER li{}; stm->Seek(li, STREAM_SEEK_SET, nullptr);
            pmedium->tymed = TYMED_ISTREAM; pmedium->pstm = stm; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (pformatetcIn->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            if (!h) return E_OUTOFMEMORY;
            auto *p = static_cast<DWORD*>(GlobalLock(h));
            *p = DROPEFFECT_COPY; // hint copy
            GlobalUnlock(h);
            pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        // Offer CF_HDROP as automatic fallback (creates temp files on demand)
        if (pformatetcIn->cfFormat == m_fmtHdrop.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
            if (!ensureTempFilesCreated()) return E_FAIL;
            // Compute wchar buffer size: each path + null, final extra null
            size_t totalChars = 0;
            for (const auto &p : m_tmpPaths) totalChars += (size_t)p.toStdWString().size() + 1;
            totalChars += 1; // final terminator
            SIZE_T bytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!h) return E_OUTOFMEMORY;
            auto *df = static_cast<DROPFILES*>(GlobalLock(h));
            df->pFiles = sizeof(DROPFILES);
            df->pt.x = 0; df->pt.y = 0; df->fNC = FALSE; df->fWide = TRUE;
            wchar_t *dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
            for (const auto &p : m_tmpPaths) {
                const std::wstring w = p.toStdWString();
                memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                dst += w.size() + 1;
            }
            *dst = L'\0';
            GlobalUnlock(h);
            pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }

    HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT __stdcall QueryGetData(FORMATETC *pformatetc) override {
        if (!pformatetc) return E_INVALIDARG;
        if ((pformatetc->cfFormat == m_fmtDesc.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL)) ||
            (pformatetc->cfFormat == m_fmtContents.cfFormat && (pformatetc->tymed & TYMED_ISTREAM)) ||
            (pformatetc->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL)) ||
            (pformatetc->cfFormat == m_fmtHdrop.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL))) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC*, FORMATETC *pformatetcOut) override { if (pformatetcOut) pformatetcOut->ptd = nullptr; return E_NOTIMPL; }
    HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override {
        if (!ppenumFormatEtc) return E_POINTER;
        *ppenumFormatEtc = nullptr;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;
        FORMATETC f[4] = { m_fmtDesc, m_fmtContents, m_fmtPrefEffect, m_fmtHdrop };
        auto *e = new FormatEtcEnum(f, 4);
        *ppenumFormatEtc = e;
        return S_OK;
    }
    HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

namespace VirtualDrag {
bool startVirtualDrag(const QVector<VirtualFile> &files) {
    if (files.isEmpty()) return false;
    HRESULT hr = OleInitialize(nullptr);
    auto *obj = new VirtualFileDataObject(files);
    IDataObject *pDataObject = static_cast<IDataObject*>(obj);
    IDropSource *pDropSource = new SimpleDropSource();
    DWORD effect = DROPEFFECT_COPY;
    HRESULT r = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY, &effect);
    pDropSource->Release();
    pDataObject->Release();
    if (SUCCEEDED(hr)) OleUninitialize();
    return r == DRAGDROP_S_DROP || r == DRAGDROP_S_CANCEL || r == S_OK;
}

// IDataObject for real file paths (CF_HDROP + PREFERREDDROPEFFECT)
class RealPathsDataObject : public IDataObject {
    LONG m_refs = 1;
    QVector<QString> m_paths;
    FORMATETC m_fmtHdrop = makeFormatEtc(CF_HDROP, TYMED_HGLOBAL);
    FORMATETC m_fmtPrefEffect = makeFormatEtc(CF_PREFERREDDROPEFFECT_ID(), TYMED_HGLOBAL);
public:
    RealPathsDataObject(const QVector<QString> &paths) : m_paths(paths) {}
    // IUnknown
    HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override {
        if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refs); }
    ULONG __stdcall Release() override { ULONG r = InterlockedDecrement(&m_refs); if (!r) delete this; return r; }

    // IDataObject
    HRESULT __stdcall GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override {
        if (!pformatetcIn || !pmedium) return E_INVALIDARG;
        if (pformatetcIn->cfFormat == m_fmtHdrop.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
            // Compute wchar buffer size for all paths + final terminator
            size_t totalChars = 0;
            for (const auto &p : m_paths) totalChars += (size_t)p.toStdWString().size() + 1;
            totalChars += 1;
            SIZE_T bytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!h) return E_OUTOFMEMORY;
            auto *df = static_cast<DROPFILES*>(GlobalLock(h));
            df->pFiles = sizeof(DROPFILES);
            df->pt.x = 0; df->pt.y = 0; df->fNC = FALSE; df->fWide = TRUE;
            wchar_t *dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
            for (const auto &p : m_paths) {
                const std::wstring w = p.toStdWString();
                memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                dst += w.size() + 1;
            }
            *dst = L'\0';
            GlobalUnlock(h);
            pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        if (pformatetcIn->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
            if (!h) return E_OUTOFMEMORY;
            auto *p = static_cast<DWORD*>(GlobalLock(h));
            *p = DROPEFFECT_COPY;
            GlobalUnlock(h);
            pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
            return S_OK;
        }
        return DV_E_FORMATETC;
    }
    HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
    HRESULT __stdcall QueryGetData(FORMATETC *pformatetc) override {
        if (!pformatetc) return E_INVALIDARG;
        if ((pformatetc->cfFormat == m_fmtHdrop.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL)) ||
            (pformatetc->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL))) return S_OK;
        return DV_E_FORMATETC;
    }
    HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC*, FORMATETC *pformatetcOut) override { if (pformatetcOut) pformatetcOut->ptd = nullptr; return E_NOTIMPL; }
    HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
    HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override {
        if (!ppenumFormatEtc) return E_POINTER;
        *ppenumFormatEtc = nullptr;
        if (dwDirection != DATADIR_GET) return E_NOTIMPL;
        FORMATETC f[2] = { m_fmtHdrop, m_fmtPrefEffect };
        auto *e = new FormatEtcEnum(f, 2);
        *ppenumFormatEtc = e;
        return S_OK;
    }
    HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
    HRESULT __stdcall EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
};

    // IDataObject that adapts CF_HDROP payload (frames vs folder) based on drop target process
    class AdaptivePathsDataObject : public IDataObject {
        LONG m_refs = 1;
        QVector<QString> m_frames;       // expanded frame files
        QVector<QString> m_folders;      // one or more folders (unique)
        FORMATETC m_fmtHdrop = makeFormatEtc(CF_HDROP, TYMED_HGLOBAL);
        FORMATETC m_fmtPrefEffect = makeFormatEtc(CF_PREFERREDDROPEFFECT_ID(), TYMED_HGLOBAL);

        static bool processNameFromPoint(wchar_t outPath[MAX_PATH]) {
            POINT pt; GetCursorPos(&pt);
            HWND hwnd = WindowFromPoint(pt);
            if (!hwnd) return false;
            HWND root = GetAncestor(hwnd, GA_ROOT);
            DWORD pid = 0; GetWindowThreadProcessId(root ? root : hwnd, &pid);
            if (!pid) return false;
            HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
            if (!hProc) return false;
            DWORD size = MAX_PATH;
            BOOL ok = QueryFullProcessImageNameW(hProc, 0, outPath, &size);
            CloseHandle(hProc);
            return ok == TRUE;
        }
        static bool isExplorerOrSelf() {
            // First, detect by window class of the root window under the cursor
            POINT pt; GetCursorPos(&pt);
            HWND hwnd = WindowFromPoint(pt);
            if (!hwnd) return false; // default to DCC behavior (folders)
            HWND root = GetAncestor(hwnd, GA_ROOT);
            if (!root) root = hwnd;
            wchar_t cls[64]{};
            if (GetClassNameW(root, cls, (int)(sizeof(cls)/sizeof(cls[0])))) {
                if (_wcsicmp(cls, L"CabinetWClass") == 0 || _wcsicmp(cls, L"WorkerW") == 0 || _wcsicmp(cls, L"Progman") == 0) {
                    return true; // Explorer window, desktop, or worker window
                }
            }
            // Fallback to process name check
            DWORD pid = 0; GetWindowThreadProcessId(root, &pid);
            if (pid) {
                HANDLE hProc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
                if (hProc) {
                    wchar_t procPath[MAX_PATH]{}; DWORD size = MAX_PATH;
                    if (QueryFullProcessImageNameW(hProc, 0, procPath, &size)) {
                        const wchar_t *base = wcsrchr(procPath, L'\\'); base = base ? base + 1 : procPath;
                        if (_wcsicmp(base, L"explorer.exe") == 0 || _wcsicmp(base, L"FileExplorer.exe") == 0) {
                            CloseHandle(hProc);
                            return true;
                        }
                        // Our own exe
                        wchar_t selfPath[MAX_PATH]{}; GetModuleFileNameW(nullptr, selfPath, MAX_PATH);
                        const wchar_t *selfBase = wcsrchr(selfPath, L'\\'); selfBase = selfBase ? selfBase + 1 : selfPath;
                        if (_wcsicmp(base, selfBase) == 0) { CloseHandle(hProc); return true; }
                    }
                    CloseHandle(hProc);
                }
            }
            return false;
        }
        static HGLOBAL makeHdrop(const QVector<QString>& paths) {
            // Compute wchar buffer size for all paths + final terminator
            size_t totalChars = 0;
            for (const auto &p : paths) totalChars += (size_t)p.toStdWString().size() + 1;
            totalChars += 1;
            SIZE_T bytes = sizeof(DROPFILES) + totalChars * sizeof(wchar_t);
            HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
            if (!h) return nullptr;
            auto *df = static_cast<DROPFILES*>(GlobalLock(h));
            df->pFiles = sizeof(DROPFILES);
            df->pt.x = 0; df->pt.y = 0; df->fNC = FALSE; df->fWide = TRUE;
            wchar_t *dst = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(df) + sizeof(DROPFILES));
            for (const auto &p : paths) {
                const std::wstring w = p.toStdWString();
                memcpy(dst, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
                dst += w.size() + 1;
            }
            *dst = L'\0';
            GlobalUnlock(h);
            return h;
        }
    public:
        AdaptivePathsDataObject(const QVector<QString>& frames, const QVector<QString>& folders)
            : m_frames(frames), m_folders(folders) {}
        // IUnknown
        HRESULT __stdcall QueryInterface(REFIID riid, void **ppv) override {
            if (riid == IID_IUnknown || riid == IID_IDataObject) { *ppv = static_cast<IDataObject*>(this); AddRef(); return S_OK; }
            *ppv = nullptr; return E_NOINTERFACE;
        }
        ULONG __stdcall AddRef() override { return InterlockedIncrement(&m_refs); }
        ULONG __stdcall Release() override { ULONG r = InterlockedDecrement(&m_refs); if (!r) delete this; return r; }
        // IDataObject
        HRESULT __stdcall GetData(FORMATETC *pformatetcIn, STGMEDIUM *pmedium) override {
            if (!pformatetcIn || !pmedium) return E_INVALIDARG;
            if (pformatetcIn->cfFormat == m_fmtHdrop.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
                const bool framesMode = isExplorerOrSelf();
                const QVector<QString>& out = framesMode ? m_frames : (!m_folders.isEmpty() ? m_folders : m_frames);
                if (out.isEmpty()) return DV_E_FORMATETC;
                HGLOBAL h = makeHdrop(out);
                if (!h) return E_OUTOFMEMORY;
                pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
                return S_OK;
            }
            if (pformatetcIn->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetcIn->tymed & TYMED_HGLOBAL)) {
                HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, sizeof(DWORD));
                if (!h) return E_OUTOFMEMORY;
                auto *p = static_cast<DWORD*>(GlobalLock(h));
                *p = DROPEFFECT_COPY;
                GlobalUnlock(h);
                pmedium->tymed = TYMED_HGLOBAL; pmedium->hGlobal = h; pmedium->pUnkForRelease = nullptr;
                return S_OK;
            }
            return DV_E_FORMATETC;
        }
        HRESULT __stdcall GetDataHere(FORMATETC*, STGMEDIUM*) override { return DATA_E_FORMATETC; }
        HRESULT __stdcall QueryGetData(FORMATETC *pformatetc) override {
            if (!pformatetc) return E_INVALIDARG;
            if ((pformatetc->cfFormat == m_fmtHdrop.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL)) ||
                (pformatetc->cfFormat == m_fmtPrefEffect.cfFormat && (pformatetc->tymed & TYMED_HGLOBAL))) return S_OK;
            return DV_E_FORMATETC;
        }
        HRESULT __stdcall GetCanonicalFormatEtc(FORMATETC*, FORMATETC *pformatetcOut) override { if (pformatetcOut) pformatetcOut->ptd = nullptr; return E_NOTIMPL; }
        HRESULT __stdcall SetData(FORMATETC*, STGMEDIUM*, BOOL) override { return E_NOTIMPL; }
        HRESULT __stdcall EnumFormatEtc(DWORD dwDirection, IEnumFORMATETC **ppenumFormatEtc) override {
            if (!ppenumFormatEtc) return E_POINTER;
            *ppenumFormatEtc = nullptr;
            if (dwDirection != DATADIR_GET) return E_NOTIMPL;
            FORMATETC f[2] = { m_fmtHdrop, m_fmtPrefEffect };
            auto *e = new FormatEtcEnum(f, 2);
            *ppenumFormatEtc = e;
            return S_OK;
        }
        HRESULT __stdcall DAdvise(FORMATETC*, DWORD, IAdviseSink*, DWORD*) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall DUnadvise(DWORD) override { return OLE_E_ADVISENOTSUPPORTED; }
        HRESULT __stdcall EnumDAdvise(IEnumSTATDATA**) override { return OLE_E_ADVISENOTSUPPORTED; }
    };

    bool startAdaptivePathsDrag(const QVector<QString> &framePaths, const QVector<QString> &folderPaths) {
        if (framePaths.isEmpty() && folderPaths.isEmpty()) return false;
        HRESULT hr = OleInitialize(nullptr);
        auto *obj = new AdaptivePathsDataObject(framePaths, folderPaths);
        IDataObject *pDataObject = static_cast<IDataObject*>(obj);
        IDropSource *pDropSource = new SimpleDropSource();
        DWORD effect = DROPEFFECT_COPY;
        HRESULT r = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY, &effect);
        pDropSource->Release();
        pDataObject->Release();
        if (SUCCEEDED(hr)) OleUninitialize();
        return r == DRAGDROP_S_DROP || r == DRAGDROP_S_CANCEL || r == S_OK;
    }


bool startRealPathsDrag(const QVector<QString> &paths) {
    if (paths.isEmpty()) return false;
    HRESULT hr = OleInitialize(nullptr);
    auto *obj = new RealPathsDataObject(paths);
    IDataObject *pDataObject = static_cast<IDataObject*>(obj);
    IDropSource *pDropSource = new SimpleDropSource();
    DWORD effect = DROPEFFECT_COPY;
    HRESULT r = DoDragDrop(pDataObject, pDropSource, DROPEFFECT_COPY, &effect);
    pDropSource->Release();
    pDataObject->Release();
    if (SUCCEEDED(hr)) OleUninitialize();
    return r == DRAGDROP_S_DROP || r == DRAGDROP_S_CANCEL || r == S_OK;
}
}

#else
// Non-Windows stub
namespace VirtualDrag { bool startVirtualDrag(const QVector<VirtualFile>&) { return false; } }
#endif

