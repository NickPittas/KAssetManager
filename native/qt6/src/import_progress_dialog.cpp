#include "import_progress_dialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QApplication>
#include <QStyle>
#include <QTimer>

ImportProgressDialog::ImportProgressDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle("Importing Assets");
    setModal(false);  // Non-modal so app stays responsive
    setMinimumWidth(500);

    // Keep on top but allow interaction with main window
    setWindowFlags(windowFlags() | Qt::WindowStaysOnTopHint);
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(15);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    
    // Title label
    titleLabel = new QLabel("Importing assets...", this);
    QFont titleFont = titleLabel->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    titleLabel->setFont(titleFont);
    mainLayout->addWidget(titleLabel);
    
    // Current file label
    fileLabel = new QLabel("", this);
    fileLabel->setWordWrap(true);
    fileLabel->setStyleSheet("color: #666;");
    mainLayout->addWidget(fileLabel);
    
    // Progress bar
    progressBar = new QProgressBar(this);
    progressBar->setMinimum(0);
    progressBar->setMaximum(100);
    progressBar->setValue(0);
    progressBar->setTextVisible(true);
    progressBar->setFormat("%v / %m files (%p%)");
    progressBar->setMinimumHeight(25);
    mainLayout->addWidget(progressBar);
    
    // Close button (initially hidden)
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    closeButton = new QPushButton("Close", this);
    closeButton->setVisible(false);
    closeButton->setMinimumWidth(100);
    connect(closeButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
    
    // Center on parent
    if (parent) {
        move(parent->geometry().center() - rect().center());
    }
}

void ImportProgressDialog::setProgress(int current, int total)
{
    progressBar->setMaximum(total);
    progressBar->setValue(current);
    progressBar->setFormat(QString("%1 / %2 files (%p%)").arg(current).arg(total));
    
    // Process events to keep UI responsive
    QApplication::processEvents();
}

void ImportProgressDialog::setCurrentFile(const QString& fileName)
{
    fileLabel->setText(QString("Processing: %1").arg(fileName));

    // Process events to keep UI responsive
    QApplication::processEvents();
}

void ImportProgressDialog::setCurrentFolder(const QString& folderName)
{
    titleLabel->setText(QString("Importing folder: %1").arg(folderName));
    fileLabel->setText("");
    progressBar->setValue(0);

    // Process events to keep UI responsive
    QApplication::processEvents();
}

void ImportProgressDialog::setComplete()
{
    titleLabel->setText("Import Complete!");
    fileLabel->setText(QString("Successfully imported %1 files").arg(progressBar->value()));
    closeButton->setVisible(true);
    closeButton->setFocus();

    // Auto-close after 1.5 seconds
    QTimer::singleShot(1500, this, &QDialog::accept);
}

