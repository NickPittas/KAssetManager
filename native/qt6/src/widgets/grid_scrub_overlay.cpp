#include "widgets/grid_scrub_overlay.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPen>
#include <QPaintEvent>

#include <algorithm>

GridScrubOverlay::GridScrubOverlay(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TransparentForMouseEvents, true);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_TranslucentBackground, true);
    hide();
}

void GridScrubOverlay::setProgress(qreal value)
{
    m_progress = std::clamp(value, 0.0, 1.0);
    if (!m_hasCustomHint) {
        m_statusText = QStringLiteral("%1%").arg(qRound(m_progress * 100.0));
    }
    update();
}

void GridScrubOverlay::setHintText(const QString& text)
{
    m_statusText = text;
    m_hasCustomHint = true;
    update();
}

void GridScrubOverlay::clearHintText()
{
    m_hasCustomHint = false;
    m_statusText = m_defaultHint;
    update();
}

void GridScrubOverlay::setFrame(const QPixmap& pixmap)
{
    m_frame = pixmap;
    update();
}

void GridScrubOverlay::clearFrame()
{
    if (!m_frame.isNull()) {
        m_frame = QPixmap();
    }
    update();
}

void GridScrubOverlay::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const QRectF bounds = rect();
    if (!bounds.isValid()) {
        return;
    }

    painter.setPen(Qt::NoPen);
    painter.setBrush(Qt::NoBrush);
    painter.setClipRect(bounds.adjusted(0, 0, -0.5, -0.5));

    painter.fillRect(bounds, QColor(0, 0, 0, 220));

    if (!m_frame.isNull()) {
        QSize targetSize = bounds.size().toSize();
        if (!targetSize.isEmpty()) {
            QPixmap scaled = m_frame.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const qreal x = bounds.left() + (bounds.width() - scaled.width()) / 2.0;
            const qreal y = bounds.top() + (bounds.height() - scaled.height()) / 2.0;
            painter.drawPixmap(QPointF(x, y), scaled);
        }
    } else {
        painter.setPen(QPen(QColor(80, 80, 80, 160), 1.0));
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
        painter.setPen(Qt::NoPen);
    }

    const qreal hudHeight = 26.0;
    QRectF hudRect = bounds.adjusted(8, bounds.height() - hudHeight - 10, -8, -6);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 170));
    painter.drawRoundedRect(hudRect, 6, 6);

    const qreal barHeight = 4.0;
    QRectF barRect(hudRect.left() + 10, hudRect.bottom() - barHeight - 6, hudRect.width() - 20, barHeight);
    painter.setBrush(QColor(60, 60, 60, 220));
    painter.drawRoundedRect(barRect, 2, 2);

    QRectF fillRect = barRect;
    fillRect.setWidth(barRect.width() * m_progress);
    if (fillRect.width() > 0) {
        painter.setBrush(QColor(88, 166, 255, 230));
        painter.drawRoundedRect(fillRect, 3, 3);
    }

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 8, QFont::DemiBold));
    QRectF textRect(hudRect.left() + 10, hudRect.top() + 6, hudRect.width() - 20, hudRect.height() - barHeight - 14);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_statusText);
}
