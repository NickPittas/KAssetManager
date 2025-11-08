#include "bulk_rename_dialog.h"
#include "db.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <QRegularExpression>
#include <QSet>

BulkRenameDialog::BulkRenameDialog(const QVector<int>& assetIds, QWidget* parent)
    : QDialog(parent)
    , m_fileManagerMode(false)
    , m_assetIds(assetIds)
    , m_hasConflicts(false)
{
    setWindowTitle(QString("Bulk Rename - %1 Asset(s)").arg(assetIds.size()));
    resize(900, 600);

    setupUI();
    loadAssets();
    updatePreview();
}

BulkRenameDialog::BulkRenameDialog(const QStringList& filePaths, QWidget* parent)
    : QDialog(parent)
    , m_fileManagerMode(true)
    , m_filePaths(filePaths)
    , m_hasConflicts(false)
{
    setWindowTitle(QString("Bulk Rename - %1 File(s)").arg(filePaths.size()));
    resize(900, 600);

    setupUI();
    loadFiles();
    updatePreview();
}

void BulkRenameDialog::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(12);

    // Pattern input section
    m_patternGroup = new QGroupBox("Rename Pattern", this);
    QVBoxLayout* patternLayout = new QVBoxLayout(m_patternGroup);

    QHBoxLayout* patternInputLayout = new QHBoxLayout();
    m_patternEdit = new QLineEdit(this);
    m_patternEdit->setPlaceholderText("Enter rename pattern (e.g., shot_{###} or {original}_v01)");
    connect(m_patternEdit, &QLineEdit::textChanged, this, &BulkRenameDialog::onPatternChanged);
    patternInputLayout->addWidget(m_patternEdit);

    patternLayout->addLayout(patternInputLayout);

    // Token buttons
    QHBoxLayout* tokenLayout = new QHBoxLayout();
    m_insertCounterButton = new QPushButton("{###}", this);
    m_insertCounterButton->setToolTip("Insert counter with padding");
    connect(m_insertCounterButton, &QPushButton::clicked, this, [this]() { onInsertToken("{###}"); });
    tokenLayout->addWidget(m_insertCounterButton);

    m_insertOriginalButton = new QPushButton("{original}", this);
    m_insertOriginalButton->setToolTip("Insert original filename");
    connect(m_insertOriginalButton, &QPushButton::clicked, this, [this]() { onInsertToken("{original}"); });
    tokenLayout->addWidget(m_insertOriginalButton);

    m_insertDateButton = new QPushButton("{date}", this);
    m_insertDateButton->setToolTip("Insert current date (YYYYMMDD)");
    connect(m_insertDateButton, &QPushButton::clicked, this, [this]() { onInsertToken("{date}"); });
    tokenLayout->addWidget(m_insertDateButton);

    tokenLayout->addStretch();
    patternLayout->addLayout(tokenLayout);

    m_patternHelpLabel = new QLabel(
        "Tokens: {###} = counter with padding, {original} = original name, {date} = YYYYMMDD, {ext} = extension",
        this
    );
    m_patternHelpLabel->setStyleSheet("color: #666; font-size: 10pt;");
    m_patternHelpLabel->setWordWrap(true);
    patternLayout->addWidget(m_patternHelpLabel);

    m_mainLayout->addWidget(m_patternGroup);

    // Options section
    m_optionsGroup = new QGroupBox("Options", this);
    QHBoxLayout* optionsLayout = new QHBoxLayout(m_optionsGroup);

    m_preserveExtensionCheck = new QCheckBox("Preserve Extension", this);
    m_preserveExtensionCheck->setChecked(true);
    m_preserveExtensionCheck->setToolTip("Keep original file extension");
    connect(m_preserveExtensionCheck, &QCheckBox::toggled, this, &BulkRenameDialog::onPatternChanged);
    optionsLayout->addWidget(m_preserveExtensionCheck);

    m_updateDatabaseCheck = new QCheckBox("Update Database", this);
    m_updateDatabaseCheck->setChecked(true);
    m_updateDatabaseCheck->setToolTip("Update asset names in database");
    m_updateDatabaseCheck->setVisible(!m_fileManagerMode);  // Hide in file manager mode
    optionsLayout->addWidget(m_updateDatabaseCheck);

    m_updateFilesystemCheck = new QCheckBox("Rename Files on Disk", this);
    m_updateFilesystemCheck->setChecked(true);
    m_updateFilesystemCheck->setToolTip("Physically rename files on filesystem");
    optionsLayout->addWidget(m_updateFilesystemCheck);

    optionsLayout->addSpacing(20);

    QLabel* startLabel = new QLabel("Start Number:", this);
    optionsLayout->addWidget(startLabel);
    m_startNumberSpin = new QSpinBox(this);
    m_startNumberSpin->setRange(0, 99999);
    m_startNumberSpin->setValue(1);
    connect(m_startNumberSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BulkRenameDialog::onPatternChanged);
    optionsLayout->addWidget(m_startNumberSpin);

    QLabel* paddingLabel = new QLabel("Padding:", this);
    optionsLayout->addWidget(paddingLabel);
    m_paddingSpin = new QSpinBox(this);
    m_paddingSpin->setRange(1, 10);
    m_paddingSpin->setValue(3);
    connect(m_paddingSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &BulkRenameDialog::onPatternChanged);
    optionsLayout->addWidget(m_paddingSpin);

    optionsLayout->addStretch();
    m_mainLayout->addWidget(m_optionsGroup);

    // Preview section
    m_previewGroup = new QGroupBox("Preview", this);
    QVBoxLayout* previewLayout = new QVBoxLayout(m_previewGroup);

    m_previewTable = new QTableWidget(this);
    m_previewTable->setColumnCount(3);
    m_previewTable->setHorizontalHeaderLabels({"Original Name", "New Name", "Status"});
    m_previewTable->horizontalHeader()->setStretchLastSection(false);
    m_previewTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_previewTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_previewTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_previewTable->setAlternatingRowColors(true);
    m_previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_previewTable->setSelectionBehavior(QAbstractItemView::SelectRows);

    previewLayout->addWidget(m_previewTable);

    m_statusLabel = new QLabel(this);
    m_statusLabel->setStyleSheet("color: #666;");
    previewLayout->addWidget(m_statusLabel);

    m_mainLayout->addWidget(m_previewGroup);

    // Action buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();

    m_cancelButton = new QPushButton("Cancel", this);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_cancelButton);

    m_applyButton = new QPushButton("Apply Rename", this);
    m_applyButton->setEnabled(false);
    connect(m_applyButton, &QPushButton::clicked, this, &BulkRenameDialog::onApplyRename);
    buttonLayout->addWidget(m_applyButton);

    m_mainLayout->addLayout(buttonLayout);
}

