#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTextEdit>

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private slots:
    void onClearCache();
    void onClearDatabase();
    void onExportDatabase();
    void onImportDatabase();
    void saveSettings();

private:
    void setupGeneralTab();
    void setupCacheTab();
    void setupViewTab();
    void setupShortcutsTab();
    void setupAboutTab();
    
    QTabWidget* tabWidget;
    
    // General tab
    QComboBox* themeCombo;
    
    // Cache tab
    QLabel* cacheSizeLabel;
    QPushButton* clearCacheBtn;
    QSpinBox* maxCacheSizeSpin;
    
    // View tab
    QComboBox* viewModeCombo;
    QSpinBox* thumbnailSizeSpin;
    QCheckBox* showFileExtensionsCheck;
    QCheckBox* showSequenceOverlayCheck;
    
    // Shortcuts tab
    QTextEdit* shortcutsText;
    
    // About tab
    QLabel* versionLabel;
    QLabel* qtVersionLabel;
    QTextEdit* licensesText;
};

