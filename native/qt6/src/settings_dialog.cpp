#include "settings_dialog.h"
#include "db.h"
#include "thumbnail_generator.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QApplication>
#include <QStandardPaths>
#include <QScrollArea>
#include <QSettings>
#include <QHeaderView>

SettingsDialog::SettingsDialog(QWidget* parent)
    : QDialog(parent, Qt::Window | Qt::WindowTitleHint | Qt::WindowCloseButtonHint)
{
    setWindowTitle("Settings");
    setMinimumSize(600, 500);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    tabWidget = new QTabWidget(this);
    tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #333; background-color: #1a1a1a; }"
        "QTabBar::tab { background-color: #2a2a2a; color: #ffffff; padding: 8px 16px; border: 1px solid #333; }"
        "QTabBar::tab:selected { background-color: #1a1a1a; border-bottom-color: #1a1a1a; }"
        "QTabBar::tab:hover { background-color: #333; }"
    );

    setupGeneralTab();
    setupCacheTab();
    setupViewTab();
    setupShortcutsTab();
    setupAboutTab();

    mainLayout->addWidget(tabWidget);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    QPushButton* saveBtn = new QPushButton("Save", this);
    saveBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 8px 24px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::saveSettings);
    buttonLayout->addWidget(saveBtn);

    QPushButton* closeBtn = new QPushButton("Close", this);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #333; color: #ffffff; border: none; padding: 8px 24px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #444; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(closeBtn);

    mainLayout->addLayout(buttonLayout);

    setStyleSheet("QDialog { background-color: #121212; color: #ffffff; }");
}

