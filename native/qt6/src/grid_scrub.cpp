#include "grid_scrub.h"
#include "live_preview_manager.h"
#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QApplication>
#include <QScrollBar>
#include <QCursor>
#include <QFileInfo>
#include <QRegularExpression>
#include <algorithm>
#include <limits>

namespace {
    constexpr qreal kScrubDefaultPosition = 0.0;
}

// GridScrubOverlay implementation

GridScrubOverlay::GridScrubOverlay(QWidget *parent)
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

void GridScrubOverlay::setHintText(const QString &text)
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

void GridScrubOverlay::setFrame(const QPixmap &pixmap)
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

void GridScrubOverlay::paintEvent(QPaintEvent *)
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

            // Draw vertical scrubbing line on top of the frame
            const qreal lineX = x + scaled.width() * m_progress;
            const qreal lineTop = y;
            const qreal lineBottom = y + scaled.height();

            // Draw the main line (bright blue)
            painter.setPen(QPen(QColor(88, 166, 255, 255), 2.0));
            painter.drawLine(QPointF(lineX, lineTop), QPointF(lineX, lineBottom));

            // Draw a subtle shadow line for better visibility
            painter.setPen(QPen(QColor(0, 0, 0, 120), 1.0));
            painter.drawLine(QPointF(lineX + 1, lineTop), QPointF(lineX + 1, lineBottom));
        }
    } else {
        painter.setPen(QPen(QColor(80, 80, 80, 160), 1.0));
        painter.drawRoundedRect(bounds.adjusted(1, 1, -1, -1), 6, 6);
        painter.setPen(Qt::NoPen);
    }

    // Text and timeline display removed - keeping only the vertical line
}

// GridScrubController implementation

GridScrubController::GridScrubController(QAbstractItemView *view,
                                         std::function<QString(const QModelIndex&)> resolver,
                                         QObject *parent)
    : QObject(parent),
      m_view(view),
      m_pathResolver(std::move(resolver)),
      m_overlay(new GridScrubOverlay(view->viewport())),
      m_position(kScrubDefaultPosition),
      m_lastMouseX(std::numeric_limits<qreal>::quiet_NaN()),
      m_sequenceGroupingEnabled(true)
{
    if (!m_view) return;
    m_view->setMouseTracking(true);
    if (m_view->viewport()) {
        m_view->viewport()->setMouseTracking(true);
        m_view->viewport()->installEventFilter(this);
    }
    m_view->installEventFilter(this);
    if (m_view->verticalScrollBar()) {
        connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](){ updateOverlayGeometry(); });
    }
    if (m_view->horizontalScrollBar()) {
        connect(m_view->horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](){ updateOverlayGeometry(); });
    }
    if (m_view->model()) {
        connect(m_view->model(), &QAbstractItemModel::modelReset, this, [this]() {
            m_positions.clear();
            hideOverlay();
        });
    }

    LivePreviewManager &previewMgr = LivePreviewManager::instance();
    connect(&previewMgr, &LivePreviewManager::frameReady, this,
            [this](const QString &path, qreal position, QSize, const QPixmap &pixmap) {
        if (path != m_currentPath || !m_overlay) {
            return;
        }
        m_loadingFrame = false;
        m_position = position;
        m_positions[m_currentPath] = position;
        m_overlay->setProgress(m_position);
        m_overlay->setFrame(pixmap);
        if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier) || !qFuzzyIsNull(m_position)) {
            m_overlay->setHintText(QStringLiteral("%1%").arg(qRound(m_position * 100.0)));
        } else {
            m_overlay->clearHintText();
        }
    });
    connect(&previewMgr, &LivePreviewManager::frameFailed, this,
            [this](const QString &path, const QString &error) {
        if (path != m_currentPath || !m_overlay) {
            return;
        }
        m_loadingFrame = false;
        m_overlay->clearFrame();
        m_overlay->setHintText(error);
    });
}

GridScrubController::~GridScrubController()
{
    endScrub();
}

void GridScrubController::setSequenceGroupingEnabled(bool enabled)
{
    m_sequenceGroupingEnabled = enabled;
    if (!m_sequenceGroupingEnabled) {
        endScrub();
        hideOverlay();
        resetCtrlTracking();
    }
}

bool GridScrubController::isSequenceGroupingEnabled() const 
{ 
    return m_sequenceGroupingEnabled; 
}

bool GridScrubController::canScrubFile(const QString& filePath) const
{
    if (filePath.isEmpty()) return false;

    QFileInfo info(filePath);
    QString suffix = info.suffix().toLower();

    // Videos can always be scrubbed
    if (suffix == "mp4" || suffix == "mov" || suffix == "avi" ||
        suffix == "mkv" || suffix == "webm" || suffix == "m4v" || suffix == "mxf") {
        return true;
    }

    // Image sequences can only be scrubbed when grouping is enabled
    if (m_sequenceGroupingEnabled) {
        // Check if it's an image with sequence-like name
        static QRegularExpression seqPattern(R"(.*(?:\d{2,}|%0\d+d|\#\#\#).*)");
        return seqPattern.match(info.fileName()).hasMatch();
    }

    return false;
}

