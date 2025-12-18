#include "theme_manager.h"
#include <QSettings>
#include <QApplication>
#include <QPalette>

ThemeManager& ThemeManager::instance()
{
    static ThemeManager instance;
    return instance;
}

ThemeManager::ThemeManager()
    : m_currentTheme(Dark)
{
}

void ThemeManager::setTheme(Theme theme)
{
    if (m_currentTheme != theme) {
        m_currentTheme = theme;
        applyTheme();
        emit themeChanged();
    }
}

void ThemeManager::loadTheme()
{
    QSettings s("AugmentCode", "KAssetManager");
    int themeIndex = s.value("Appearance/Theme", 0).toInt();
    m_currentTheme = (themeIndex == 1) ? Light : Dark;
    applyTheme();
}

void ThemeManager::saveTheme()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("Appearance/Theme", m_currentTheme == Light ? 1 : 0);
}

void ThemeManager::applyTheme()
{
    // Build and apply QPalette for efficient widget theming
    QPalette palette;
    
    // Window and general background
    palette.setColor(QPalette::Window, backgroundColor());
    palette.setColor(QPalette::WindowText, textColor());
    
    // Base colors for input widgets (QLineEdit, QTextEdit, etc.)
    palette.setColor(QPalette::Base, backgroundColorAlt());
    palette.setColor(QPalette::AlternateBase, backgroundColorDark());
    palette.setColor(QPalette::Text, textColor());
    palette.setColor(QPalette::PlaceholderText, textColorSecondary());
    
    // Button colors
    palette.setColor(QPalette::Button, buttonColor());
    palette.setColor(QPalette::ButtonText, textColor());
    
    // Selection/highlight colors
    palette.setColor(QPalette::Highlight, selectedColor());
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    
    // Tooltip colors
    palette.setColor(QPalette::ToolTipBase, backgroundColorAlt());
    palette.setColor(QPalette::ToolTipText, textColor());
    
    // Link colors
    palette.setColor(QPalette::Link, accentColor());
    palette.setColor(QPalette::LinkVisited, accentHoverColor());
    
    // Light/midlight/dark/mid for 3D effects
    palette.setColor(QPalette::Light, m_currentTheme == Dark ? QColor(60, 60, 60) : QColor(255, 255, 255));
    palette.setColor(QPalette::Midlight, m_currentTheme == Dark ? QColor(50, 50, 50) : QColor(240, 240, 240));
    palette.setColor(QPalette::Mid, borderColor());
    palette.setColor(QPalette::Dark, m_currentTheme == Dark ? QColor(30, 30, 30) : QColor(180, 180, 180));
    palette.setColor(QPalette::Shadow, m_currentTheme == Dark ? QColor(0, 0, 0) : QColor(100, 100, 100));
    
    // Disabled state colors
    palette.setColor(QPalette::Disabled, QPalette::WindowText, textColorSecondary());
    palette.setColor(QPalette::Disabled, QPalette::Text, textColorSecondary());
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, textColorSecondary());
    palette.setColor(QPalette::Disabled, QPalette::HighlightedText, textColorSecondary());
    
    QApplication::setPalette(palette);
    
    // Minimal global stylesheet for pseudo-elements that QPalette cannot style
    // This covers only: scrollbars, sliders, combo dropdowns, checkboxes, radio buttons,
    // and accent/danger button variants
    QString globalStyleSheet = QString(
        // Scrollbar styling
        "QScrollBar:vertical { background: %1; width: 12px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %2; min-height: 20px; border-radius: 6px; margin: 2px; }"
        "QScrollBar::handle:vertical:hover { background: %3; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal { background: %1; height: 12px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %2; min-width: 20px; border-radius: 6px; margin: 2px; }"
        "QScrollBar::handle:horizontal:hover { background: %3; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
        
        // Slider styling
        "QSlider::groove:horizontal { background: %1; height: 6px; border-radius: 3px; }"
        "QSlider::handle:horizontal { background: %4; width: 14px; height: 14px; margin: -4px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: %5; }"
        "QSlider::sub-page:horizontal { background: %4; border-radius: 3px; }"
        "QSlider::groove:vertical { background: %1; width: 6px; border-radius: 3px; }"
        "QSlider::handle:vertical { background: %4; width: 14px; height: 14px; margin: 0 -4px; border-radius: 7px; }"
        "QSlider::handle:vertical:hover { background: %5; }"
        
        // ComboBox dropdown arrow
        "QComboBox::drop-down { border: none; width: 20px; }"
        "QComboBox::down-arrow { image: none; border-left: 4px solid transparent; border-right: 4px solid transparent; "
        "border-top: 6px solid %6; margin-right: 6px; }"
        "QComboBox QAbstractItemView { background: %7; border: 1px solid %8; selection-background-color: %9; }"
        
        // CheckBox indicator
        "QCheckBox::indicator { width: 16px; height: 16px; border-radius: 3px; }"
        "QCheckBox::indicator:unchecked { background: %10; border: 1px solid %8; }"
        "QCheckBox::indicator:unchecked:hover { border: 1px solid %4; }"
        "QCheckBox::indicator:checked { background: %4; border: 1px solid %4; }"
        "QCheckBox::indicator:checked:hover { background: %5; border: 1px solid %5; }"
        
        // RadioButton indicator
        "QRadioButton::indicator { width: 16px; height: 16px; border-radius: 8px; }"
        "QRadioButton::indicator:unchecked { background: %10; border: 1px solid %8; }"
        "QRadioButton::indicator:unchecked:hover { border: 1px solid %4; }"
        "QRadioButton::indicator:checked { background: %4; border: 1px solid %4; }"
        "QRadioButton::indicator:checked:hover { background: %5; border: 1px solid %5; }"
        
        // GroupBox title styling
        "QGroupBox { border: 1px solid %8; border-radius: 4px; margin-top: 8px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
        
        // Tab widget styling
        "QTabWidget::pane { border: 1px solid %8; border-radius: 4px; }"
        "QTabBar::tab { padding: 8px 16px; border: 1px solid %8; border-bottom: none; border-top-left-radius: 4px; border-top-right-radius: 4px; }"
        "QTabBar::tab:selected { background: %7; }"
        "QTabBar::tab:!selected { background: %11; }"
        "QTabBar::tab:hover:!selected { background: %3; }"
        
        // Menu styling
        "QMenu { background: %7; border: 1px solid %8; }"
        "QMenu::item { padding: 6px 20px; }"
        "QMenu::item:selected { background: %9; }"
        "QMenu::separator { height: 1px; background: %8; margin: 4px 10px; }"
        "QMenuBar { background: %12; }"
        "QMenuBar::item { padding: 6px 10px; }"
        "QMenuBar::item:selected { background: %9; }"
        
        // Header view (table/tree headers)
        "QHeaderView::section { background: %7; padding: 4px 8px; border: none; border-right: 1px solid %8; border-bottom: 1px solid %8; }"
        
        // ToolBar styling
        "QToolBar { background: %12; border: none; spacing: 2px; }"
        "QToolButton { padding: 4px; border-radius: 4px; }"
        "QToolButton:hover { background: %3; }"
        "QToolButton:pressed { background: %9; }"
        
        // Accent button class
        "QPushButton[class=\"accent\"] { background-color: %4; color: #ffffff; border: none; padding: 8px 24px; border-radius: 4px; }"
        "QPushButton[class=\"accent\"]:hover { background-color: %5; }"
        "QPushButton[class=\"accent\"]:pressed { background-color: %13; }"
        "QPushButton[class=\"accent\"]:disabled { background-color: %8; color: %14; }"
        
        // Danger button class
        "QPushButton[class=\"danger\"] { background-color: %15; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton[class=\"danger\"]:hover { background-color: %16; }"
        "QPushButton[class=\"danger\"]:pressed { background-color: %17; }"
        "QPushButton[class=\"danger\"]:disabled { background-color: %8; color: %14; }"
        
        // Standard button refinements
        "QPushButton { border-radius: 4px; padding: 6px 16px; }"
        "QPushButton:hover { background-color: %3; }"
        
        // LineEdit / SpinBox / ComboBox border styling
        "QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox { border: 1px solid %8; border-radius: 4px; padding: 4px 8px; }"
        "QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid %4; }"
        
        // TextEdit / PlainTextEdit
        "QTextEdit, QPlainTextEdit { border: 1px solid %8; border-radius: 4px; }"
        "QTextEdit:focus, QPlainTextEdit:focus { border: 1px solid %4; }"
        
        // TreeView / TableView / ListView
        "QTreeView, QTableView, QListView { border: none; }"
        "QTreeView::item:hover, QTableView::item:hover, QListView::item:hover { background: %3; }"
        
        // Splitter handle
        "QSplitter::handle { background: %8; }"
        "QSplitter::handle:horizontal { width: 2px; }"
        "QSplitter::handle:vertical { height: 2px; }"
        
        // StatusBar
        "QStatusBar { background: %12; border-top: 1px solid %8; }"
        
        // ProgressBar
        "QProgressBar { border: 1px solid %8; border-radius: 4px; text-align: center; }"
        "QProgressBar::chunk { background: %4; border-radius: 3px; }"
        
        // Star rating widget buttons
        "StarRatingWidget QPushButton { background: transparent; border: none; font-size: 18px; color: #FFD700; }"
        "StarRatingWidget QPushButton:hover { background: rgba(255, 255, 255, 0.1); border-radius: 3px; }"
        "StarRatingWidget QPushButton[class=\"clear\"] { font-size: 14px; color: #999; }"
        "StarRatingWidget QPushButton[class=\"clear\"]:hover { color: #fff; }"
    ).arg(backgroundColorDark().name())      // %1 - scrollbar/slider groove background
     .arg(borderColor().name())              // %2 - scrollbar handle
     .arg(hoverColor().name())               // %3 - hover backgrounds
     .arg(accentColor().name())              // %4 - accent color (handles, checks, accent buttons)
     .arg(accentHoverColor().name())         // %5 - accent hover
     .arg(textColor().name())                // %6 - dropdown arrow color
     .arg(backgroundColorAlt().name())       // %7 - popup/dropdown backgrounds
     .arg(borderColor().name())              // %8 - borders
     .arg(selectedColor().name())            // %9 - selection background
     .arg(m_currentTheme == Dark ? "#2a2a2a" : "#ffffff") // %10 - unchecked checkbox bg
     .arg(buttonColor().name())              // %11 - unselected tab background
     .arg(toolbarColor().name())             // %12 - toolbar/menubar/statusbar background
     .arg(m_currentTheme == Dark ? "#3d7bbf" : "#0056a3") // %13 - accent pressed
     .arg(textColorSecondary().name())       // %14 - disabled text
     .arg(dangerColor().name())              // %15 - danger button
     .arg(dangerHoverColor().name())         // %16 - danger hover
     .arg(m_currentTheme == Dark ? "#9e2a35" : "#a61b1b"); // %17 - danger pressed
    
    qApp->setStyleSheet(globalStyleSheet);
}

