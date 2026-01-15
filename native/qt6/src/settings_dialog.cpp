#include "settings_dialog.h"
#include "db.h"
#include "live_preview_manager.h"
#include "thumbnail_cache_manager.h"
#include "theme_manager.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QApplication>
#include <QStandardPaths>
#include <QScrollArea>
#include <QSettings>
#include <QHeaderView>
#include <QFileInfo>
#include <QCoreApplication>
#include <QCompleter>

#ifdef HAVE_OCIO
#include <OpenColorIO/OpenColorIO.h>
namespace OCIO = OCIO_NAMESPACE;
#endif

namespace {
const QString kOcioEnabledSetting = QStringLiteral("OCIO/Enabled");
const QString kOcioConfigSetting = QStringLiteral("OCIO/ConfigPath");
const QString kOcioDefault8bitSetting = QStringLiteral("OCIO/DefaultInput8bit");
const QString kOcioDefault16bitSetting = QStringLiteral("OCIO/DefaultInput16bit");
const QString kOcioDefault32bitSetting = QStringLiteral("OCIO/DefaultInput32bit");
const QString kOcioDefaultLogSetting = QStringLiteral("OCIO/DefaultInputLog");

QString findBundledAcesConfig()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString relPath = "OpenColorIO-Config-ACES-1.2/aces_1.2/config.ocio";
    const QStringList roots = {
        appDir,
        QDir(appDir).filePath(".."),
        QDir(appDir).filePath("../.."),
        QDir(appDir).filePath("../../.."),
        QDir::currentPath(),
        QDir(QDir::currentPath()).filePath(".."),
        QDir(QDir::currentPath()).filePath("../..")
    };
    for (const QString& root : roots) {
        const QString candidate = QDir(root).filePath(relPath);
        if (QFileInfo::exists(candidate)) {
            return QDir::cleanPath(candidate);
        }
    }
    return QString();
}
} // namespace

#ifdef Q_OS_WIN
#include <windows.h>
#endif

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle("Settings");
    setMinimumSize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    tabWidget = new QTabWidget(this);

    setupGeneralTab();
    setupCacheTab();
    setupViewTab();
    setupColorTab();
    setupShortcutsTab();
    setupExternalAppsTab();
    setupAboutTab();

    loadSettings();

    mainLayout->addWidget(tabWidget);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* saveBtn = new QPushButton("Save", this);
    saveBtn->setProperty("class", "accent");
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    buttonLayout->addWidget(saveBtn);

    QPushButton* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);

    mainLayout->addLayout(buttonLayout);
}

