#ifndef EVERYTHING_SEARCH_DIALOG_H
#define EVERYTHING_SEARCH_DIALOG_H

#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QLabel>
#include <QComboBox>
#include <QCheckBox>
#include <QVector>
#include <QString>
#include "everything_search.h"

class EverythingSearchDialog : public QDialog {
    Q_OBJECT

public:
    enum Mode {
        AssetManagerMode,  // Show import status, allow bulk import
        FileManagerMode    // Just show results, allow opening/selecting
    };

    explicit EverythingSearchDialog(Mode mode, QWidget* parent = nullptr);
    ~EverythingSearchDialog() = default;

    // Get selected file paths (for File Manager mode)
    QStringList getSelectedPaths() const;

signals:
    void importRequested(const QStringList& paths);

private slots:
    void onSearchClicked();
    void onSearchTextChanged(const QString& text);
    void onFilterChanged(int index);
    void onImportSelected();
    void onOpenLocation();
    void onResultDoubleClicked(int row, int column);

private:
    void setupUI();
    void performSearch();
    void updateResults(const QVector<EverythingResult>& results);
    void checkImportStatus(QVector<EverythingResult>& results);
    QString formatFileSize(qint64 bytes) const;
    QStringList getSelectedFilePaths() const;

    // UI components
    QVBoxLayout* m_mainLayout;
    
    // Search section
    QLineEdit* m_searchEdit;
    QPushButton* m_searchButton;
    QComboBox* m_filterCombo;
    QCheckBox* m_matchCaseCheck;
    
    // Results section
    QTableWidget* m_resultsTable;
    QLabel* m_statusLabel;
    
    // Action buttons
    QPushButton* m_importButton;      // Asset Manager mode only
    QPushButton* m_openLocationButton;
    QPushButton* m_selectButton;      // File Manager mode only
    QPushButton* m_closeButton;
    
    // Data
    Mode m_mode;
    QVector<EverythingResult> m_currentResults;
};

#endif // EVERYTHING_SEARCH_DIALOG_H