void BulkRenameDialog::loadAssets() {
    m_previewItems.clear();

    for (int assetId : m_assetIds) {
        QString filePath = DB::instance().getAssetFilePath(assetId);
        if (filePath.isEmpty()) continue;

        RenamePreviewItem item;
        item.assetId = assetId;
        item.originalName = QFileInfo(filePath).fileName();
        item.fullPath = filePath;
        item.hasConflict = false;

        m_previewItems.append(item);
    }
}

void BulkRenameDialog::loadFiles() {
    m_previewItems.clear();

    for (const QString& filePath : m_filePaths) {
        QFileInfo fi(filePath);
        if (!fi.exists()) continue;

        RenamePreviewItem item;
        item.assetId = -1;  // No asset ID in file manager mode
        item.originalName = fi.fileName();
        item.fullPath = filePath;
        item.hasConflict = false;

        m_previewItems.append(item);
    }
}

void BulkRenameDialog::onPatternChanged() {
    updatePreview();
}

void BulkRenameDialog::onPreviewUpdated() {
    updatePreview();
}

void BulkRenameDialog::onInsertToken(const QString& token) {
    m_patternEdit->insert(token);
}

void BulkRenameDialog::updatePreview() {
    QString pattern = m_patternEdit->text();

    if (pattern.isEmpty()) {
        m_statusLabel->setText("Enter a rename pattern to preview changes");
        m_applyButton->setEnabled(false);
        m_previewTable->setRowCount(0);
        return;
    }

    // Update preview items
    QSet<QString> newNames;
    m_hasConflicts = false;

    for (int i = 0; i < m_previewItems.size(); ++i) {
        RenamePreviewItem& item = m_previewItems[i];
        item.newName = applyPattern(item.originalName, i);

        // Check for conflicts
        if (newNames.contains(item.newName)) {
            item.hasConflict = true;
            item.conflictReason = "Duplicate name";
            m_hasConflicts = true;
        } else if (item.newName.isEmpty()) {
            item.hasConflict = true;
            item.conflictReason = "Empty name";
            m_hasConflicts = true;
        } else if (item.newName == item.originalName) {
            item.hasConflict = false;
            item.conflictReason = "No change";
        } else {
            item.hasConflict = false;
            item.conflictReason = "";
            newNames.insert(item.newName);
        }
    }

    // Update table
    m_previewTable->setRowCount(m_previewItems.size());

    for (int i = 0; i < m_previewItems.size(); ++i) {
        const RenamePreviewItem& item = m_previewItems[i];

        QTableWidgetItem* originalItem = new QTableWidgetItem(item.originalName);
        m_previewTable->setItem(i, 0, originalItem);

        QTableWidgetItem* newItem = new QTableWidgetItem(item.newName);
        if (item.hasConflict) {
            newItem->setForeground(QBrush(QColor("#d32f2f")));
        } else if (item.newName != item.originalName) {
            newItem->setForeground(QBrush(QColor("#388e3c")));
        }
        m_previewTable->setItem(i, 1, newItem);

        QString status = item.hasConflict ? item.conflictReason : (item.newName == item.originalName ? "No change" : "OK");
        QTableWidgetItem* statusItem = new QTableWidgetItem(status);
        if (item.hasConflict) {
            statusItem->setForeground(QBrush(QColor("#d32f2f")));
        }
        m_previewTable->setItem(i, 2, statusItem);
    }

    // Update status
    int changedCount = 0;
    for (const auto& item : m_previewItems) {
        if (!item.hasConflict && item.newName != item.originalName) {
            changedCount++;
        }
    }

    if (m_hasConflicts) {
        m_statusLabel->setText(QString("<span style='color:#d32f2f;'>⚠ Conflicts detected - fix issues before applying</span>"));
        m_applyButton->setEnabled(false);
    } else if (changedCount == 0) {
        m_statusLabel->setText("No changes to apply");
        m_applyButton->setEnabled(false);
    } else {
        m_statusLabel->setText(QString("<span style='color:#388e3c;'>✓ Ready to rename %1 asset(s)</span>").arg(changedCount));
        m_applyButton->setEnabled(true);
    }
}

