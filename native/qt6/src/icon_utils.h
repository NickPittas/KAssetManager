#ifndef ICON_UTILS_H
#define ICON_UTILS_H

#include <QIcon>
#include <QColor>
#include <QFileIconProvider>
#include <functional>
#include <QPainter>

// Icon generation helpers
QIcon mkIcon(const std::function<void(QPainter&, const QRectF&)>& draw, const QColor& color = QColor(235,235,235));
QIcon loadPngIcon(const QString& filename, const QColor& targetColor = QColor(255, 255, 255));

// General icons
QIcon icoFolderNew(const QColor& color = QColor(255,255,255));
QIcon icoCopy(const QColor& color = QColor(255,255,255));
QIcon icoCut(const QColor& color = QColor(255,255,255));
QIcon icoPaste(const QColor& color = QColor(255,255,255));
QIcon icoDelete(const QColor& color = QColor(255,255,255));
QIcon icoRename(const QColor& color = QColor(255,255,255));
QIcon icoAdd(const QColor& color = QColor(255,255,255));
QIcon icoGrid(const QColor& color = QColor(255,255,255));
QIcon icoList(const QColor& color = QColor(255,255,255));
QIcon icoGroup(const QColor& color = QColor(255,255,255));
QIcon icoEye(const QColor& color = QColor(235,235,235));
QIcon icoBack(const QColor& color = QColor(255,255,255));
QIcon icoUp(const QColor& color = QColor(255,255,255));
QIcon icoRefresh(const QColor& color = QColor(255,255,255));
QIcon icoHide(const QColor& color = QColor(255,255,255));
QIcon icoSearch(const QColor& color = QColor(255,255,255));
QIcon icoDualPane(const QColor& color = QColor(255,255,255));

// Media icons
QIcon icoMediaPlay(const QColor& color = QColor(255,255,255));
QIcon icoMediaPause(const QColor& color = QColor(255,255,255));
QIcon icoMediaStop(const QColor& color = QColor(255,255,255));
QIcon icoMediaNextFrame(const QColor& color = QColor(255,255,255));
QIcon icoMediaPrevFrame(const QColor& color = QColor(255,255,255));
QIcon icoMediaAudio(const QColor& color = QColor(255,255,255));
QIcon icoMediaNoAudio(const QColor& color = QColor(255,255,255));
QIcon icoMediaMute(const QColor& color = QColor(255,255,255));

// File type icons
QIcon icoFilePdf(const QColor& color = QColor(235,235,235));
QIcon icoFileCsv(const QColor& color = QColor(235,235,235));
QIcon icoFileDoc(const QColor& color = QColor(235,235,235));
QIcon icoFileXls(const QColor& color = QColor(235,235,235));
QIcon icoFileTxt(const QColor& color = QColor(235,235,235));
QIcon icoFileAi(const QColor& color = QColor(235,235,235));
QIcon icoFileGeneric(const QColor& color = QColor(235,235,235));

QIcon getFileTypeIcon(const QString &ext, const QColor& color = QColor(235,235,235));

// Icon providers
class FmTreeIconProvider : public QFileIconProvider {
public:
    FmTreeIconProvider() : QFileIconProvider() {}
    QIcon icon(IconType type) const override;
    QIcon icon(const QFileInfo &info) const override;
};

class FmIconProvider : public QFileIconProvider {
public:
    FmIconProvider() : QFileIconProvider() {}
    QIcon icon(const QFileInfo &info) const override;
};

#endif // ICON_UTILS_H