void SettingsDialog::setupGeneralTab()
{
    QWidget* generalTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(generalTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Theme selection
    QGroupBox* themeGroup = new QGroupBox("Appearance", generalTab);
    QVBoxLayout* themeLayout = new QVBoxLayout(themeGroup);

    QLabel* themeLabel = new QLabel("Theme:", themeGroup);
    themeLayout->addWidget(themeLabel);

    themeCombo = new QComboBox(themeGroup);
    themeCombo->addItems({"Dark", "Light"});

    // Load current theme selection
    int currentThemeIndex = (ThemeManager::instance().currentTheme() == ThemeManager::Light) ? 1 : 0;
    themeCombo->setCurrentIndex(currentThemeIndex);

    themeLayout->addWidget(themeCombo);

    layout->addWidget(themeGroup);

    // Playback backend (tlRender removed)
    layout->addStretch();

    tabWidget->addTab(generalTab, "General");
}

void SettingsDialog::setupCacheTab()
{
    QWidget* cacheTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(cacheTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Cache info
    QGroupBox* cacheGroup = new QGroupBox("Live Preview Cache", cacheTab);
    QVBoxLayout* cacheLayout = new QVBoxLayout(cacheGroup);

    cacheSizeLabel = new QLabel(QString("Cached previews: %1 entries").arg(LivePreviewManager::instance().cacheEntryCount()), cacheGroup);
    cacheLayout->addWidget(cacheSizeLabel);

    // Cache size configuration
    QHBoxLayout* cacheSizeLayout = new QHBoxLayout();
    QLabel* maxCacheLabel = new QLabel("Maximum cache size:", cacheGroup);
    cacheSizeLayout->addWidget(maxCacheLabel);

    maxCacheSizeSpin = new QSpinBox(cacheGroup);
    maxCacheSizeSpin->setMinimum(64);
    maxCacheSizeSpin->setMaximum(2048);
    maxCacheSizeSpin->setSingleStep(64);
    maxCacheSizeSpin->setValue(LivePreviewManager::instance().maxCacheEntries());
    cacheSizeLayout->addWidget(maxCacheSizeSpin);

    QLabel* entriesLabel = new QLabel("entries", cacheGroup);
    cacheSizeLayout->addWidget(entriesLabel);
    cacheSizeLayout->addStretch();
    cacheLayout->addLayout(cacheSizeLayout);

    clearCacheBtn = new QPushButton("Clear Preview Cache", cacheGroup);
    clearCacheBtn->setProperty("class", "danger");
    connect(clearCacheBtn, &QPushButton::clicked, this, &SettingsDialog::onClearCache);
    cacheLayout->addWidget(clearCacheBtn);

    layout->addWidget(cacheGroup);

    // Persistent Thumbnail Cache settings
    QGroupBox* thumbCacheGroup = new QGroupBox("Persistent Thumbnail Cache", cacheTab);
    QVBoxLayout* thumbCacheLayout = new QVBoxLayout(thumbCacheGroup);

    // Cache info
    ThumbnailCacheManager& thumbCache = ThumbnailCacheManager::instance();
    qint64 cacheSize = thumbCache.getCacheSize();
    int cachedFiles = thumbCache.getCachedFileCount();
    QString cacheSizeStr = QString::number(cacheSize / (1024.0 * 1024.0), 'f', 2) + " MB";

    QLabel* thumbCacheInfoLabel = new QLabel(QString("Cached files: %1 (%2)").arg(cachedFiles).arg(cacheSizeStr), thumbCacheGroup);
    thumbCacheLayout->addWidget(thumbCacheInfoLabel);

    // Cache directory
    QHBoxLayout* cacheDirLayout = new QHBoxLayout();
    QLabel* cacheDirLabel = new QLabel("Cache directory:", thumbCacheGroup);
    cacheDirLayout->addWidget(cacheDirLabel);

    QLineEdit* cacheDirEdit = new QLineEdit(thumbCache.getCacheDirectory(), thumbCacheGroup);
    cacheDirEdit->setReadOnly(true);
    cacheDirLayout->addWidget(cacheDirEdit);

    QPushButton* browseCacheDirBtn = new QPushButton("Browse...", thumbCacheGroup);
    connect(browseCacheDirBtn, &QPushButton::clicked, this, [this, cacheDirEdit]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Thumbnail Cache Directory", cacheDirEdit->text());
        if (!dir.isEmpty()) {
            cacheDirEdit->setText(dir);
            ThumbnailCacheManager::instance().setCacheDirectory(dir);
        }
    });
    cacheDirLayout->addWidget(browseCacheDirBtn);
    thumbCacheLayout->addLayout(cacheDirLayout);

    // Thumbnail size
    QHBoxLayout* thumbSizeLayout = new QHBoxLayout();
    QLabel* thumbSizeLabel = new QLabel("Generated thumbnail size:", thumbCacheGroup);
    thumbSizeLayout->addWidget(thumbSizeLabel);

    QSpinBox* thumbWidthSpin = new QSpinBox(thumbCacheGroup);
    thumbWidthSpin->setMinimum(128);
    thumbWidthSpin->setMaximum(1024);
    thumbWidthSpin->setSingleStep(64);
    thumbWidthSpin->setValue(thumbCache.getThumbnailSize().width());
    thumbSizeLayout->addWidget(thumbWidthSpin);

    QLabel* xLabel = new QLabel("x", thumbCacheGroup);
    thumbSizeLayout->addWidget(xLabel);

    QSpinBox* thumbHeightSpin = new QSpinBox(thumbCacheGroup);
    thumbHeightSpin->setMinimum(128);
    thumbHeightSpin->setMaximum(1024);
    thumbHeightSpin->setSingleStep(64);
    thumbHeightSpin->setValue(thumbCache.getThumbnailSize().height());
    thumbSizeLayout->addWidget(thumbHeightSpin);

    QLabel* pxLabel = new QLabel("pixels", thumbCacheGroup);
    thumbSizeLayout->addWidget(pxLabel);
    thumbSizeLayout->addStretch();
    thumbCacheLayout->addLayout(thumbSizeLayout);

    // Save thumbnail size on change
    connect(thumbWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [thumbHeightSpin](int w) {
        ThumbnailCacheManager::instance().setThumbnailSize(QSize(w, thumbHeightSpin->value()));
    });
    connect(thumbHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [thumbWidthSpin](int h) {
        ThumbnailCacheManager::instance().setThumbnailSize(QSize(thumbWidthSpin->value(), h));
    });

    // Clear cache button
    QPushButton* clearThumbCacheBtn = new QPushButton("Clear Thumbnail Cache", thumbCacheGroup);
    clearThumbCacheBtn->setProperty("class", "danger");
    connect(clearThumbCacheBtn, &QPushButton::clicked, this, [this, thumbCacheInfoLabel]() {
        QMessageBox::StandardButton reply = QMessageBox::question(
            this, "Clear Thumbnail Cache",
            "Are you sure you want to clear all cached thumbnails?\n\nThis will delete all generated thumbnails and they will need to be regenerated.",
            QMessageBox::Yes | QMessageBox::No
        );
        if (reply == QMessageBox::Yes) {
            ThumbnailCacheManager::instance().clearCache();
            thumbCacheInfoLabel->setText("Cached files: 0 (0.00 MB)");
            QMessageBox::information(this, "Cache Cleared", "Thumbnail cache has been cleared successfully.");
        }
    });
    thumbCacheLayout->addWidget(clearThumbCacheBtn);

    layout->addWidget(thumbCacheGroup);

    // Sequence Cache settings
    QGroupBox* seqCacheGroup = new QGroupBox("Image Sequence Cache", cacheTab);
    QVBoxLayout* seqCacheLayout = new QVBoxLayout(seqCacheGroup);

    // Auto cache size checkbox
    QSettings s("AugmentCode", "KAssetManager");
    bool autoCache = s.value("SequenceCache/AutoSize", true).toBool();
    int autoPercent = s.value("SequenceCache/AutoPercent", 70).toInt();
    int manualSize = s.value("SequenceCache/ManualSize", 100).toInt();

    autoSequenceCacheCheck = new QCheckBox("Automatically calculate cache size based on available RAM", seqCacheGroup);
    autoSequenceCacheCheck->setChecked(autoCache);
    seqCacheLayout->addWidget(autoSequenceCacheCheck);

    // Auto cache percentage
    QHBoxLayout* autoPercentLayout = new QHBoxLayout();
    QLabel* autoPercentLabel = new QLabel("Use", seqCacheGroup);
    autoPercentLayout->addWidget(autoPercentLabel);

    autoSequenceCachePercentSpin = new QSpinBox(seqCacheGroup);
    autoSequenceCachePercentSpin->setMinimum(10);
    autoSequenceCachePercentSpin->setMaximum(90);
    autoSequenceCachePercentSpin->setSingleStep(5);
    autoSequenceCachePercentSpin->setValue(autoPercent);
    autoSequenceCachePercentSpin->setSuffix("%");
    autoSequenceCachePercentSpin->setEnabled(autoCache);
    autoPercentLayout->addWidget(autoSequenceCachePercentSpin);

    QLabel* autoPercentLabel2 = new QLabel("of available RAM", seqCacheGroup);
    autoPercentLayout->addWidget(autoPercentLabel2);
    autoPercentLayout->addStretch();
    seqCacheLayout->addLayout(autoPercentLayout);

    // Manual cache size
    QHBoxLayout* manualSizeLayout = new QHBoxLayout();
    QLabel* manualSizeLabel = new QLabel("Manual cache size:", seqCacheGroup);
    manualSizeLayout->addWidget(manualSizeLabel);

    sequenceCacheSizeSpin = new QSpinBox(seqCacheGroup);
    sequenceCacheSizeSpin->setMinimum(10);
    sequenceCacheSizeSpin->setMaximum(1000);
    sequenceCacheSizeSpin->setSingleStep(10);
    sequenceCacheSizeSpin->setValue(manualSize);
    sequenceCacheSizeSpin->setEnabled(!autoCache);
    manualSizeLayout->addWidget(sequenceCacheSizeSpin);

    QLabel* framesLabel = new QLabel("frames", seqCacheGroup);
    manualSizeLayout->addWidget(framesLabel);
    manualSizeLayout->addStretch();
    seqCacheLayout->addLayout(manualSizeLayout);

    // Memory usage label
    sequenceCacheMemoryLabel = new QLabel("Estimated memory usage: calculating...", seqCacheGroup);
    QFont italicFont = sequenceCacheMemoryLabel->font();
    italicFont.setItalic(true);
    sequenceCacheMemoryLabel->setFont(italicFont);
    seqCacheLayout->addWidget(sequenceCacheMemoryLabel);

    // Connect signals to update UI
    connect(autoSequenceCacheCheck, &QCheckBox::toggled, [this](bool checked) {
        autoSequenceCachePercentSpin->setEnabled(checked);
        sequenceCacheSizeSpin->setEnabled(!checked);
        updateSequenceCacheMemoryLabel();
    });

    connect(autoSequenceCachePercentSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::updateSequenceCacheMemoryLabel);
    connect(sequenceCacheSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::updateSequenceCacheMemoryLabel);

    // Initial update
    updateSequenceCacheMemoryLabel();

    layout->addWidget(seqCacheGroup);

    // Database management
    QGroupBox* dbGroup = new QGroupBox("Database", cacheTab);
    QVBoxLayout* dbLayout = new QVBoxLayout(dbGroup);

    QPushButton* exportDbBtn = new QPushButton("Export Database", dbGroup);
    exportDbBtn->setProperty("class", "accent");
    connect(exportDbBtn, &QPushButton::clicked, this, &SettingsDialog::onExportDatabase);
    dbLayout->addWidget(exportDbBtn);

    QPushButton* importDbBtn = new QPushButton("Import Database", dbGroup);
    importDbBtn->setProperty("class", "accent");
    connect(importDbBtn, &QPushButton::clicked, this, &SettingsDialog::onImportDatabase);
    dbLayout->addWidget(importDbBtn);

    QPushButton* clearDbBtn = new QPushButton("Clear Database (Danger!)", dbGroup);
    clearDbBtn->setProperty("class", "danger");
    connect(clearDbBtn, &QPushButton::clicked, this, &SettingsDialog::onClearDatabase);
    dbLayout->addWidget(clearDbBtn);

    layout->addWidget(dbGroup);
    layout->addStretch();

    tabWidget->addTab(cacheTab, "Cache & Database");
}

void SettingsDialog::setupViewTab()
{
    QWidget* viewTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(viewTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // View options
    QGroupBox* viewGroup = new QGroupBox("View Options", viewTab);
    QVBoxLayout* viewLayout = new QVBoxLayout(viewGroup);

    QLabel* thumbnailLabel = new QLabel("Thumbnail Size:", viewGroup);
    viewLayout->addWidget(thumbnailLabel);

    thumbnailSizeSpin = new QSpinBox(viewGroup);
    thumbnailSizeSpin->setRange(64, 512);
    thumbnailSizeSpin->setValue(200);
    thumbnailSizeSpin->setSuffix(" px");
    viewLayout->addWidget(thumbnailSizeSpin);

    showFileExtensionsCheck = new QCheckBox("Show file extensions", viewGroup);
    showFileExtensionsCheck->setChecked(true);
    viewLayout->addWidget(showFileExtensionsCheck);

    showSequenceOverlayCheck = new QCheckBox("Show sequence overlay badges", viewGroup);
    showSequenceOverlayCheck->setChecked(true);
    viewLayout->addWidget(showSequenceOverlayCheck);

    layout->addWidget(viewGroup);
    layout->addStretch();

    tabWidget->addTab(viewTab, "View");
}

void SettingsDialog::setupColorTab()
{
    QWidget* colorTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(colorTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QGroupBox* ocioGroup = new QGroupBox("OCIO Configuration", colorTab);
    QVBoxLayout* ocioLayout = new QVBoxLayout(ocioGroup);

    QHBoxLayout* configLayout = new QHBoxLayout();
    QLabel* configLabel = new QLabel("OCIO config:", ocioGroup);
    configLayout->addWidget(configLabel);

    ocioConfigPathEdit = new QLineEdit(ocioGroup);
    ocioConfigPathEdit->setPlaceholderText("Path to config.ocio");
    configLayout->addWidget(ocioConfigPathEdit, 1);

    ocioConfigBrowseBtn = new QPushButton("Browse...", ocioGroup);
    configLayout->addWidget(ocioConfigBrowseBtn);

    ocioConfigBundledBtn = new QPushButton("Use Bundled ACES 1.2", ocioGroup);
    configLayout->addWidget(ocioConfigBundledBtn);

    ocioLayout->addLayout(configLayout);

    ocioEnabledCheck = new QCheckBox("Enable OCIO by default", ocioGroup);
    ocioLayout->addWidget(ocioEnabledCheck);

    layout->addWidget(ocioGroup);

    QGroupBox* defaultsGroup = new QGroupBox("Default Input Transforms", colorTab);
    QGridLayout* defaultsLayout = new QGridLayout(defaultsGroup);
    defaultsLayout->setHorizontalSpacing(12);
    defaultsLayout->setVerticalSpacing(10);

    QLabel* bit8Label = new QLabel("8-bit files", defaultsGroup);
    ocioDefault8bitCombo = new QComboBox(defaultsGroup);
    setSearchableCombo(ocioDefault8bitCombo);

    QLabel* bit16Label = new QLabel("16-bit files", defaultsGroup);
    ocioDefault16bitCombo = new QComboBox(defaultsGroup);
    setSearchableCombo(ocioDefault16bitCombo);

    QLabel* bit32Label = new QLabel("32-bit files", defaultsGroup);
    ocioDefault32bitCombo = new QComboBox(defaultsGroup);
    setSearchableCombo(ocioDefault32bitCombo);

    QLabel* logLabel = new QLabel("Log files", defaultsGroup);
    ocioDefaultLogCombo = new QComboBox(defaultsGroup);
    setSearchableCombo(ocioDefaultLogCombo);

    defaultsLayout->addWidget(bit8Label, 0, 0);
    defaultsLayout->addWidget(ocioDefault8bitCombo, 0, 1);
    defaultsLayout->addWidget(bit16Label, 1, 0);
    defaultsLayout->addWidget(ocioDefault16bitCombo, 1, 1);
    defaultsLayout->addWidget(bit32Label, 2, 0);
    defaultsLayout->addWidget(ocioDefault32bitCombo, 2, 1);
    defaultsLayout->addWidget(logLabel, 3, 0);
    defaultsLayout->addWidget(ocioDefaultLogCombo, 3, 1);

    defaultsLayout->setColumnStretch(1, 1);
    layout->addWidget(defaultsGroup);
    layout->addStretch();

    connect(ocioConfigBrowseBtn, &QPushButton::clicked, this, [this]() {
        const QString filePath = QFileDialog::getOpenFileName(
            this,
            tr("Select OCIO config"),
            ocioConfigPathEdit ? ocioConfigPathEdit->text() : QString(),
            tr("OCIO Config (config.ocio);;All Files (*.*)")
        );
        if (!filePath.isEmpty() && ocioConfigPathEdit) {
            ocioConfigPathEdit->setText(filePath);
            refreshOcioDefaults();
        }
    });

    connect(ocioConfigBundledBtn, &QPushButton::clicked, this, [this]() {
        const QString bundled = findBundledAcesConfig();
        if (bundled.isEmpty()) {
            QMessageBox::warning(this, "OCIO Config", "Bundled ACES config not found.");
            return;
        }
        if (ocioConfigPathEdit) {
            ocioConfigPathEdit->setText(bundled);
            refreshOcioDefaults();
        }
    });

    connect(ocioConfigPathEdit, &QLineEdit::editingFinished, this, &SettingsDialog::refreshOcioDefaults);

    tabWidget->addTab(colorTab, "Color");
}

void SettingsDialog::setupShortcutsTab()
{
    QWidget* shortcutsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(shortcutsTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    QLabel* title = new QLabel("File Manager Keyboard Shortcuts", shortcutsTab);
    QFont titleFont = title->font();
    titleFont.setPointSize(14);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    // Table: Action | Shortcut | Reset
    fmShortcutsTable = new QTableWidget(shortcutsTab);
    fmShortcutsTable->setColumnCount(3);
    QStringList headers; headers << "Action" << "Shortcut" << "Reset";
    fmShortcutsTable->setHorizontalHeaderLabels(headers);
    fmShortcutsTable->horizontalHeader()->setStretchLastSection(false);
    fmShortcutsTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    fmShortcutsTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    fmShortcutsTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    fmShortcutsTable->verticalHeader()->setVisible(false);
    fmShortcutsTable->setAlternatingRowColors(true);

    struct Row { const char* name; QKeySequence def; const char* label; };
    const Row rows[] = {
        {"OpenOverlay", QKeySequence(Qt::Key_Space), "Open Overlay/Preview"},
        {"Copy", QKeySequence::Copy, "Copy"},
        {"Cut", QKeySequence::Cut, "Cut"},
        {"Paste", QKeySequence::Paste, "Paste"},
        {"Delete", QKeySequence::Delete, "Delete (Recycle Bin)"},
        {"Rename", QKeySequence(Qt::Key_F2), "Rename"},
        {"DeletePermanent", QKeySequence(Qt::SHIFT | Qt::Key_Delete), "Permanent Delete"},
        {"NewFolder", QKeySequence::New, "New Folder"},
        {"CreateFolderWithSelected", QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), "Create Folder with Selected Files"},
        {"BackToParent", QKeySequence(Qt::Key_Backspace), "Back to Parent"}
    };

    QSettings s("AugmentCode", "KAssetManager");
    s.beginGroup("FileManager/Shortcuts");
    fmShortcutsTable->setRowCount(int(sizeof(rows)/sizeof(rows[0])));

    for (int i=0; i< int(sizeof(rows)/sizeof(rows[0])); ++i) {
        const QString actionName = rows[i].name;
        const QKeySequence def = rows[i].def;
        const QString label = rows[i].label;

        // Action label
        auto item = new QTableWidgetItem(label);
        item->setData(Qt::UserRole, actionName);
        item->setData(Qt::UserRole+1, rows[i].def.toString(QKeySequence::PortableText)); // default
        fmShortcutsTable->setItem(i, 0, item);

        // Shortcut editor
        auto editor = new QKeySequenceEdit();
        QString stored = s.value(actionName).toString();
        QKeySequence seq = stored.isEmpty() ? def : QKeySequence(stored);
        editor->setKeySequence(seq);
        fmShortcutsTable->setCellWidget(i, 1, editor);

        // Reset button
        auto resetBtn = new QPushButton("Reset");
        resetBtn->setProperty("actionName", actionName);
        QObject::connect(resetBtn, &QPushButton::clicked, this, [this, i]() {
            QTableWidgetItem* it = fmShortcutsTable->item(i, 0);
            if (!it) return;
            QString defStr = it->data(Qt::UserRole+1).toString();
            auto ed = qobject_cast<QKeySequenceEdit*>(fmShortcutsTable->cellWidget(i, 1));
            if (ed) ed->setKeySequence(QKeySequence(defStr));
        });
        fmShortcutsTable->setCellWidget(i, 2, resetBtn);
    }
    s.endGroup();

    layout->addWidget(fmShortcutsTable);

    // Footer buttons
    QHBoxLayout* footer = new QHBoxLayout();
    footer->addStretch();
    fmResetAllBtn = new QPushButton("Reset All");
    connect(fmResetAllBtn, &QPushButton::clicked, this, [this]() {
        for (int r=0; r<fmShortcutsTable->rowCount(); ++r) {
            QTableWidgetItem* it = fmShortcutsTable->item(r, 0);
            if (!it) continue;
            QString defStr = it->data(Qt::UserRole+1).toString();
            auto ed = qobject_cast<QKeySequenceEdit*>(fmShortcutsTable->cellWidget(r, 1));
            if (ed) ed->setKeySequence(QKeySequence(defStr));
        }
    });
    footer->addWidget(fmResetAllBtn);
    layout->addLayout(footer);

    tabWidget->addTab(shortcutsTab, "Shortcuts");
}

void SettingsDialog::setupExternalAppsTab()
{
    QWidget* extAppsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(extAppsTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Info label
    QLabel* infoLabel = new QLabel(
        "Configure external applications used to open project files.\n"
        "These paths are used when double-clicking .aep, .aepx, or .nk files in the Project Manager.",
        extAppsTab);
    QPalette infoPal = infoLabel->palette();
    infoPal.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::PlaceholderText));
    infoLabel->setPalette(infoPal);
    infoLabel->setWordWrap(true);
    layout->addWidget(infoLabel);

    // Load settings
    QSettings s("AugmentCode", "KAssetManager");

    // After Effects group
    QGroupBox* aeGroup = new QGroupBox("Adobe After Effects", extAppsTab);
    QVBoxLayout* aeLayout = new QVBoxLayout(aeGroup);

    QLabel* aeLabel = new QLabel("After Effects executable path:", aeGroup);
    aeLayout->addWidget(aeLabel);

    QHBoxLayout* aePathLayout = new QHBoxLayout();
    afterEffectsPathEdit = new QLineEdit(aeGroup);
    afterEffectsPathEdit->setText(s.value("ExternalApps/AfterEffectsPath", "").toString());
    afterEffectsPathEdit->setPlaceholderText("e.g., C:\\Program Files\\Adobe\\Adobe After Effects 2024\\Support Files\\AfterFX.exe");
    aePathLayout->addWidget(afterEffectsPathEdit);

    QPushButton* aeBrowseBtn = new QPushButton("Browse...", aeGroup);
    connect(aeBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this,
            "Select After Effects Executable",
            "C:/Program Files/Adobe",
            "Executables (*.exe);;All Files (*.*)"
        );
        if (!path.isEmpty()) {
            afterEffectsPathEdit->setText(path);
        }
    });
    aePathLayout->addWidget(aeBrowseBtn);
    aeLayout->addLayout(aePathLayout);

    QLabel* aeHintLabel = new QLabel("Supported file types: .aep, .aepx", aeGroup);
    QPalette aeHintPal = aeHintLabel->palette();
    aeHintPal.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Disabled, QPalette::WindowText));
    aeHintLabel->setPalette(aeHintPal);
    aeLayout->addWidget(aeHintLabel);

    layout->addWidget(aeGroup);

    // Nuke group
    QGroupBox* nukeGroup = new QGroupBox("Foundry NukeX", extAppsTab);
    QVBoxLayout* nukeLayout = new QVBoxLayout(nukeGroup);

    QLabel* nukeLabel = new QLabel("NukeX executable path:", nukeGroup);
    nukeLayout->addWidget(nukeLabel);

    QHBoxLayout* nukePathLayout = new QHBoxLayout();
    nukeXPathEdit = new QLineEdit(nukeGroup);
    nukeXPathEdit->setText(s.value("ExternalApps/NukeXPath", "").toString());
    nukeXPathEdit->setPlaceholderText("e.g., C:\\Program Files\\Nuke15.1v1\\Nuke15.1.exe");
    nukePathLayout->addWidget(nukeXPathEdit);

    QPushButton* nukeBrowseBtn = new QPushButton("Browse...", nukeGroup);
    connect(nukeBrowseBtn, &QPushButton::clicked, this, [this]() {
        QString path = QFileDialog::getOpenFileName(
            this,
            "Select NukeX Executable",
            "C:/Program Files",
            "Executables (*.exe);;All Files (*.*)"
        );
        if (!path.isEmpty()) {
            nukeXPathEdit->setText(path);
        }
    });
    nukePathLayout->addWidget(nukeBrowseBtn);
    nukeLayout->addLayout(nukePathLayout);

    QLabel* nukeHintLabel = new QLabel("Supported file types: .nk", nukeGroup);
    QPalette nukeHintPal = nukeHintLabel->palette();
    nukeHintPal.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::Disabled, QPalette::WindowText));
    nukeHintLabel->setPalette(nukeHintPal);
    nukeLayout->addWidget(nukeHintLabel);

    layout->addWidget(nukeGroup);

    // Add stretch to push content to top
    layout->addStretch();

    tabWidget->addTab(extAppsTab, "External Apps");
}

