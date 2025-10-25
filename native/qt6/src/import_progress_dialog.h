#pragma once

#include <QDialog>
#include <QProgressBar>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

class ImportProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit ImportProgressDialog(QWidget* parent = nullptr);
    
    void setProgress(int current, int total);
    void setCurrentFile(const QString& fileName);
    void setCurrentFolder(const QString& folderName);
    void setComplete();
    
private:
    QLabel* titleLabel;
    QLabel* fileLabel;
    QProgressBar* progressBar;
    QPushButton* closeButton;
};

