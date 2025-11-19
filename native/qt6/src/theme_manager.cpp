#include "theme_manager.h"
#include <QSettings>
#include <QApplication>

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
        emit themeChanged();
    }
}

void ThemeManager::loadTheme()
{
    QSettings s("AugmentCode", "KAssetManager");
    int themeIndex = s.value("Appearance/Theme", 0).toInt();
    m_currentTheme = (themeIndex == 1) ? Light : Dark;
}

void ThemeManager::saveTheme()
{
    QSettings s("AugmentCode", "KAssetManager");
    s.setValue("Appearance/Theme", m_currentTheme == Light ? 1 : 0);
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

// Stylesheet getters
QString ThemeManager::dialogStyleSheet() const
{
    return QString("QDialog { background-color: %1; color: %2; }")
        .arg(backgroundColor().name())
        .arg(textColor().name());
}

QString ThemeManager::tabWidgetStyleSheet() const
{
    return QString(
        "QTabWidget::pane { border: 1px solid %1; background-color: %2; }"
        "QTabBar::tab { background-color: %3; color: %4; padding: 8px 16px; border: 1px solid %1; }"
        "QTabBar::tab:selected { background-color: %2; border-bottom-color: %2; }"
        "QTabBar::tab:hover { background-color: %5; }"
    ).arg(borderColor().name())
     .arg(backgroundColorAlt().name())
     .arg(buttonColor().name())
     .arg(textColor().name())
     .arg(hoverColor().name());
}

QString ThemeManager::menuStyleSheet() const
{
    return QString(
        "QMenu { background-color: %1; color: %2; border: 1px solid %3; }"
        "QMenu::item:selected { background-color: %4; }"
    ).arg(backgroundColorAlt().name())
     .arg(textColor().name())
     .arg(borderColor().name())
     .arg(selectedColor().name());
}

QString ThemeManager::buttonStyleSheet() const
{
    return QString(
        "QPushButton { background-color: %1; color: %2; border: none; padding: 8px 24px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %3; }"
    ).arg(buttonColor().name())
     .arg(textColor().name())
     .arg(buttonHoverColor().name());
}

QString ThemeManager::accentButtonStyleSheet() const
{
    return QString(
        "QPushButton { background-color: %1; color: #ffffff; border: none; padding: 8px 24px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }"
    ).arg(accentColor().name())
     .arg(accentHoverColor().name());
}

QString ThemeManager::dangerButtonStyleSheet() const
{
    return QString(
        "QPushButton { background-color: %1; color: #ffffff; border: none; padding: 8px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: %2; }"
    ).arg(dangerColor().name())
     .arg(dangerHoverColor().name());
}

QString ThemeManager::groupBoxStyleSheet() const
{
    return QString(
        "QGroupBox { color: %1; border: 1px solid %2; padding: 10px; margin-top: 10px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 5px; }"
    ).arg(textColor().name())
     .arg(borderColor().name());
}

QString ThemeManager::comboBoxStyleSheet() const
{
    return QString(
        "QComboBox { background-color: %1; color: %2; border: 1px solid %3; padding: 6px; border-radius: 4px; }"
        "QComboBox:hover { background-color: %4; }"
        "QComboBox::drop-down { border: none; }"
        "QComboBox QAbstractItemView { background-color: %1; color: %2; selection-background-color: %5; }"
    ).arg(buttonColor().name())
     .arg(textColor().name())
     .arg(borderColor().name())
     .arg(buttonHoverColor().name())
     .arg(selectedColor().name());
}

QString ThemeManager::checkBoxStyleSheet() const
{
    QString indicatorBg = m_currentTheme == Dark ? "#2a2a2a" : "#ffffff";
    QString indicatorBorder = m_currentTheme == Dark ? "#666" : "#999";

    return QString(
        "QCheckBox { color: %1; }"
        "QCheckBox::indicator { width: 14px; height: 14px; }"
        "QCheckBox::indicator:checked { background-color: %2; border: 1px solid %2; }"
        "QCheckBox::indicator:unchecked { background-color: %3; border: 1px solid %4; }"
    ).arg(textColor().name())
     .arg(accentColor().name())
     .arg(indicatorBg)
     .arg(indicatorBorder);
}

QString ThemeManager::treeViewStyleSheet() const
{
    return QString(
        "QTreeView { background-color: %1; color: %2; border: none; }"
        "QTreeView::item:selected { background-color: %3; color: %4; }"
        "QTreeView::item:hover { background-color: %5; }"
        "QHeaderView::section { background-color: %6; color: %2; border: none; padding: 4px; }"
    ).arg(backgroundColor().name())
     .arg(textColor().name())
     .arg(selectedColor().name())
     .arg(m_currentTheme == Dark ? "#ffffff" : "#ffffff")
     .arg(hoverColor().name())
     .arg(backgroundColorAlt().name());
}

QString ThemeManager::tableViewStyleSheet() const
{
    return QString(
        "QTableView { background-color: %1; color: %2; border: none; }"
        "QTableView::item { padding: 2px 6px; }"
        "QTableView::item:selected { background-color: %3; }"
        "QHeaderView::section { background-color: %4; color: %2; border: none; padding: 4px; }"
    ).arg(backgroundColorDark().name())
     .arg(textColor().name())
     .arg(selectedColor().name())
     .arg(backgroundColorAlt().name());
}

QString ThemeManager::listViewStyleSheet() const
{
    return QString("QListView { background-color: %1; border: none; }")
        .arg(backgroundColorDark().name());
}

QString ThemeManager::textEditStyleSheet() const
{
    return QString(
        "QTextEdit { background-color: %1; color: %2; border: 1px solid %3; padding: 10px; }"
        "QPlainTextEdit { background-color: %1; color: %2; border: 1px solid %3; padding: 10px; }"
    ).arg(backgroundColorAlt().name())
     .arg(textColor().name())
     .arg(borderColor().name());
}

QString ThemeManager::labelStyleSheet() const
{
    return QString("color: %1;").arg(textColor().name());
}

QString ThemeManager::lineEditStyleSheet() const
{
    return QString(
        "QLineEdit { background-color: %1; color: %2; border: 1px solid %3; padding: 6px; border-radius: 4px; }"
        "QLineEdit:focus { border: 1px solid %4; }"
    ).arg(buttonColor().name())
     .arg(textColor().name())
     .arg(borderColor().name())
     .arg(accentColor().name());
}

QString ThemeManager::spinBoxStyleSheet() const
{
    return QString(
        "QSpinBox { background-color: %1; color: %2; border: 1px solid %3; padding: 4px; }"
        "QSpinBox:focus { border: 1px solid %4; }"
    ).arg(buttonColor().name())
     .arg(textColor().name())
     .arg(borderColor().name())
     .arg(accentColor().name());
}