void SettingsDialog::setupAboutTab()
{
    QWidget* aboutTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(aboutTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel* appName = new QLabel("KAsset Manager", aboutTab);
    QFont appNameFont = appName->font();
    appNameFont.setPointSize(18);
    appNameFont.setBold(true);
    appName->setFont(appNameFont);
    appName->setAlignment(Qt::AlignCenter);
    layout->addWidget(appName);

    versionLabel = new QLabel(QString("Version %1").arg(QCoreApplication::applicationVersion()), aboutTab);
    QPalette versionPal = versionLabel->palette();
    versionPal.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::PlaceholderText));
    versionLabel->setPalette(versionPal);
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    qtVersionLabel = new QLabel(QString("Built with Qt %1").arg(QT_VERSION_STR), aboutTab);
    QPalette qtVersionPal = qtVersionLabel->palette();
    qtVersionPal.setColor(QPalette::WindowText, QApplication::palette().color(QPalette::PlaceholderText));
    qtVersionLabel->setPalette(qtVersionPal);
    qtVersionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(qtVersionLabel);

    QLabel* licensesTitle = new QLabel("Third-Party Licenses", aboutTab);
    QFont licensesTitleFont = licensesTitle->font();
    licensesTitleFont.setPointSize(14);
    licensesTitleFont.setBold(true);
    licensesTitle->setFont(licensesTitleFont);
    layout->addWidget(licensesTitle);

    licensesText = new QTextEdit(aboutTab);
    licensesText->setReadOnly(true);
    licensesText->setHtml(
        "<h3>Qt Framework</h3>"
        "<p>Licensed under LGPL v3</p>"
        "<p><a href='https://www.qt.io/licensing/'>https://www.qt.io/licensing/</a></p>"
        "<h3>OpenImageIO</h3>"
        "<p>Licensed under Apache 2.0</p>"
        "<p><a href='https://github.com/AcademySoftwareFoundation/OpenImageIO'>https://github.com/AcademySoftwareFoundation/OpenImageIO</a></p>"
        "<h3>SQLite</h3>"
        "<p>Public Domain</p>"
        "<p><a href='https://www.sqlite.org/copyright.html'>https://www.sqlite.org/copyright.html</a></p>"
    );
    layout->addWidget(licensesText);

    tabWidget->addTab(aboutTab, "About");
}

