#include "everything_search_dialog.h"
#include "db.h"
#include <QHeaderView>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QDateTime>
#include <QApplication>
#include <QSqlQuery>

EverythingSearchDialog::EverythingSearchDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
{
    QString title = (mode == AssetManagerMode) ? "Everything Search - Asset Manager" : "Everything Search - File Manager";
    setWindowTitle(title);
    resize(1000, 600);
    
    setupUI();
    
    // Check if Everything is available
    if (!EverythingSearch::instance().isAvailable()) {
        QMessageBox::warning(this, "Everything Not Available",
            "Everything search engine is not available.\n\n"
            "Please ensure:\n"
            "1. Everything is installed (https://www.voidtools.com/)\n"
            "2. Everything service is running\n"
            "3. Everything64.dll is in the application directory");
        m_searchEdit->setEnabled(false);
        m_searchButton->setEnabled(false);
    }
}

QStringList EverythingSearchDialog::getSelectedPaths() const {
    return getSelectedFilePaths();
}

void EverythingSearchDialog::setupUI() {
    m_mainLayout = new QVBoxLayout(this);
    m_mainLayout->setSpacing(12);
    
    // Search section
    QHBoxLayout* searchLayout = new QHBoxLayout();
    
    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText("Enter search query (e.g., *.jpg, project_*, render_v*)");
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &EverythingSearchDialog::onSearchClicked);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &EverythingSearchDialog::onSearchTextChanged);
    searchLayout->addWidget(m_searchEdit, 1);
    
    m_searchButton = new QPushButton("Search", this);
    m_searchButton->setEnabled(false);
    connect(m_searchButton, &QPushButton::clicked, this, &EverythingSearchDialog::onSearchClicked);
    searchLayout->addWidget(m_searchButton);
    
    m_mainLayout->addLayout(searchLayout);
    
    // Filter section
    QHBoxLayout* filterLayout = new QHBoxLayout();
    
    QLabel* filterLabel = new QLabel("Filter:", this);
    filterLayout->addWidget(filterLabel);
    
    m_filterCombo = new QComboBox(this);
    m_filterCombo->addItem("All Files", "");
    m_filterCombo->addItem("Images", "ext:jpg;jpeg;png;tif;tiff;exr;iff;psd;bmp;gif");
    m_filterCombo->addItem("Videos", "ext:mov;mp4;avi;mkv;webm;m4v");
    m_filterCombo->addItem("Audio", "ext:mp3;wav;aac;flac;ogg;m4a");
    m_filterCombo->addItem("Documents", "ext:pdf;doc;docx;txt;md");
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &EverythingSearchDialog::onFilterChanged);
    filterLayout->addWidget(m_filterCombo);
    
    m_matchCaseCheck = new QCheckBox("Match Case", this);
    filterLayout->addWidget(m_matchCaseCheck);
    
    filterLayout->addStretch();
    m_mainLayout->addLayout(filterLayout);
    
    // Results table
    m_resultsTable = new QTableWidget(this);
    m_resultsTable->setColumnCount(m_mode == AssetManagerMode ? 6 : 5);
    
    QStringList headers;
    if (m_mode == AssetManagerMode) {
        headers << "Status" << "Name" << "Directory" << "Size" << "Modified" << "Type";
        m_resultsTable->setColumnWidth(0, 80);
    } else {
        headers << "Name" << "Directory" << "Size" << "Modified" << "Type";
    }
    
    m_resultsTable->setHorizontalHeaderLabels(headers);
    m_resultsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_resultsTable->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_resultsTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_resultsTable->setSortingEnabled(true);
    m_resultsTable->setAlternatingRowColors(true);
    m_resultsTable->horizontalHeader()->setStretchLastSection(false);
    m_resultsTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    
    // Set column widths
    int nameCol = (m_mode == AssetManagerMode) ? 1 : 0;
    int dirCol = nameCol + 1;
    int sizeCol = dirCol + 1;
    int modCol = sizeCol + 1;
    int typeCol = modCol + 1;
    
    m_resultsTable->setColumnWidth(nameCol, 250);
    m_resultsTable->setColumnWidth(dirCol, 350);
    m_resultsTable->setColumnWidth(sizeCol, 100);
    m_resultsTable->setColumnWidth(modCol, 150);
    m_resultsTable->setColumnWidth(typeCol, 80);
    
    connect(m_resultsTable, &QTableWidget::cellDoubleClicked, this, &EverythingSearchDialog::onResultDoubleClicked);
    
    m_mainLayout->addWidget(m_resultsTable);
    
    // Status label
    m_statusLabel = new QLabel("Enter a search query to begin", this);
    m_statusLabel->setStyleSheet("color: #666; font-size: 10pt;");
    m_mainLayout->addWidget(m_statusLabel);
    
    // Action buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    if (m_mode == AssetManagerMode) {
        m_importButton = new QPushButton("Import Selected", this);
        m_importButton->setEnabled(false);
        connect(m_importButton, &QPushButton::clicked, this, &EverythingSearchDialog::onImportSelected);
        buttonLayout->addWidget(m_importButton);
    } else {
        m_selectButton = new QPushButton("Select Files", this);
        m_selectButton->setEnabled(false);
        connect(m_selectButton, &QPushButton::clicked, this, &QDialog::accept);
        buttonLayout->addWidget(m_selectButton);
    }
    
    m_openLocationButton = new QPushButton("Open Location", this);
    m_openLocationButton->setEnabled(false);
    connect(m_openLocationButton, &QPushButton::clicked, this, &EverythingSearchDialog::onOpenLocation);
    buttonLayout->addWidget(m_openLocationButton);
    
    m_closeButton = new QPushButton("Close", this);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    buttonLayout->addWidget(m_closeButton);
    
    m_mainLayout->addLayout(buttonLayout);
    
    // Enable/disable buttons based on selection
    connect(m_resultsTable, &QTableWidget::itemSelectionChanged, this, [this]() {
        bool hasSelection = !m_resultsTable->selectedItems().isEmpty();
        m_openLocationButton->setEnabled(hasSelection);
        if (m_mode == AssetManagerMode) {
            m_importButton->setEnabled(hasSelection);
        } else {
            m_selectButton->setEnabled(hasSelection);
        }
    });
}

