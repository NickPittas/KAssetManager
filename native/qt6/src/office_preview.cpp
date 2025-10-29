#include "office_preview.h"

#include <QByteArray>
#include <QXmlStreamReader>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVector>
#include <QMap>
#include <QFileInfo>
#include <QDebug>
#include <QFile>


#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#  include <objbase.h>
#  include <ole2.h>
#  include <combaseapi.h>
#endif


extern "C" {
#include <mz.h>
#include <mz_strm.h>
#include <mz_strm_os.h>
#include <mz_zip.h>
#include <mz_zip_rw.h>
}

static QByteArray readZipEntry(const QString& zipPath, const QString& entryPath)
{
    QByteArray out;
    void* reader = mz_zip_reader_create();
    if (reader == nullptr)
        return out;
    int32_t status = mz_zip_reader_open_file(reader, zipPath.toUtf8().constData());
    if (status != MZ_OK) {
        mz_zip_reader_delete(&reader);
        return out;
    }
    // Use case-insensitive match to be robust against unusual casing in OOXML zips
    status = mz_zip_reader_locate_entry(reader, entryPath.toUtf8().constData(), 1);
    if (status != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return out;
    }
    status = mz_zip_reader_entry_open(reader);
    if (status != MZ_OK) {
        mz_zip_reader_close(reader);
        mz_zip_reader_delete(&reader);
        return out;
    }
    char buf[8192];
    int32_t read = 0;

#ifdef _WIN32
static QByteArray readOleStream(const QString &filePath, const wchar_t *name)
{
    QByteArray result;

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    bool didInit = SUCCEEDED(hr);

    IStorage *storage = nullptr;
    hr = StgOpenStorageEx(reinterpret_cast<LPCWSTR>(filePath.utf16()),
                          STGM_READ | STGM_SHARE_DENY_WRITE,
                          STGFMT_DOCFILE,
                          0, nullptr, nullptr,
                          IID_IStorage,
                          reinterpret_cast<void **>(&storage));
    if (FAILED(hr) || !storage) {
        if (didInit) CoUninitialize();
        return result;
    }

    IStream *stream = nullptr;
    hr = storage->OpenStream(name, nullptr,
                             STGM_READ | STGM_SHARE_EXCLUSIVE, 0, &stream);
    if (SUCCEEDED(hr) && stream) {
        STATSTG st = {};
        if (SUCCEEDED(stream->Stat(&st, STATFLAG_NONAME))) {
            ULONGLONG size = st.cbSize.QuadPart;
            if (size > (ULONGLONG) (64ull * 1024ull * 1024ull)) size = 64ull * 1024ull * 1024ull;
            result.resize((int)size);
            LARGE_INTEGER li; li.QuadPart = 0;
            stream->Seek(li, STREAM_SEEK_SET, nullptr);
            ULONG read = 0;
            stream->Read(result.data(), (ULONG)result.size(), &read);
            result.resize((int)read);
        }
        stream->Release();
    }
    storage->Release();
    if (didInit) CoUninitialize();
    return result;
}
#endif

    while ((read = mz_zip_reader_entry_read(reader, buf, (int32_t)sizeof(buf))) > 0) {
        out.append(buf, read);
        if (out.size() > 10 * 1024 * 1024) { // safety cap 10MB
            break;
        }
    }
    mz_zip_reader_entry_close(reader);
    mz_zip_reader_close(reader);
    mz_zip_reader_delete(&reader);
    return out;
}

static int colIndexFromRef(const QString& cellRef)
{
    // e.g. "C12" -> 2
    int i = 0;
    int col = 0;
    while (i < cellRef.size()) {
        QChar ch = cellRef.at(i);
        if (ch >= 'A' && ch <= 'Z') {
            col = col * 26 + (ch.unicode() - 'A' + 1);
            ++i;
        } else if (ch >= 'a' && ch <= 'z') {
            col = col * 26 + (ch.unicode() - 'a' + 1);
            ++i;
        } else {
            break;
        }
    }
    return col - 1; // zero-based
}