// Color getters
QColor ThemeManager::backgroundColor() const
{
    return m_currentTheme == Dark ? QColor(18, 18, 18) : QColor(255, 255, 255);
}

QColor ThemeManager::backgroundColorAlt() const
{
    return m_currentTheme == Dark ? QColor(26, 26, 26) : QColor(248, 248, 248);
}

QColor ThemeManager::backgroundColorDark() const
{
    return m_currentTheme == Dark ? QColor(10, 10, 10) : QColor(232, 232, 232);
}

QColor ThemeManager::textColor() const
{
    return m_currentTheme == Dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
}

QColor ThemeManager::iconColor() const
{
    // Icons should be white on dark theme, black on light theme
    return m_currentTheme == Dark ? QColor(255, 255, 255) : QColor(0, 0, 0);
}

QColor ThemeManager::textColorSecondary() const
{
    return m_currentTheme == Dark ? QColor(153, 153, 153) : QColor(102, 102, 102);
}

QColor ThemeManager::borderColor() const
{
    return m_currentTheme == Dark ? QColor(51, 51, 51) : QColor(218, 218, 218);
}

QColor ThemeManager::hoverColor() const
{
    return m_currentTheme == Dark ? QColor(32, 32, 32) : QColor(240, 240, 240);
}

QColor ThemeManager::selectedColor() const
{
    return m_currentTheme == Dark ? QColor(47, 58, 74) : QColor(0, 120, 215);
}