void EverythingSearchDialog::onSearchTextChanged(const QString& text) {
    m_searchButton->setEnabled(!text.trimmed().isEmpty());
}

void EverythingSearchDialog::onFilterChanged(int /*index*/) {
    // Auto-search if we already have a query
    if (!m_searchEdit->text().trimmed().isEmpty()) {
        performSearch();
    }
}

void EverythingSearchDialog::onSearchClicked() {
    performSearch();
}

void EverythingSearchDialog::performSearch() {
    QString query = m_searchEdit->text().trimmed();
    if (query.isEmpty()) {
        return;
    }
    
    m_statusLabel->setText("Searching...");
    m_resultsTable->setRowCount(0);
    m_currentResults.clear();
    
    QApplication::setOverrideCursor(Qt::WaitCursor);
    
    // Get filter
    QString filter = m_filterCombo->currentData().toString();
    
    // Perform search
    QVector<EverythingResult> results;
    if (filter.isEmpty()) {
        results = EverythingSearch::instance().search(query, 10000);
    } else {
        results = EverythingSearch::instance().searchWithFilter(query, filter, 10000);
    }
    
    // Check import status for Asset Manager mode
    if (m_mode == AssetManagerMode) {
        checkImportStatus(results);
    }
    
    m_currentResults = results;
    updateResults(results);
    
    QApplication::restoreOverrideCursor();
    
    m_statusLabel->setText(QString("Found %1 result(s)").arg(results.size()));
}

void EverythingSearchDialog::checkImportStatus(QVector<EverythingResult>& results) {
    // Query database to check which files are already imported
    QSet<QString> importedPaths;

    QSqlQuery query(DB::instance().database());
    query.prepare("SELECT file_path FROM assets");
    if (query.exec()) {
        while (query.next()) {
            importedPaths.insert(query.value(0).toString());
        }
    }

    // Mark results as imported if they exist in database
    for (auto& result : results) {
        result.isImported = importedPaths.contains(result.fullPath);
    }
}

