#pragma once
#include <QWidget>
#include <QPushButton>
#include <QHBoxLayout>

class StarRatingWidget : public QWidget
{
    Q_OBJECT
public:
    explicit StarRatingWidget(QWidget *parent = nullptr);
    
    void setRating(int rating);
    int rating() const { return m_rating; }
    
    void setReadOnly(bool readOnly);
    bool isReadOnly() const { return m_readOnly; }

signals:
    void ratingChanged(int rating);

private slots:
    void onStarClicked(int star);
    void onStarHovered(int star);
    void onMouseLeft();

protected:
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void updateStars();

    int m_rating = 0;
    int m_hoverRating = 0;
    bool m_readOnly = false;
    QList<QPushButton*> m_starButtons;
};

