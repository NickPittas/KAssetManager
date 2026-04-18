#pragma once

#include <QDialog>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>
#include <QComboBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QGroupBox>
#include <QTextEdit>
#include <QTableWidget>
#include <QKeySequenceEdit>
#include <QLineEdit>
#include <QStringList>

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
    void updateSequenceCacheMemoryLabel();

private:
    void setupGeneralTab();
    void setupCacheTab();
    void setupViewTab();
    void setupColorTab();
    void setupShortcutsTab();
    void setupExternalAppsTab();
    void setupAboutTab();
    void loadSettings();
    void refreshOcioDefaults();
    QStringList loadOcioColorspaces(const QString& configPath) const;
    void setSearchableCombo(QComboBox* combo) const;

    QTabWidget* tabWidget;

    // General tab
    QComboBox* themeCombo;

    // Cache tab
    QLabel* cacheSizeLabel;
    QPushButton* clearCacheBtn;
    QSpinBox* maxCacheSizeSpin;
    QSpinBox* imageMemoryLimitSpin;

    // Sequence cache settings
    QSpinBox* sequenceCacheSizeSpin;
    QLabel* sequenceCacheMemoryLabel;
    QCheckBox* autoSequenceCacheCheck;
    QSpinBox* autoSequenceCachePercentSpin;

    // View tab
    QComboBox* viewModeCombo;
    QSpinBox* thumbnailSizeSpin;
    QCheckBox* showFileExtensionsCheck;
    QCheckBox* showSequenceOverlayCheck;

    // Color tab
    QLineEdit* ocioConfigPathEdit = nullptr;
    QPushButton* ocioConfigBrowseBtn = nullptr;
    QPushButton* ocioConfigBundledBtn = nullptr;
    QCheckBox* ocioEnabledCheck = nullptr;
    QComboBox* ocioDefault8bitCombo = nullptr;
    QComboBox* ocioDefault16bitCombo = nullptr;
    QComboBox* ocioDefault32bitCombo = nullptr;
    QComboBox* ocioDefaultLogCombo = nullptr;

    // Shortcuts tab (editable)
    QTableWidget* fmShortcutsTable;
    QPushButton* fmResetAllBtn;

    // External Applications tab
    QLineEdit* nukeXPathEdit;
    QLineEdit* afterEffectsPathEdit;

    // About tab
    QLabel* versionLabel;
    QLabel* qtVersionLabel;
    QTextEdit* licensesText;
};