void SettingsDialog::setSearchableCombo(QComboBox* combo) const
{
    if (!combo) return;
    combo->setEditable(true);
    combo->setInsertPolicy(QComboBox::NoInsert);
    if (auto *edit = combo->lineEdit()) {
        edit->setPlaceholderText("Search...");
        edit->setClearButtonEnabled(true);
    }
    auto *completer = new QCompleter(combo);
    completer->setModel(combo->model());
    completer->setCompletionMode(QCompleter::PopupCompletion);
    completer->setFilterMode(Qt::MatchContains);
    completer->setCaseSensitivity(Qt::CaseInsensitive);
    combo->setCompleter(completer);
}

QStringList SettingsDialog::loadOcioColorspaces(const QString& configPath) const
{
#ifdef HAVE_OCIO
    try {
        OCIO::ConstConfigRcPtr config = OCIO::Config::CreateFromFile(configPath.toStdString().c_str());
        if (!config) {
            return {};
        }
        QStringList colorspaces;
        const int count = config->getNumColorSpaces();
        colorspaces.reserve(count);
        for (int i = 0; i < count; ++i) {
            colorspaces.append(QString::fromStdString(config->getColorSpaceNameByIndex(i)));
        }
        return colorspaces;
    } catch (const OCIO::Exception&) {
        return {};
    }
#else
    Q_UNUSED(configPath);
    return {};
#endif
}