static QString resolveFirstSheetPath(const QString& zipPath)
{
    // Try the common default first
    if (!readZipEntry(zipPath, "xl/worksheets/sheet1.xml").isEmpty())
        return QStringLiteral("xl/worksheets/sheet1.xml");

    // Parse workbook.xml to get first sheet r:id
    const QByteArray wb = readZipEntry(zipPath, "xl/workbook.xml");
    if (wb.isEmpty()) return QString();
    QString rid;
    {
        QXmlStreamReader xml(wb);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("sheet")) {
                rid = xml.attributes().value(QLatin1String("r:id")).toString();
                if (!rid.isEmpty()) break;
            }
        }
    }
    if (rid.isEmpty()) return QString();

    // Map r:id -> Target via workbook.xml.rels
    const QByteArray rels = readZipEntry(zipPath, "xl/_rels/workbook.xml.rels");
    if (rels.isEmpty()) return QString();
    QString target;
    {
        QXmlStreamReader xml(rels);
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("Relationship")) {
                const auto a = xml.attributes();
                if (a.value(QLatin1String("Id")).toString() == rid) {
                    target = a.value(QLatin1String("Target")).toString();
                    break;
                }
            }
        }
    }
    if (target.isEmpty()) return QString();
    if (!target.startsWith("xl/")) target.prepend("xl/");
    return target;
}

QString extractDocxText(const QString& filePath)
{
    const QByteArray xmlData = readZipEntry(filePath, "word/document.xml");
    if (xmlData.isEmpty()) return QString();

    QString out;
    out.reserve(64 * 1024);

    QXmlStreamReader xml(xmlData);
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isStartElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("t")) {
                out += xml.readElementText();
            } else if (name == QLatin1String("br") || name == QLatin1String("cr")) {
                out += QLatin1Char('\n');
            } else if (name == QLatin1String("tab")) {
                out += QLatin1Char('\t');
            }
        } else if (xml.isEndElement()) {
            const auto name = xml.name();
            if (name == QLatin1String("p")) {
                if (!out.endsWith('\n')) out += QLatin1Char('\n');
            }
        }
        if (out.size() > 2 * 1024 * 1024) break; // 2MB cap
    }

    return out;
}


static inline bool isUtf16TextChar(quint16 c)
{
    if (c == 0x0009 || c == 0x000A || c == 0x000D) return true; // tab, LF, CR
    if (c >= 0x0020 && c < 0xD800) return true;                 // BMP before surrogates
    if (c >= 0xE000 && c <= 0xFFFD) return true;                // BMP after surrogates (skip non-chars)
    return false;
}

static inline bool isCjkChar(uint u)
{
    if ((u >= 0x4E00 && u <= 0x9FFF) || // CJK Unified Ideographs
        (u >= 0x3400 && u <= 0x4DBF) || // CJK Ext A
        (u >= 0x3040 && u <= 0x30FF) || // Hiragana/Katakana
        (u >= 0x31A0 && u <= 0x31FF) || // Bopomofo/Katakana Phonetic Ext
        (u >= 0xAC00 && u <= 0xD7AF) || // Hangul Syllables
        (u >= 0xF900 && u <= 0xFAFF) || // CJK Compatibility Ideographs
        (u >= 0x2E80 && u <= 0x2EFF) || // CJK Radicals
        (u >= 0x3000 && u <= 0x303F))   // CJK punctuation
        return true;
    return false;
}

static inline bool isWesternishChar(uint u)
{
    if (u == 0x0009 || u == 0x000A || u == 0x000D) return true; // whitespace
    if (u >= 0x0020 && u <= 0x007E) return true;                 // ASCII
    if (u >= 0x00A0 && u <= 0x00FF) return true;                 // Latin-1 supplement
    if (u >= 0x0100 && u <= 0x024F) return true;                 // Latin Extended A/B
    if ((u >= 0x2010 && u <= 0x2015) ||                          // dashes
        (u >= 0x2018 && u <= 0x201F) ||                          // curly quotes
        u == 0x2026)                                             // ellipsis
        return true;
    return false;
}

