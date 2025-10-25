#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QSplitter>
#include <QTreeView>
#include <QListView>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QScrollArea>
#include <QMenu>
#include <QAction>
#include <QSet>

class VirtualFolderTreeModel;
class AssetsModel;
class TagsModel;
class PreviewOverlay;
class ImportProgressDialog;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void onFolderSelected(const QModelIndex &index);
    void onAssetSelectionChanged();
    void onAssetDoubleClicked(const QModelIndex &index);
    void onAssetContextMenu(const QPoint &pos);
    void onFolderContextMenu(const QPoint &pos);
    void onEmptySpaceContextMenu(const QPoint &pos);

    void showPreview(int index);
    void closePreview();
    void changePreview(int delta);

    void applyFilters();
    void clearFilters();
    void onSearchTextChanged(const QString &text);

    void onCreateTag();
    void onApplyTags();
    void onFilterByTags();
    void onTagContextMenu(const QPoint &pos);
    void updateTagButtonStates();

    void importFiles(const QStringList &filePaths);
    void onImportProgress(int current, int total);
    void onImportFileChanged(const QString& fileName);
    void onImportFolderChanged(const QString& folderName);
    void onImportComplete();
    void onThumbnailProgress(int current, int total);
    void onRatingChanged(int rating);

private:
    void setupUi();
    void setupConnections();
    void updateInfoPanel();
    void updateSelectionInfo();

    QSet<int> getSelectedAssetIds() const;
    int getAnchorIndex() const;
    void selectAsset(int assetId, int index, Qt::KeyboardModifiers modifiers);
    void selectSingle(int assetId, int index);
    void toggleSelection(int assetId, int index);
    void selectRange(int fromIndex, int toIndex);
    void clearSelection();

    // Folder tree expansion state management
    void saveFolderExpansionState();
    void restoreFolderExpansionState();
    QSet<int> expandedFolderIds;
    
    // UI Components
    QSplitter *mainSplitter;
    QSplitter *rightSplitter;
    
    // Left panel: Folder tree
    QTreeView *folderTreeView;
    VirtualFolderTreeModel *folderModel;
    
    // Center panel: Asset grid
    QListView *assetGridView;
    AssetsModel *assetsModel;
    
    // Right panel: Filters + Info
    QWidget *rightPanel;
    QWidget *filtersPanel;
    QWidget *infoPanel;

    // Importer
    class Importer *importer;
    
    // Filters
    QLineEdit *searchBox;
    QComboBox *ratingFilter;
    QListView *tagsListView;
    TagsModel *tagsModel;
    QPushButton *applyTagsBtn;
    QPushButton *filterByTagsBtn;
    QComboBox *tagFilterModeCombo;
    
    // Info panel labels
    QLabel *infoFileName;
    QLabel *infoFilePath;
    QLabel *infoFileSize;
    QLabel *infoFileType;
    QLabel *infoModified;
    QLabel *infoRatingLabel;
    class StarRatingWidget *infoRatingWidget;
    QLabel *infoTags;
    
    // Selection state
    QSet<int> selectedAssetIds;
    int anchorIndex;
    int currentAssetId;
    int previewIndex;
    
    // Preview overlay
    PreviewOverlay *previewOverlay;

    // Thumbnail generation progress
    QLabel *thumbnailProgressLabel;
    class QProgressBar *thumbnailProgressBar;

    // Import progress dialog
    ImportProgressDialog *importProgressDialog;
};

#endif // MAINWINDOW_H