void SettingsDialog::refreshOcioDefaults()
{
    const QString configPath = ocioConfigPathEdit ? ocioConfigPathEdit->text().trimmed() : QString();
    QStringList colorspaces;
    if (!configPath.isEmpty() && QFileInfo::exists(configPath)) {
        colorspaces = loadOcioColorspaces(configPath);
    }

    auto applyList = [&](QComboBox* combo, const QString& key) {
        if (!combo) return;
        combo->blockSignals(true);
        combo->clear();
        combo->addItems(colorspaces);
        combo->setEnabled(!colorspaces.isEmpty());

        if (!colorspaces.isEmpty()) {
            QSettings s("AugmentCode", "KAssetManager");
            const QString stored = s.value(key).toString();
            const int idx = stored.isEmpty() ? -1 : combo->findText(stored);
            if (idx >= 0) {
                combo->setCurrentIndex(idx);
            } else {
                combo->setCurrentIndex(-1);
                if (auto *edit = combo->lineEdit()) {
                    edit->clear();
                }
            }
        }
        combo->blockSignals(false);
    };

    applyList(ocioDefault8bitCombo, kOcioDefault8bitSetting);
    applyList(ocioDefault16bitCombo, kOcioDefault16bitSetting);
    applyList(ocioDefault32bitCombo, kOcioDefault32bitSetting);
    applyList(ocioDefaultLogCombo, kOcioDefaultLogSetting);
}

