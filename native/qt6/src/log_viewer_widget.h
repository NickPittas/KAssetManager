#ifndef LOG_VIEWER_WIDGET_H
#define LOG_VIEWER_WIDGET_H

#include <QWidget>
#include <QTextEdit>
#include <QComboBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

class LogViewerWidget : public QWidget {
    Q_OBJECT

public:
    explicit LogViewerWidget(QWidget* parent = nullptr);

    enum LogLevel {
        All = 0,
        Debug = 1,
        Info = 2,
        Warning = 3,
        Error = 4,
        Critical = 5
    };

private slots:
    void onLogAdded(const QString& message);
    void onFilterChanged(int index);
    void onClearLogs();

private:
    void addLogToView(const QString& message);
    bool shouldShowLog(const QString& message) const;
    QString colorizeLog(const QString& message) const;

    QTextEdit* m_logTextEdit;
    QComboBox* m_filterCombo;
    QPushButton* m_clearButton;
    LogLevel m_currentFilter;
};

#endif // LOG_VIEWER_WIDGET_H