void SettingsDialog::setupGeneralTab()
{
    QWidget* generalTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(generalTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    // Theme selection
    QGroupBox* themeGroup = new QGroupBox("Appearance", generalTab);
    themeGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid #333; padding: 10px; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* themeLayout = new QVBoxLayout(themeGroup);

    QLabel* themeLabel = new QLabel("Theme:", themeGroup);
    themeLabel->setStyleSheet("color: #ffffff;");
    themeLayout->addWidget(themeLabel);

    themeCombo = new QComboBox(themeGroup);
    themeCombo->addItems({"Dark (Default)", "Light (Not Implemented)"});
    themeCombo->setCurrentIndex(0);
    themeCombo->setStyleSheet(
        "QComboBox { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    themeLayout->addWidget(themeCombo);

    layout->addWidget(themeGroup);
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
    QGroupBox* cacheGroup = new QGroupBox("Thumbnail Cache", cacheTab);
    cacheGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid #333; padding: 10px; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* cacheLayout = new QVBoxLayout(cacheGroup);

    // Calculate cache size
    QString cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    qint64 cacheSize = 0;
    QDirIterator it(cacheDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        cacheSize += QFileInfo(it.filePath()).size();
    }
    double cacheSizeMB = cacheSize / (1024.0 * 1024.0);

    cacheSizeLabel = new QLabel(QString("Current cache size: %1 MB").arg(cacheSizeMB, 0, 'f', 2), cacheGroup);
    cacheSizeLabel->setStyleSheet("color: #ffffff;");
    cacheLayout->addWidget(cacheSizeLabel);

    clearCacheBtn = new QPushButton("Clear Thumbnail Cache", cacheGroup);
    clearCacheBtn->setStyleSheet(
        "QPushButton { background-color: #d73a49; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b52a3a; }"
    );
    connect(clearCacheBtn, &QPushButton::clicked, this, &SettingsDialog::onClearCache);
    cacheLayout->addWidget(clearCacheBtn);

    layout->addWidget(cacheGroup);

    // Database management
    QGroupBox* dbGroup = new QGroupBox("Database", cacheTab);
    dbGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid #333; padding: 10px; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* dbLayout = new QVBoxLayout(dbGroup);

    QPushButton* exportDbBtn = new QPushButton("Export Database", dbGroup);
    exportDbBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    connect(exportDbBtn, &QPushButton::clicked, this, &SettingsDialog::onExportDatabase);
    dbLayout->addWidget(exportDbBtn);

    QPushButton* importDbBtn = new QPushButton("Import Database", dbGroup);
    importDbBtn->setStyleSheet(
        "QPushButton { background-color: #58a6ff; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8fd9; }"
    );
    connect(importDbBtn, &QPushButton::clicked, this, &SettingsDialog::onImportDatabase);
    dbLayout->addWidget(importDbBtn);

    QPushButton* clearDbBtn = new QPushButton("Clear Database (Danger!)", dbGroup);
    clearDbBtn->setStyleSheet(
        "QPushButton { background-color: #d73a49; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #b52a3a; }"
    );
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
    viewGroup->setStyleSheet("QGroupBox { color: #ffffff; border: 1px solid #333; padding: 10px; margin-top: 10px; } QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }");
    QVBoxLayout* viewLayout = new QVBoxLayout(viewGroup);

    QLabel* thumbnailLabel = new QLabel("Thumbnail Size:", viewGroup);
    thumbnailLabel->setStyleSheet("color: #ffffff;");
    viewLayout->addWidget(thumbnailLabel);

    thumbnailSizeSpin = new QSpinBox(viewGroup);
    thumbnailSizeSpin->setRange(64, 512);
    thumbnailSizeSpin->setValue(200);
    thumbnailSizeSpin->setSuffix(" px");
    thumbnailSizeSpin->setStyleSheet(
        "QSpinBox { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; padding: 6px; border-radius: 4px; }"
    );
    viewLayout->addWidget(thumbnailSizeSpin);

    showFileExtensionsCheck = new QCheckBox("Show file extensions", viewGroup);
    showFileExtensionsCheck->setChecked(true);
    showFileExtensionsCheck->setStyleSheet("QCheckBox { color: #ffffff; }");
    viewLayout->addWidget(showFileExtensionsCheck);

    showSequenceOverlayCheck = new QCheckBox("Show sequence overlay badges", viewGroup);
    showSequenceOverlayCheck->setChecked(true);
    showSequenceOverlayCheck->setStyleSheet("QCheckBox { color: #ffffff; }");
    viewLayout->addWidget(showSequenceOverlayCheck);

    layout->addWidget(viewGroup);
    layout->addStretch();

    tabWidget->addTab(viewTab, "View");
}

void SettingsDialog::setupShortcutsTab()
{
    QWidget* shortcutsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(shortcutsTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(10);

    QLabel* title = new QLabel("File Manager Keyboard Shortcuts", shortcutsTab);
    title->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff;");
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
    fmShortcutsTable->setStyleSheet("QTableWidget { background-color:#1a1a1a; color:#fff; border:1px solid #333; } QHeaderView::section { background:#222; color:#fff; }");

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

void SettingsDialog::setupAboutTab()
{
    QWidget* aboutTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(aboutTab);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(15);

    QLabel* appName = new QLabel("KAsset Manager", aboutTab);
    appName->setStyleSheet("font-size: 18px; font-weight: bold; color: #ffffff;");
    appName->setAlignment(Qt::AlignCenter);
    layout->addWidget(appName);

    versionLabel = new QLabel("Version 0.1.0", aboutTab);
    versionLabel->setStyleSheet("color: #999; font-size: 12px;");
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);

    qtVersionLabel = new QLabel(QString("Built with Qt %1").arg(QT_VERSION_STR), aboutTab);
    qtVersionLabel->setStyleSheet("color: #999; font-size: 12px;");
    qtVersionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(qtVersionLabel);

    QLabel* licensesTitle = new QLabel("Third-Party Licenses", aboutTab);
    licensesTitle->setStyleSheet("font-size: 14px; font-weight: bold; color: #ffffff; margin-top: 20px;");
    layout->addWidget(licensesTitle);

    licensesText = new QTextEdit(aboutTab);
    licensesText->setReadOnly(true);
    licensesText->setStyleSheet(
        "QTextEdit { background-color: #1a1a1a; color: #ffffff; border: 1px solid #333; padding: 10px; }"
    );
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

void SettingsDialog::onClearCache()
{
    QMessageBox::StandardButton reply = QMessageBox::question(
        this,
        "Clear Cache",
        "Are you sure you want to clear the thumbnail cache? This will delete all cached thumbnails.",
        QMessageBox::Yes | QMessageBox::No
    );

    if (reply == QMessageBox::Yes) {
        ThumbnailGenerator::instance().clearCache();
        QMessageBox::information(this, "Cache Cleared", "Thumbnail cache has been cleared successfully.");

        // Update cache size label
        cacheSizeLabel->setText("Current cache size: 0.00 MB");
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

void SettingsDialog::saveSettings()
{

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