void SettingsDialog::onClearCache()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Clear Cache",
        "Are you sure you want to clear the in-memory preview cache?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        LivePreviewManager::instance().clear();
        QMessageBox::information(this, "Cache Cleared", "Live preview cache has been cleared successfully.");
        cacheSizeLabel->setText(QString("Cached previews: %1 entries").arg(0));
    }
}

void SettingsDialog::onClearDatabase()
{
    QMessageBox::StandardButton reply = QMessageBox::warning(
        this,
        "Clear Database",
        "WARNING: This will delete ALL data including folders, assets, tags, and ratings!\n\nThis action cannot be undone. Are you absolutely sure?",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        DB::instance().clearAllData();
        QMessageBox::information(this, "Database Cleared", "Database has been cleared. Please restart the application.");
    }
}

void SettingsDialog::onExportDatabase()
{
    QString fileName = QFileDialog::getSaveFileName(
        this,
        "Export Database",
        QDir::homePath() + "/kassetmanager_backup.db",
        "SQLite Database (*.db)"
    );

    if (!fileName.isEmpty()) {
        if (DB::instance().exportDatabase(fileName)) {
            QMessageBox::information(this, "Export Successful", "Database exported successfully.");
        } else {
            QMessageBox::critical(this, "Export Failed", "Failed to export database.");
        }
    }
}