static bool looksWestern(const QString &s, double minRatio)
{
    int western = 0, cjk = 0, total = 0;
    for (QChar ch : s) {
        const uint u = ch.unicode();
        if (u == 0x0009 || u == 0x000A || u == 0x000D || u >= 0x0020) ++total;
        if (isWesternishChar(u)) ++western;
        if (isCjkChar(u)) ++cjk;
    }
    if (total == 0) return false;
    // Reject if overwhelmingly CJK-like
    if (cjk > western * 2) return false;
    const double ratio = (double)western / (double)total;
    return ratio >= minRatio;
}


QString extractDocBinaryText(const QString& filePath, int maxChars)
{
    QByteArray data;
#ifdef _WIN32
    data = readOleStream(filePath, L"WordDocument");
#endif
    if (data.isEmpty()) {
        QFile f(filePath);
        if (!f.open(QIODevice::ReadOnly)) return QString();
        const qint64 kMaxRead = 32ll * 1024 * 1024; // read up to 32 MB
        data = f.read(kMaxRead);
        f.close();
        if (data.isEmpty()) return QString();
    }

    QString out;
    out.reserve(64 * 1024);

    bool useFilter = false; // do not filter when reading WordDocument stream
    auto appendSeg = [&](QString seg) {
        seg.replace("\r\n", "\n");
        seg.replace('\r', '\n');
        while (seg.contains("\n\n\n")) seg.replace("\n\n\n", "\n\n");
        seg = seg.trimmed();
        if (seg.size() < 20) return; // ignore very short/noisy chunks
        if (useFilter && !looksWestern(seg, 0.55)) return; // optional filter for full-file fallback
        if (!out.isEmpty() && !out.endsWith('\n')) out += '\n';
        out += seg;
        out += '\n';
    };

    // First, try to extract UTF-16LE runs (common for .doc text storage)
    for (int offset = 0; offset < 2 && out.size() < maxChars; ++offset) {
        const int n = data.size() - offset;
        int i = 0;
        while (i + 1 < n && out.size() < maxChars) {
            // Advance to start of a likely text char
            while (i + 1 < n) {
                quint16 c = (quint16)(uchar)data[offset + i] | ((quint16)(uchar)data[offset + i + 1] << 8);
                if (isUtf16TextChar(c)) break;
                i += 2;
            }
            if (i + 1 >= n) break;

            QVector<quint16> buf;
            int bad = 0;
            while (i + 1 < n) {
                quint16 c = (quint16)(uchar)data[offset + i] | ((quint16)(uchar)data[offset + i + 1] << 8);
                if (isUtf16TextChar(c)) {
                    buf.push_back(c);
                    bad = 0;
                } else {
                    // allow brief noise within a run; insert space once
                    if (!buf.isEmpty()) buf.push_back((quint16)' ');
                    if (++bad >= 2) { i += 2; break; }
                }
                i += 2;
                if (buf.size() >= 8192) break; // cap individual run
            }
            if (buf.size() >= 16) {
                QString seg = QString::fromUtf16(reinterpret_cast<const char16_t*>(buf.constData()), buf.size());
                appendSeg(seg);
            }
        }
    }

    // If nothing substantial extracted, scan for long ASCII/ANSI runs
    if (out.size() < 256) {
        const int n = data.size();
        int i = 0;
        while (i < n && out.size() < maxChars) {
            // advance to likely text byte
            while (i < n) {
                uchar b = (uchar)data[i];
                if (b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E)) break;
                ++i;
            }
            if (i >= n) break;
            const int start = i;
            int count = 0;
            while (i < n) {
                uchar b = (uchar)data[i];
                if (b == 0x09 || b == 0x0A || b == 0x0D || (b >= 0x20 && b <= 0x7E)) {
                    ++count; ++i;
                    if (count >= 16384) break;
                } else {
                    break;
                }
            }
            if (count >= 32) {
                QString seg = QString::fromLatin1(data.constData() + start, count);
                appendSeg(seg);
            }
        }
    }

    if (out.size() > maxChars) out.truncate(maxChars);
    return out.trimmed();
}


