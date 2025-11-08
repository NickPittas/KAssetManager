#pragma once

#include <QPixmap>
#include <QString>
#include <QWidget>

class GridScrubOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit GridScrubOverlay(QWidget* parent = nullptr);

    void setProgress(qreal value);
    void setHintText(const QString& text);
    void clearHintText();
    void setFrame(const QPixmap& pixmap);
    void clearFrame();

protected:
    void paintEvent(QPaintEvent*) override;

private:
    qreal m_progress = 0.0;
    QString m_statusText = QStringLiteral("Ctrl + Move/Wheel to scrub");
    const QString m_defaultHint = QStringLiteral("Ctrl + Move/Wheel to scrub");
    bool m_hasCustomHint = false;
    QPixmap m_frame;
};