void SettingsDialog::onImportDatabase()
{
    QString fileName = QFileDialog::getOpenFileName(
        this,
        "Import Database",
        QDir::homePath(),
        "SQLite Database (*.db)"
    );

    if (!fileName.isEmpty()) {
        QMessageBox::StandardButton reply = QMessageBox::warning(
            this,
            "Import Database",
            "WARNING: This will replace your current database with the imported one!\n\nAre you sure?",
            QMessageBox::Yes | QMessageBox::No
        );

        if (reply == QMessageBox::Yes) {
            if (DB::instance().importDatabase(fileName)) {
                QMessageBox::information(this, "Import Successful", "Database imported successfully. Please restart the application.");
            } else {
                QMessageBox::critical(this, "Import Failed", "Failed to import database.");
            }
        }
    }
}

void SettingsDialog::loadSettings()
{
    QSettings s("AugmentCode", "KAssetManager");

    // Load theme
    if (themeCombo) {
        int themeIndex = s.value("Appearance/Theme", 0).toInt();
        themeCombo->setCurrentIndex(themeIndex);
    }

    if (ocioEnabledCheck) {
        ocioEnabledCheck->setChecked(s.value(kOcioEnabledSetting, true).toBool());
    }
    if (ocioConfigPathEdit) {
        QString configPath = s.value(kOcioConfigSetting).toString();
        if (configPath.isEmpty()) {
            configPath = findBundledAcesConfig();
        }
        ocioConfigPathEdit->setText(configPath);
    }
    refreshOcioDefaults();
}

