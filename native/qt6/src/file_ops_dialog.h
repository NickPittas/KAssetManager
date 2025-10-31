#pragma once
#include <QDialog>
#include <QListWidget>
#include <QProgressBar>
#include <QPushButton>
#include <QLabel>
#include <QTimer>

#include "file_ops.h"

class FileOpsProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit FileOpsProgressDialog(QWidget* parent=nullptr);

private slots:
    void refreshList();
    void onProgress(int current, int total, const QString& currentFile);
    void onCurrentChanged(const FileOpsQueue::Item& item);
    void onItemFinished(int id, bool success, const QString& error);

private:
    QListWidget* list;
    QProgressBar* bar;
    QLabel* label;
    QPushButton* cancelCurrentBtn;
    QPushButton* cancelAllBtn;
    QPushButton* closeBtn;
    QTimer refreshTimer;
    bool m_cancelling = false;
};