QColor ThemeManager::buttonColor() const
{
    return m_currentTheme == Dark ? QColor(51, 51, 51) : QColor(240, 240, 240);
}

QColor ThemeManager::buttonHoverColor() const
{
    return m_currentTheme == Dark ? QColor(68, 68, 68) : QColor(225, 225, 225);
}

QColor ThemeManager::accentColor() const
{
    return m_currentTheme == Dark ? QColor(88, 166, 255) : QColor(0, 120, 215);
}

QColor ThemeManager::accentHoverColor() const
{
    return m_currentTheme == Dark ? QColor(74, 143, 217) : QColor(0, 100, 180);
}

QColor ThemeManager::dangerColor() const
{
    return m_currentTheme == Dark ? QColor(215, 58, 73) : QColor(196, 43, 28);
}

QColor ThemeManager::dangerHoverColor() const
{
    return m_currentTheme == Dark ? QColor(181, 42, 58) : QColor(164, 38, 44);
}

QColor ThemeManager::cardColor() const
{
    return m_currentTheme == Dark ? QColor(26, 26, 26) : QColor(250, 250, 250);
}

QColor ThemeManager::cardHoverColor() const
{
    return m_currentTheme == Dark ? QColor(38, 38, 38) : QColor(235, 235, 235);
}

QColor ThemeManager::cardSelectedColor() const
{
    return m_currentTheme == Dark ? QColor(62, 90, 140) : QColor(204, 232, 255);
}

QColor ThemeManager::toolbarColor() const
{
    return m_currentTheme == Dark ? QColor(26, 26, 26) : QColor(250, 250, 250);
}