QString BulkRenameDialog::applyPattern(const QString& originalName, int index) const {
    QString pattern = m_patternEdit->text();
    QString result = replaceTokens(pattern, originalName, index);

    // Handle extension
    if (m_preserveExtensionCheck->isChecked()) {
        QFileInfo info(originalName);
        QString ext = info.suffix();
        if (!ext.isEmpty() && !result.endsWith("." + ext)) {
            result += "." + ext;
        }
    }

    return result;
}

QString BulkRenameDialog::replaceTokens(const QString& pattern, const QString& originalName, int index) const {
    QString result = pattern;

    // {###} - Counter with padding
    QRegularExpression counterRegex(R"(\{(#+)\})");
    QRegularExpressionMatch match = counterRegex.match(result);
    if (match.hasMatch()) {
        int padding = match.captured(1).length();
        int number = m_startNumberSpin->value() + index;
        QString counterStr = QString("%1").arg(number, padding, 10, QChar('0'));
        result.replace(match.captured(0), counterStr);
    }

    // {original} - Original filename without extension
    QFileInfo info(originalName);
    QString baseName = info.completeBaseName();
    result.replace("{original}", baseName);

    // {date} - Current date
    QString dateStr = QDateTime::currentDateTime().toString("yyyyMMdd");
    result.replace("{date}", dateStr);

    // {ext} - Extension
    QString ext = info.suffix();
    result.replace("{ext}", ext);

    return result;
}

bool BulkRenameDialog::validateRename() {
    if (m_hasConflicts) {
        QMessageBox::warning(this, "Validation Failed",
            "Cannot proceed with rename: conflicts detected.\n"
            "Please fix duplicate names or empty names.");
        return false;
    }

    int changedCount = 0;
    for (const auto& item : m_previewItems) {
        if (item.newName != item.originalName) {
            changedCount++;
        }
    }

    if (changedCount == 0) {
        QMessageBox::information(this, "No Changes", "No assets will be renamed.");
        return false;
    }

    return true;
}

void BulkRenameDialog::onApplyRename() {
    if (!validateRename()) {
        return;
    }

    // Count changes
    int changedCount = 0;
    for (const auto& item : m_previewItems) {
        if (item.newName != item.originalName) {
            changedCount++;
        }
    }

    // Confirm with user
    QString itemType = m_fileManagerMode ? "file(s)" : "asset(s)";
    QString message = QString("Rename %1 %2?\n\n").arg(changedCount).arg(itemType);
    if (m_updateFilesystemCheck->isChecked()) {
        message += "Files will be physically renamed on disk.\n";
    }
    if (!m_fileManagerMode && m_updateDatabaseCheck->isChecked()) {
        message += "Database records will be updated.\n";
    }
    message += "\nFilesystem changes are permanent.";

    QMessageBox::StandardButton reply = QMessageBox::question(this, "Confirm Bulk Rename",
        message, QMessageBox::Yes | QMessageBox::No);

    if (reply != QMessageBox::Yes) {
        return;
    }

    // Perform rename
    bool success = m_fileManagerMode ? performFileRename() : performRename();
    if (success) {
        QMessageBox::information(this, "Success",
            QString("Successfully renamed %1 %2").arg(changedCount).arg(itemType));
        accept();
    }
}