bool loadXlsxSheet(const QString& filePath, QStandardItemModel* model, int maxRows)
{
    if (!model) return false;

    // Shared strings
    QVector<QString> sst;
    {
        const QByteArray sstXml = readZipEntry(filePath, "xl/sharedStrings.xml");
        if (!sstXml.isEmpty()) {
            QXmlStreamReader xml(sstXml);
            while (!xml.atEnd()) {
                xml.readNext();
                if (xml.isStartElement() && xml.name() == QLatin1String("si")) {
                    QString s;
                    while (!(xml.isEndElement() && xml.name() == QLatin1String("si")) && !xml.atEnd()) {
                        xml.readNext();
                        if (xml.isStartElement() && (xml.name() == QLatin1String("t"))) {
                            s += xml.readElementText();
                        }
                    }
                    sst.push_back(s);
                }
            }
        }
    }

    // Resolve first sheet path
    const QString sheetPath = resolveFirstSheetPath(filePath);
    if (sheetPath.isEmpty()) return false;
    const QByteArray sheetXml = readZipEntry(filePath, sheetPath);
    if (sheetXml.isEmpty()) return false;

    model->clear();

    QXmlStreamReader xml(sheetXml);
    int currentRow = -1;
    int producedRows = 0;
    int maxCol = 0;

    while (!xml.atEnd() && producedRows < maxRows) {
        xml.readNext();
        if (xml.isStartElement()) {
            if (xml.name() == QLatin1String("row")) {
                bool ok = false; int rAttr = xml.attributes().value(QLatin1String("r")).toInt(&ok);
                currentRow = ok ? (rAttr - 1) : (currentRow + 1);
            } else if (xml.name() == QLatin1String("c")) {
                const auto attrs = xml.attributes();
                const QString r = attrs.value(QLatin1String("r")).toString();
                const QString t = attrs.value(QLatin1String("t")).toString();
                const int col = colIndexFromRef(r);
                if (col > maxCol) maxCol = col;

                // Read until end of cell to find <v> or inline <is>
                QString cellText;
                while (!(xml.isEndElement() && xml.name() == QLatin1String("c")) && !xml.atEnd()) {
                    xml.readNext();
                    if (xml.isStartElement() && xml.name() == QLatin1String("v")) {
                        const QString v = xml.readElementText();
                        if (t == QLatin1String("s")) {
                            bool ok = false; int idx = v.toInt(&ok);
                            cellText = (ok && idx >= 0 && idx < sst.size()) ? sst.at(idx) : v;
                        } else {
                            cellText = v;
                        }
                    } else if (xml.isStartElement() && xml.name() == QLatin1String("is")) {
                        // Inline string
                        while (!(xml.isEndElement() && xml.name() == QLatin1String("is")) && !xml.atEnd()) {
                            xml.readNext();
                            if (xml.isStartElement() && xml.name() == QLatin1String("t"))
                                cellText += xml.readElementText();
                        }
                    }
                }
                if (currentRow >= 0) {
                    if (model->columnCount() <= col) model->setColumnCount(col + 1);
                    if (model->rowCount() <= currentRow) model->setRowCount(currentRow + 1);
                    model->setItem(currentRow, col, new QStandardItem(cellText));
                }
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("row")) {
            ++producedRows;
        }
    }

    // Trim to max rows if necessary
    if (model->rowCount() > maxRows) model->removeRows(maxRows, model->rowCount() - maxRows);

    return model->rowCount() > 0 && model->columnCount() > 0;
}

