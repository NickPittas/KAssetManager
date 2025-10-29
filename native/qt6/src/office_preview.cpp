#include "office_preview.h"

#include <QByteArray>
#include <QXmlStreamReader>
#include <QStandardItemModel>
#include <QStandardItem>
#include <QVector>
#include <QMap>
#include <QFileInfo>
#include <QDebug>

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

