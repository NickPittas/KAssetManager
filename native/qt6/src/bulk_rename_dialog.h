#ifndef BULK_RENAME_DIALOG_H
#define BULK_RENAME_DIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QTableWidget>
#include <QGroupBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QComboBox>
#include <QVector>
#include <QString>

struct RenamePreviewItem {
    int assetId;
    QString originalName;
    QString newName;
    QString fullPath;
    bool hasConflict;
    QString conflictReason;
};

class BulkRenameDialog : public QDialog {
    Q_OBJECT

public:
    explicit BulkRenameDialog(const QVector<int>& assetIds, QWidget* parent = nullptr);
    ~BulkRenameDialog() = default;

private slots:
    void onPatternChanged();
    void onPreviewUpdated();
    void onApplyRename();
    void onInsertToken(const QString& token);

private:
    void setupUI();
    void loadAssets();
    void updatePreview();
    QString applyPattern(const QString& originalName, int index) const;
    bool validateRename();
    bool performRename();
    void rollbackRename(const QVector<QPair<int, QString>>& renamedAssets);
    
    // Pattern tokens
    QString replaceTokens(const QString& pattern, const QString& originalName, int index) const;
    
    // UI components
    QVBoxLayout* m_mainLayout;
    
    // Pattern input section
    QGroupBox* m_patternGroup;
    QLineEdit* m_patternEdit;
    QLabel* m_patternHelpLabel;
    QPushButton* m_insertCounterButton;
    QPushButton* m_insertOriginalButton;
    QPushButton* m_insertDateButton;
    
    // Options section
    QGroupBox* m_optionsGroup;
    QCheckBox* m_preserveExtensionCheck;
    QCheckBox* m_updateDatabaseCheck;
    QCheckBox* m_updateFilesystemCheck;
    QSpinBox* m_startNumberSpin;
    QSpinBox* m_paddingSpin;
    
    // Preview section
    QGroupBox* m_previewGroup;
    QTableWidget* m_previewTable;
    QLabel* m_statusLabel;
    
    // Action buttons
    QPushButton* m_applyButton;
    QPushButton* m_cancelButton;
    
    // Data
    QVector<int> m_assetIds;
    QVector<RenamePreviewItem> m_previewItems;
    bool m_hasConflicts;
};

#endif // BULK_RENAME_DIALOG_H

