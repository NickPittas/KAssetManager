#include "log_viewer_widget.h"
#include "log_manager.h"
#include <QScrollBar>

LogViewerWidget::LogViewerWidget(QWidget* parent)
    : QWidget(parent)
    , m_currentFilter(LogLevel::Info)
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    // Toolbar
    QWidget* toolbar = new QWidget(this);
    toolbar->setStyleSheet("QWidget { background-color: #1a1a1a; border-bottom: 1px solid #333; }");
    QHBoxLayout* toolbarLayout = new QHBoxLayout(toolbar);
    toolbarLayout->setContentsMargins(8, 4, 8, 4);
    toolbarLayout->setSpacing(8);

    QLabel* titleLabel = new QLabel("Application Log", toolbar);
    titleLabel->setStyleSheet("color: #ffffff; font-size: 12px; font-weight: bold;");
    toolbarLayout->addWidget(titleLabel);

    toolbarLayout->addStretch();

    // Filter dropdown
    QLabel* filterLabel = new QLabel("Level:", toolbar);
    filterLabel->setStyleSheet("color: #ffffff; font-size: 11px;");
    toolbarLayout->addWidget(filterLabel);

    m_filterCombo = new QComboBox(toolbar);
    m_filterCombo->addItem("All", LogLevel::All);
    m_filterCombo->addItem("Debug+", LogLevel::Debug);
    m_filterCombo->addItem("Info+", LogLevel::Info);
    m_filterCombo->addItem("Warning+", LogLevel::Warning);
    m_filterCombo->addItem("Error+", LogLevel::Error);
    m_filterCombo->setCurrentIndex(2); // Default to Info+
    m_filterCombo->setStyleSheet(
        "QComboBox { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; border-radius: 3px; padding: 2px 8px; font-size: 11px; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox::down-arrow { image: none; border: none; }"
        "QComboBox QAbstractItemView { background-color: #2a2a2a; color: #ffffff; selection-background-color: #58a6ff; }"
    );
    connect(m_filterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &LogViewerWidget::onFilterChanged);
    toolbarLayout->addWidget(m_filterCombo);

    // Clear button
    m_clearButton = new QPushButton("Clear", toolbar);
    m_clearButton->setFixedSize(60, 24);
    m_clearButton->setStyleSheet(
        "QPushButton { background-color: #2a2a2a; color: #ffffff; border: 1px solid #333; border-radius: 3px; font-size: 11px; }"
        "QPushButton:hover { background-color: #333; }"
    );
    connect(m_clearButton, &QPushButton::clicked, this, &LogViewerWidget::onClearLogs);
    toolbarLayout->addWidget(m_clearButton);

    mainLayout->addWidget(toolbar);

    // Log text area
    m_logTextEdit = new QTextEdit(this);
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setStyleSheet(
        "QTextEdit { background-color: #0a0a0a; color: #cccccc; border: none; font-family: 'Consolas', 'Courier New', monospace; font-size: 10px; }"
    );
    mainLayout->addWidget(m_logTextEdit);

    // Connect to log manager
    connect(&LogManager::instance(), &LogManager::logAdded, this, &LogViewerWidget::onLogAdded);

    // Load existing logs
    QStringList existingLogs = LogManager::instance().logs();
    for (const QString& log : existingLogs) {
        addLogToView(log);
    }
}

void LogViewerWidget::onLogAdded(const QString& message) {
    addLogToView(message);
}

void LogViewerWidget::addLogToView(const QString& message) {
    if (!shouldShowLog(message)) {
        return;
    }

    QString colorized = colorizeLog(message);
    
    // Auto-scroll to bottom
    QScrollBar* scrollBar = m_logTextEdit->verticalScrollBar();
    bool wasAtBottom = scrollBar->value() == scrollBar->maximum();
    
    m_logTextEdit->append(colorized);
    
    if (wasAtBottom) {
        scrollBar->setValue(scrollBar->maximum());
    }
}

bool LogViewerWidget::shouldShowLog(const QString& message) const {
    if (m_currentFilter == LogLevel::All) {
        return true;
    }

    // Extract log level from message format: [timestamp] [LEVEL] message
    if (message.contains("[DEBUG]")) {
        return m_currentFilter <= LogLevel::Debug;
    } else if (message.contains("[INFO]")) {
        return m_currentFilter <= LogLevel::Info;
    } else if (message.contains("[WARN]")) {
        return m_currentFilter <= LogLevel::Warning;
    } else if (message.contains("[ERROR]")) {
        return m_currentFilter <= LogLevel::Error;
    } else if (message.contains("[FATAL]") || message.contains("[CRITICAL]")) {
        return m_currentFilter <= LogLevel::Critical;
    }

    // If no level found, show it
    return true;
}

QString LogViewerWidget::colorizeLog(const QString& message) const {
    QString result = message;

    // Color-code based on log level
    if (message.contains("[DEBUG]")) {
        result = QString("<span style='color: #888888;'>%1</span>").arg(message.toHtmlEscaped());
    } else if (message.contains("[INFO]")) {
        result = QString("<span style='color: #58a6ff;'>%1</span>").arg(message.toHtmlEscaped());
    } else if (message.contains("[WARN]")) {
        result = QString("<span style='color: #f0ad4e;'>%1</span>").arg(message.toHtmlEscaped());
    } else if (message.contains("[ERROR]")) {
        result = QString("<span style='color: #ff4444;'>%1</span>").arg(message.toHtmlEscaped());
    } else if (message.contains("[FATAL]") || message.contains("[CRITICAL]")) {
        result = QString("<span style='color: #ff0000; font-weight: bold;'>%1</span>").arg(message.toHtmlEscaped());
    } else {
        result = QString("<span style='color: #cccccc;'>%1</span>").arg(message.toHtmlEscaped());
    }

    return result;
}

void LogViewerWidget::onFilterChanged(int index) {
    m_currentFilter = static_cast<LogLevel>(m_filterCombo->itemData(index).toInt());
    
    // Reload all logs with new filter
    m_logTextEdit->clear();
    QStringList existingLogs = LogManager::instance().logs();
    for (const QString& log : existingLogs) {
        addLogToView(log);
    }
}

void LogViewerWidget::onClearLogs() {
    m_logTextEdit->clear();
    LogManager::instance().clear();
}