void EverythingSearchDialog::updateResults(const QVector<EverythingResult>& results) {
    m_resultsTable->setRowCount(results.size());
    m_resultsTable->setSortingEnabled(false);
    
    for (int i = 0; i < results.size(); ++i) {
        const auto& result = results[i];
        int col = 0;
        
        // Status column (Asset Manager mode only)
        if (m_mode == AssetManagerMode) {
            QTableWidgetItem* statusItem = new QTableWidgetItem(result.isImported ? "Imported" : "Not Imported");
            statusItem->setForeground(result.isImported ? QColor(100, 200, 100) : QColor(200, 200, 200));
            m_resultsTable->setItem(i, col++, statusItem);
        }
        
        // Name
        QTableWidgetItem* nameItem = new QTableWidgetItem(result.fileName);
        m_resultsTable->setItem(i, col++, nameItem);
        
        // Directory
        QTableWidgetItem* dirItem = new QTableWidgetItem(result.directory);
        dirItem->setToolTip(result.directory);
        m_resultsTable->setItem(i, col++, dirItem);
        
        // Size
        QString sizeStr = result.isFolder ? "<DIR>" : formatFileSize(result.size);
        QTableWidgetItem* sizeItem = new QTableWidgetItem(sizeStr);
        sizeItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_resultsTable->setItem(i, col++, sizeItem);
        
        // Modified
        QString modStr = result.dateModified.toString("yyyy-MM-dd HH:mm");
        QTableWidgetItem* modItem = new QTableWidgetItem(modStr);
        m_resultsTable->setItem(i, col++, modItem);
        
        // Type
        QString typeStr = result.isFolder ? "Folder" : "File";
        QTableWidgetItem* typeItem = new QTableWidgetItem(typeStr);
        m_resultsTable->setItem(i, col++, typeItem);
    }
    
    m_resultsTable->setSortingEnabled(true);
}

QString EverythingSearchDialog::formatFileSize(qint64 bytes) const {
    if (bytes < 1024) return QString("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QString("%1 KB").arg(bytes / 1024);
    if (bytes < 1024 * 1024 * 1024) return QString("%1 MB").arg(bytes / (1024 * 1024));
    return QString("%1 GB").arg(bytes / (1024 * 1024 * 1024));
}

QStringList EverythingSearchDialog::getSelectedFilePaths() const {
    QStringList paths;
    QSet<int> selectedRows;
    
    for (const auto* item : m_resultsTable->selectedItems()) {
        selectedRows.insert(item->row());
    }
    
    for (int row : selectedRows) {
        if (row >= 0 && row < m_currentResults.size()) {
            paths.append(m_currentResults[row].fullPath);
        }
    }
    
    return paths;
}

void EverythingSearchDialog::onImportSelected() {
    QStringList paths = getSelectedFilePaths();
    if (paths.isEmpty()) {
        return;
    }
    
    // Filter out already imported files
    QStringList notImported;
    for (const QString& path : paths) {
        bool imported = false;
        for (const auto& result : m_currentResults) {
            if (result.fullPath == path && result.isImported) {
                imported = true;
                break;
            }
        }
        if (!imported) {
            notImported.append(path);
        }
    }
    
    if (notImported.isEmpty()) {
        QMessageBox::information(this, "Already Imported",
            "All selected files are already imported into the asset library.");
        return;
    }
    
    emit importRequested(notImported);
    accept();
}

void EverythingSearchDialog::onOpenLocation() {
    QStringList paths = getSelectedFilePaths();
    if (paths.isEmpty()) {
        return;
    }
    
    // Open first selected file's location
    QString path = paths.first();
    QFileInfo fi(path);
    
    if (fi.exists()) {
        QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
    }
}

void EverythingSearchDialog::onResultDoubleClicked(int row, int /*column*/) {
    if (row >= 0 && row < m_currentResults.size()) {
        const auto& result = m_currentResults[row];
        QFileInfo fi(result.fullPath);
        
        if (fi.exists()) {
            if (result.isFolder) {
                // Open folder
                QDesktopServices::openUrl(QUrl::fromLocalFile(result.fullPath));
            } else {
                // Open file location
                QDesktopServices::openUrl(QUrl::fromLocalFile(fi.absolutePath()));
            }
        }
    }
}