void SettingsDialog::saveSettings()
{
    QSettings s("AugmentCode", "KAssetManager");

    // Save theme
    if (themeCombo) {
        int themeIndex = themeCombo->currentIndex();
        ThemeManager::Theme theme = (themeIndex == 1) ? ThemeManager::Light : ThemeManager::Dark;
        ThemeManager::instance().setTheme(theme);
        ThemeManager::instance().saveTheme();
    }

    if (ocioEnabledCheck) {
        s.setValue(kOcioEnabledSetting, ocioEnabledCheck->isChecked());
    }
    if (ocioConfigPathEdit) {
        s.setValue(kOcioConfigSetting, ocioConfigPathEdit->text().trimmed());
    }
    if (ocioDefault8bitCombo) {
        const QString value = ocioDefault8bitCombo->currentText().trimmed();
        if (!value.isEmpty() && ocioDefault8bitCombo->findText(value) >= 0) {
            s.setValue(kOcioDefault8bitSetting, value);
        }
    }
    if (ocioDefault16bitCombo) {
        const QString value = ocioDefault16bitCombo->currentText().trimmed();
        if (!value.isEmpty() && ocioDefault16bitCombo->findText(value) >= 0) {
            s.setValue(kOcioDefault16bitSetting, value);
        }
    }
    if (ocioDefault32bitCombo) {
        const QString value = ocioDefault32bitCombo->currentText().trimmed();
        if (!value.isEmpty() && ocioDefault32bitCombo->findText(value) >= 0) {
            s.setValue(kOcioDefault32bitSetting, value);
        }
    }
    if (ocioDefaultLogCombo) {
        const QString value = ocioDefaultLogCombo->currentText().trimmed();
        if (!value.isEmpty() && ocioDefaultLogCombo->findText(value) >= 0) {
            s.setValue(kOcioDefaultLogSetting, value);
        }
    }

    // Save cache size setting
    if (maxCacheSizeSpin) {
        int cacheSize = maxCacheSizeSpin->value();
        LivePreviewManager::instance().setMaxCacheEntries(cacheSize);
        s.setValue("LivePreview/MaxCacheEntries", cacheSize);
    }

    // Save sequence cache settings
    if (autoSequenceCacheCheck) {
        s.setValue("SequenceCache/AutoSize", autoSequenceCacheCheck->isChecked());
    }
    if (autoSequenceCachePercentSpin) {
        s.setValue("SequenceCache/AutoPercent", autoSequenceCachePercentSpin->value());
    }
    if (sequenceCacheSizeSpin) {
        s.setValue("SequenceCache/ManualSize", sequenceCacheSizeSpin->value());
    }

    // Save external application paths
    if (afterEffectsPathEdit) {
        s.setValue("ExternalApps/AfterEffectsPath", afterEffectsPathEdit->text());
    }
    if (nukeXPathEdit) {
        s.setValue("ExternalApps/NukeXPath", nukeXPathEdit->text());
    }

    // Persist File Manager shortcuts
    if (fmShortcutsTable) {
        // Detect conflicts
        QSet<QString> seen;
        QStringList conflicts;
        struct Entry { QString action; QString seq; QString def; };
        QList<Entry> entries;
        for (int r=0; r<fmShortcutsTable->rowCount(); ++r) {
            QTableWidgetItem* it = fmShortcutsTable->item(r, 0);
            auto ed = qobject_cast<QKeySequenceEdit*>(fmShortcutsTable->cellWidget(r, 1));
            if (!it || !ed) continue;
            QString action = it->data(Qt::UserRole).toString();
            QString defStr = it->data(Qt::UserRole+1).toString();
            QString seqStr = ed->keySequence().toString(QKeySequence::PortableText);
            if (!seqStr.isEmpty()) {
                if (seen.contains(seqStr)) conflicts << seqStr;
                else seen.insert(seqStr);
            }
            entries.append({action, seqStr, defStr});
        }
        if (!conflicts.isEmpty()) {
            QMessageBox::warning(this, "Shortcut Conflict",
                                 QString("Conflicting shortcuts detected: %1\nPlease resolve duplicates before saving.")
                                 .arg(conflicts.join(", ")));
            return;
        }
        QSettings s("AugmentCode", "KAssetManager");
        s.beginGroup("FileManager/Shortcuts");
        // Remove all existing keys first to avoid stale entries
        for (const QString& k : s.childKeys()) s.remove(k);
        for (const auto& e : entries) {
            if (!e.seq.isEmpty()) s.setValue(e.action, e.seq);
            // Empty means use default; don't set value
        }
        s.endGroup();
    }

    QMessageBox::information(this, "Settings Saved", "Settings have been saved successfully.");
    accept();
}

void SettingsDialog::updateSequenceCacheMemoryLabel()
{
    if (!sequenceCacheMemoryLabel || !autoSequenceCacheCheck ||
        !autoSequenceCachePercentSpin || !sequenceCacheSizeSpin) {
        return;
    }

    int cacheFrames = 0;
    QString source;

    if (autoSequenceCacheCheck->isChecked()) {
        // Calculate based on available RAM
        int percent = autoSequenceCachePercentSpin->value();

        // Get available RAM (use a simple estimate for now)
        #ifdef Q_OS_WIN
        MEMORYSTATUSEX memInfo;
        memInfo.dwLength = sizeof(MEMORYSTATUSEX);
        qint64 availableRAM = 8192; // Default 8GB
        if (GlobalMemoryStatusEx(&memInfo)) {
            availableRAM = static_cast<qint64>(memInfo.ullAvailPhys / (1024 * 1024));
        }
        #else
        qint64 availableRAM = 8192; // Default 8GB for non-Windows
        #endif

        // Calculate cache size
        const int avgFrameSizeMB = 30;
        qint64 cacheRAM = (availableRAM * percent) / 100;
        cacheFrames = static_cast<int>(cacheRAM / avgFrameSizeMB);
        cacheFrames = qMax(10, qMin(500, cacheFrames));

        source = QString("Auto: %1% of %2 GB RAM").arg(percent).arg(availableRAM / 1024.0, 0, 'f', 1);
    } else {
        // Use manual size
        cacheFrames = sequenceCacheSizeSpin->value();
        source = "Manual";
    }

    // Calculate memory usage
    int memoryMB = cacheFrames * 30; // 30MB average per frame
    double memoryGB = memoryMB / 1024.0;

    QString text = QString("Estimated memory usage: %1 frames (~%2 GB) - %3")
                       .arg(cacheFrames)
                       .arg(memoryGB, 0, 'f', 2)
                       .arg(source);

    sequenceCacheMemoryLabel->setText(text);
}