bool BulkRenameDialog::performRename() {
    QVector<QPair<int, QString>> renamedAssets; // Track for rollback
    bool updateDb = m_updateDatabaseCheck->isChecked();
    bool updateFs = m_updateFilesystemCheck->isChecked();

    for (const auto& item : m_previewItems) {
        if (item.newName == item.originalName) {
            continue; // Skip unchanged
        }
        // Validate that newName does not traverse directories or include separators
        if (item.newName == "." || item.newName == ".." || item.newName.contains('/') || item.newName.contains('\\')) {
            QMessageBox::critical(this, "Invalid Name",
                QString("The new name '%1' is invalid. It must not contain '/' or '\\' or be '.'/'..'.")
                    .arg(item.newName));
            rollbackRename(renamedAssets);
            return false;
        }


        QFileInfo originalInfo(item.fullPath);
        QString newPath = originalInfo.absolutePath() + "/" + item.newName;

        // Rename on filesystem
        if (updateFs) {
            QFile file(item.fullPath);
            if (!file.rename(newPath)) {
                QMessageBox::critical(this, "Rename Failed",
                    QString("Failed to rename file:\n%1\n\nError: %2\n\nRolling back changes...")
                        .arg(item.fullPath).arg(file.errorString()));
                rollbackRename(renamedAssets);
                return false;
            }
        }

        // Update database
        if (updateDb) {
            if (!DB::instance().updateAssetPath(item.assetId, newPath)) {
                QMessageBox::critical(this, "Database Update Failed",
                    QString("Failed to update database for asset ID %1\n\nRolling back changes...")
                        .arg(item.assetId));
                rollbackRename(renamedAssets);
                return false;
            }
        }

        renamedAssets.append({item.assetId, item.fullPath});
    }

    return true;
}

bool BulkRenameDialog::performFileRename() {
    QVector<QPair<QString, QString>> renamedFiles; // Track for potential rollback (old, new)

    for (const auto& item : m_previewItems) {
        if (item.newName == item.originalName) {
            continue; // Skip unchanged
        }
        // Validate that newName does not traverse directories or include separators
        if (item.newName == "." || item.newName == ".." || item.newName.contains('/') || item.newName.contains('\\')) {
            QMessageBox::critical(this, "Invalid Name",
                QString("The new name '%1' is invalid. It must not contain '/' or '\\' or be '.'/'..'.")
                    .arg(item.newName));
            // Rollback previous renames
            for (const auto& pair : renamedFiles) {
                QFile rollbackFile(pair.second);
                rollbackFile.rename(pair.first);
            }
            return false;
        }


        QFileInfo originalInfo(item.fullPath);
        QString newPath = originalInfo.dir().filePath(item.newName);

        // Rename file on filesystem
        if (m_updateFilesystemCheck->isChecked()) {
            QFile file(item.fullPath);
            if (!file.rename(newPath)) {
                QMessageBox::critical(this, "Rename Failed",
                    QString("Failed to rename:\n%1\nto:\n%2\n\nError: %3")
                        .arg(item.fullPath)
                        .arg(newPath)
                        .arg(file.errorString()));
        // Validate that newName does not traverse directories or include separators
        if (item.newName == "." || item.newName == ".." || item.newName.contains('/') || item.newName.contains('\\')) {
            QMessageBox::critical(this, "Invalid Name",
                QString("The new name '%1' is invalid. It must not contain '/' or '\\' or be '.'/'..'.")
                    .arg(item.newName));
            // Rollback previous renames
            for (const auto& pair : renamedFiles) {
                QFile rollbackFile(pair.second);
                rollbackFile.rename(pair.first);
            }
            return false;
        }


                // Rollback previous renames
                for (const auto& pair : renamedFiles) {
                    QFile rollbackFile(pair.second);
                    rollbackFile.rename(pair.first);
                }
                return false;
            }
            renamedFiles.append(qMakePair(item.fullPath, newPath));
        }
    }

    return true;
}

void BulkRenameDialog::rollbackRename(const QVector<QPair<int, QString>>& renamedAssets) {
    // Attempt to rollback filesystem changes
    for (const auto& pair : renamedAssets) {
        int assetId = pair.first;
        QString originalPath = pair.second;

        // Get current path from database
        QString currentPath = DB::instance().getAssetFilePath(assetId);
        if (currentPath.isEmpty()) continue;

        // Rename back
        if (m_updateFilesystemCheck->isChecked()) {
            QFile file(currentPath);
            file.rename(originalPath);
        }

        // Restore database
        if (m_updateDatabaseCheck->isChecked()) {
            DB::instance().updateAssetPath(assetId, originalPath);
        }
    }
}


