#include "file_ops_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStyle>
#include <QCoreApplication>
#include <QEventLoop>
#include <QMessageBox>

#include <algorithm>

FileOpsProgressDialog::FileOpsProgressDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("File Operations");
    setWindowFlag(Qt::WindowStaysOnTopHint);
    setWindowModality(Qt::NonModal);
    resize(520, 320);

    QVBoxLayout* v = new QVBoxLayout(this);
    list = new QListWidget(this);
    list->setSelectionMode(QAbstractItemView::NoSelection);
    v->addWidget(list);

    label = new QLabel("Idle", this);
    bar = new QProgressBar(this);
    bar->setRange(0, 1000);
    bar->setValue(0);

    v->addWidget(label);
    v->addWidget(bar);

    QHBoxLayout* h = new QHBoxLayout();
    cancelCurrentBtn = new QPushButton(style()->standardIcon(QStyle::SP_BrowserStop), "Cancel Current", this);
    cancelAllBtn = new QPushButton("Cancel All", this);
    closeBtn = new QPushButton("Close", this);
    h->addWidget(cancelCurrentBtn);
    h->addWidget(cancelAllBtn);
    h->addStretch();
    h->addWidget(closeBtn);
    v->addLayout(h);

    // Debounce refreshes to keep UI responsive during high-frequency updates
    refreshTimer.setSingleShot(true);
    refreshTimer.setInterval(60);
    connect(&refreshTimer, &QTimer::timeout, this, &FileOpsProgressDialog::refreshList);

    auto &q = FileOpsQueue::instance();
    connect(&q, &FileOpsQueue::queueChanged, this, [this]{ if (!refreshTimer.isActive()) refreshTimer.start(); });
    connect(&q, &FileOpsQueue::progressChanged, this, &FileOpsProgressDialog::onProgress);
    connect(&q, &FileOpsQueue::currentItemChanged, this, &FileOpsProgressDialog::onCurrentChanged);
    connect(&q, &FileOpsQueue::itemFinished, this, &FileOpsProgressDialog::onItemFinished);

    connect(cancelCurrentBtn, &QPushButton::clicked, this, [this]{
        m_cancelling = true;
        FileOpsQueue::instance().cancelCurrent();
        label->setText(tr("Cancelling current operation..."));
    });
    connect(cancelAllBtn, &QPushButton::clicked, this, [this]{
        m_cancelling = true;
        FileOpsQueue::instance().cancelAll();
        label->setText(tr("Cancelling all operations..."));
    });

    connect(closeBtn, &QPushButton::clicked, this, &QDialog::close);

    refreshList();
}

void FileOpsProgressDialog::refreshList()
{
    auto items = FileOpsQueue::instance().items();
    list->clear();
    bool anyActive = false;
    for (const auto &it : items) {
        if (it.status == "Queued" || it.status == "In Progress" || it.status == "Cancelling") {
            anyActive = true;
            QString text = QString("#%1  %2  (%3/%4)  %5")

                               .arg(it.id)
                               .arg(it.type == FileOpsQueue::Type::Copy ? "Copy" : it.type == FileOpsQueue::Type::Move ? "Move" : "Delete")
                               .arg(it.completedFiles)
                               .arg(it.totalFiles)
                               .arg(it.status);
            QListWidgetItem* li = new QListWidgetItem(text, list);
            if (!it.error.isEmpty()) li->setToolTip(it.error);
        }
    }
    if (!anyActive) {
        // Auto-close when nothing is active
        m_cancelling = false;
        close();
    }
}


void FileOpsProgressDialog::onProgress(int current, int total, const QString& currentFile)
{
    if (m_cancelling) {
        bar->setRange(0, 0);
        bar->setValue(0);
        label->setText(tr("Cancelling current operation..."));
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        return;
    }

    if (total <= 0) { bar->setRange(0,0); return; }
    bar->setRange(0, 1000);
    int v = int(double(current) / double(total) * 1000.0);
    bar->setValue(std::clamp(v, 0, 1000));
    if (!currentFile.isEmpty()) label->setText(QString("Processing: %1").arg(currentFile));

    // Keep UI responsive even during very large copies
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
}


void FileOpsProgressDialog::onCurrentChanged(const FileOpsQueue::Item& item)
{
    if (item.status == "Cancelling") {
        m_cancelling = true;
        label->setText(tr("Cancelling current operation..."));
        bar->setRange(0, 0);
        bar->setValue(0);
        return;
    }

    m_cancelling = false;
    label->setText(QString("%1: %2 item(s) -> %3").arg(
        item.type == FileOpsQueue::Type::Copy ? "Copy" : item.type == FileOpsQueue::Type::Move ? "Move" : "Delete",
        QString::number(item.totalFiles),
        item.destination));
    // Indeterminate until we get explicit progress
    bar->setRange(0, 0);
    bar->setValue(0);
}


void FileOpsProgressDialog::onItemFinished(int, bool success, const QString& error)
{
    m_cancelling = false;
    if (!success) {

        const QString msg = error.isEmpty() ? QString("The file operation failed.") : error;
        label->setText(QString("Error: %1").arg(msg));
        // Surface the error prominently to the user
        QMessageBox::critical(this, tr("File operation failed"), msg);
    }
    refreshList();
    // Additional safety: if queue has no active work, close
    auto items = FileOpsQueue::instance().items();
    bool anyActive = false;
    for (const auto &it : items) {
        if (it.status == "Queued" || it.status == "In Progress") { anyActive = true; break; }
    }
    if (!anyActive) close();
}