bool GridScrubController::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view) return QObject::eventFilter(watched, event);

    if (watched == m_view->viewport()) {
        switch (event->type()) {
        case QEvent::MouseMove:
        {
            auto moveEvent = static_cast<QMouseEvent*>(event);
            const QPoint pos = moveEvent->position().toPoint();
            handleHoverMove(pos);
            if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
                if (m_currentIndex.isValid() && canScrubFile(m_currentPath)) {
                    handleCtrlScrub(pos);
                    showOverlay();
                    event->accept();
                    return true;
                }
            }
            endScrub();
            resetCtrlTracking();
            break;
        }
        case QEvent::Leave:
            if (m_scrubActive && m_currentIndex.isValid()) {
                updateOverlayGeometry();
            } else {
                hideOverlay();
                m_currentPath.clear();
                m_currentIndex = QModelIndex();
            }
            break;
        case QEvent::Wheel:
        {
            auto wheel = static_cast<QWheelEvent*>(event);
            if (!wheel->modifiers().testFlag(Qt::ControlModifier)) {
                endScrub();
                hideOverlay();
                resetCtrlTracking();
                break;
            }
            QModelIndex idx = m_view->indexAt(wheel->position().toPoint());
            if (!idx.isValid()) {
                wheel->accept();
                return true;
            }
            setCurrentIndex(idx);
            if (!canScrubFile(m_currentPath)) {
                endScrub();
                hideOverlay();
                resetCtrlTracking();
                break;
            }
            beginScrub();
            int delta = wheel->angleDelta().x();
            if (delta == 0) {
                delta = wheel->angleDelta().y();
            }
            if (delta != 0 && !m_currentPath.isEmpty()) {
                const qreal step = static_cast<qreal>(delta) / 3600.0;
                setPosition(std::clamp(m_position + step, 0.0, 1.0));
                requestPreview();
            }
            showOverlay();
            resetCtrlTracking();
            wheel->accept();
            return true;
        }
        case QEvent::Resize:
            updateOverlayGeometry();
            break;
        default:
            break;
        }
    } else if (watched == m_view) {
        if (event->type() == QEvent::KeyRelease) {
            auto keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent->key() == Qt::Key_Control) {
                if (qFuzzyIsNull(m_position)) {
                    hideOverlay();
                }
                endScrub();
                resetCtrlTracking();
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

void GridScrubController::handleHoverMove(const QPoint &pos)
{
    if (m_scrubActive && m_currentIndex.isValid()) {
        handleCtrlScrub(pos);
        updateOverlayGeometry();
        return;
    }
    QModelIndex idx = m_view->indexAt(pos);
    if (!idx.isValid()) {
        if (!m_scrubActive) {
            hideOverlay();
            m_currentIndex = QModelIndex();
            m_currentPath.clear();
        }
        return;
    }
    if (idx == m_currentIndex) {
        return;
    }
    setCurrentIndex(idx);
}

void GridScrubController::setCurrentIndex(const QModelIndex &idx)
{
    if (!idx.isValid()) {
        m_currentIndex = QModelIndex();
        m_currentPath.clear();
        hideOverlay();
        resetCtrlTracking();
        return;
    }
    QString resolved = m_pathResolver ? m_pathResolver(idx) : QString();
    if (resolved.isEmpty()) {
        m_currentIndex = QModelIndex();
        m_currentPath.clear();
        hideOverlay();
        resetCtrlTracking();
        return;
    }

    QFileInfo resolvedInfo(resolved);
    if (!resolvedInfo.exists() || !resolvedInfo.isFile()) {
        m_currentIndex = QModelIndex();
        m_currentPath.clear();
        hideOverlay();
        resetCtrlTracking();
        return;
    }

    m_currentIndex = idx;
    m_currentPath = resolved;
    m_position = m_positions.value(m_currentPath, kScrubDefaultPosition);
    m_loadingFrame = false;
    endScrub();
    if (m_overlay) {
        m_overlay->setProgress(m_position);
        m_overlay->clearHintText();
        m_overlay->clearFrame();
    }
    if (QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        m_lastMouseX = m_view->viewport()->mapFromGlobal(QCursor::pos()).x();
        showOverlay();
        requestPreview();
    } else {
        resetCtrlTracking();
    }
}

void GridScrubController::setPosition(qreal value)
{
    m_position = std::clamp(value, 0.0, 1.0);
    if (!m_currentPath.isEmpty()) {
        m_positions[m_currentPath] = m_position;
    }
    m_overlay->setProgress(m_position);
}

void GridScrubController::requestPreview()
{
    QFileInfo info(m_currentPath);
    if (!info.exists() || !info.isFile()) {
        return;
    }
    QSize targetSize = currentTargetSize();
    if (m_overlay) {
        m_overlay->setProgress(m_position);
        m_overlay->setHintText(QStringLiteral("Decoding..."));
    }
    beginScrub();
    m_loadingFrame = true;
    LivePreviewManager::instance().requestFrame(m_currentPath, targetSize, m_position);
}

void GridScrubController::showOverlay()
{
    if (!m_overlay || !m_currentIndex.isValid()) return;
    if (m_loadingFrame) {
        m_overlay->setHintText(QStringLiteral("Decoding..."));
    } else if (qFuzzyCompare(m_position, kScrubDefaultPosition) && !QApplication::keyboardModifiers().testFlag(Qt::ControlModifier)) {
        m_overlay->clearHintText();
    } else {
        m_overlay->setHintText(QStringLiteral("%1%").arg(qRound(m_position * 100.0)));
    }
    updateOverlayGeometry();
    m_overlay->setProgress(m_position);
    m_overlay->show();
    m_overlay->raise();
}

void GridScrubController::hideOverlay()
{
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->clearHintText();
        m_overlay->clearFrame();
    }
    m_loadingFrame = false;
    endScrub();
    resetCtrlTracking();
}

void GridScrubController::updateOverlayGeometry()
{
    if (!m_overlay || !m_currentIndex.isValid()) return;
    QRect thumbRect = currentThumbRect();
    if (!thumbRect.isValid()) {
        hideOverlay();
        return;
    }
    m_overlay->setGeometry(thumbRect.adjusted(1, 1, -1, -1));
}

bool GridScrubController::handleCtrlScrub(const QPoint &pos)
{
    if (m_warpingCursor) {
        m_warpingCursor = false;
    }
    if (m_currentPath.isEmpty() || !m_currentIndex.isValid()) {
        return false;
    }
    QRect thumbRect = currentThumbRect();
    if (!thumbRect.isValid() || thumbRect.width() <= 0) {
        return false;
    }
    const int clampedX = std::clamp(pos.x(), thumbRect.left(), thumbRect.right());
    const int clampedY = std::clamp(pos.y(), thumbRect.top(), thumbRect.bottom());
    if (m_view && m_view->viewport() && (clampedX != pos.x() || clampedY != pos.y())) {
        m_warpingCursor = true;
        const QPoint clampedPoint(clampedX, clampedY);
        QCursor::setPos(m_view->viewport()->mapToGlobal(clampedPoint));
    }
    beginScrub();
    const qreal fraction = thumbRect.width() > 0
        ? static_cast<qreal>(clampedX - thumbRect.left()) / static_cast<qreal>(thumbRect.width())
        : 0.0;
    m_lastMouseX = clampedX;
    setPosition(fraction);
    requestPreview();
    return true;
}

QSize GridScrubController::currentTargetSize() const
{
    QSize targetSize = m_view ? m_view->iconSize() : QSize();
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        targetSize = QSize(180, 180);
    }
    return targetSize;
}

