#include "star_rating_widget.h"
#include <QHBoxLayout>
#include <QPushButton>
#include <QEvent>

StarRatingWidget::StarRatingWidget(QWidget *parent)
    : QWidget(parent)
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(2);
    
    // Create 5 star buttons
    for (int i = 0; i < 5; i++) {
        QPushButton *btn = new QPushButton(this);
        btn->setFixedSize(24, 24);
        btn->setFlat(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(
            "QPushButton { "
            "  background: transparent; "
            "  border: none; "
            "  font-size: 18px; "
            "  color: #FFD700; "
            "} "
            "QPushButton:hover { "
            "  background: rgba(255, 255, 255, 0.1); "
            "  border-radius: 3px; "
            "}"
        );
        
        // Connect click
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            onStarClicked(i + 1);
        });
        
        // Install event filter for hover
        btn->installEventFilter(this);
        btn->setProperty("starIndex", i + 1);
        
        m_starButtons.append(btn);
        layout->addWidget(btn);
    }
    
    // Add clear button
    QPushButton *clearBtn = new QPushButton("✕", this);
    clearBtn->setFixedSize(24, 24);
    clearBtn->setFlat(true);
    clearBtn->setCursor(Qt::PointingHandCursor);
    clearBtn->setToolTip("Clear rating");
    clearBtn->setStyleSheet(
        "QPushButton { "
        "  background: transparent; "
        "  border: none; "
        "  font-size: 14px; "
        "  color: #999; "
        "} "
        "QPushButton:hover { "
        "  background: rgba(255, 255, 255, 0.1); "
        "  border-radius: 3px; "
        "  color: #fff; "
        "}"
    );
    connect(clearBtn, &QPushButton::clicked, this, [this]() {
        onStarClicked(0);
    });
    layout->addWidget(clearBtn);
    
    layout->addStretch();
    
    updateStars();
}

void StarRatingWidget::setRating(int rating)
{
    if (rating < 0) rating = 0;
    if (rating > 5) rating = 5;
    
    if (m_rating != rating) {
        m_rating = rating;
        updateStars();
    }
}

void StarRatingWidget::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    for (QPushButton *btn : m_starButtons) {
        btn->setEnabled(!readOnly);
        btn->setCursor(readOnly ? Qt::ArrowCursor : Qt::PointingHandCursor);
    }
}

void StarRatingWidget::onStarClicked(int star)
{
    if (m_readOnly) return;
    
    // If clicking the same rating, clear it
    if (star == m_rating) {
        star = 0;
    }
    
    setRating(star);
    emit ratingChanged(star);
}

void StarRatingWidget::onStarHovered(int star)
{
    if (m_readOnly) return;
    m_hoverRating = star;
    updateStars();
}

void StarRatingWidget::onMouseLeft()
{
    if (m_readOnly) return;
    m_hoverRating = 0;
    updateStars();
}

void StarRatingWidget::updateStars()
{
    int displayRating = m_hoverRating > 0 ? m_hoverRating : m_rating;
    
    for (int i = 0; i < m_starButtons.size(); i++) {
        QString star = (i < displayRating) ? "★" : "☆";
        m_starButtons[i]->setText(star);
    }
}

bool StarRatingWidget::eventFilter(QObject *obj, QEvent *event)
{
    if (m_readOnly) return QWidget::eventFilter(obj, event);
    
    QPushButton *btn = qobject_cast<QPushButton*>(obj);
    if (btn && m_starButtons.contains(btn)) {
        if (event->type() == QEvent::Enter) {
            int starIndex = btn->property("starIndex").toInt();
            onStarHovered(starIndex);
            return false;
        } else if (event->type() == QEvent::Leave) {
            onMouseLeft();
            return false;
        }
    }
    
    return QWidget::eventFilter(obj, event);
}