QRect GridScrubController::thumbRectFor(const QRect &itemRect) const
{
    if (!itemRect.isValid()) {
        return QRect();
    }
    const int margin = 6;
    QSize icon = currentTargetSize();
    int side = std::max(0, std::min(icon.width(), icon.height()));
    side = std::min(side, itemRect.width() - margin * 2);
    side = std::min(side, itemRect.height() - margin * 2);
    if (side <= 0) {
        return QRect();
    }
    int x = itemRect.x() + (itemRect.width() - side) / 2;
    int y = itemRect.y() + margin;
    if (y + side > itemRect.bottom() - margin) {
        y = itemRect.bottom() - margin - side;
    }
    return QRect(x, y, side, side);
}

QRect GridScrubController::currentThumbRect() const
{
    if (!m_currentIndex.isValid() || !m_view) {
        return QRect();
    }
    QRect itemRect = m_view->visualRect(m_currentIndex);
    return thumbRectFor(itemRect);
}

void GridScrubController::resetCtrlTracking()
{
    m_lastMouseX = std::numeric_limits<qreal>::quiet_NaN();
}

void GridScrubController::beginScrub()
{
    if (m_scrubActive) {
        return;
    }
    m_scrubActive = true;
    if (m_view && m_view->viewport() && !m_mouseGrabbed) {
        m_view->viewport()->grabMouse();
        m_mouseGrabbed = true;
    }
}

void GridScrubController::endScrub()
{
    if (!m_scrubActive) {
        return;
    }
    m_scrubActive = false;
    if (m_view && m_view->viewport() && m_mouseGrabbed) {
        m_view->viewport()->releaseMouse();
        m_mouseGrabbed = false;
    }
}
