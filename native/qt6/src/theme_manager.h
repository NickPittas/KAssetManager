#ifndef THEME_MANAGER_H
#define THEME_MANAGER_H

#include <QObject>
#include <QString>
#include <QColor>

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    enum Theme {
        Dark,
        Light
    };

    static ThemeManager& instance();

    // Get/Set current theme
    Theme currentTheme() const { return m_currentTheme; }
    void setTheme(Theme theme);
    
    // Load theme from settings
    void loadTheme();
    
    // Save theme to settings
    void saveTheme();

    // Color getters
    QColor backgroundColor() const;
    QColor backgroundColorAlt() const;
    QColor backgroundColorDark() const;
    QColor textColor() const;
    QColor textColorSecondary() const;
    QColor borderColor() const;
    QColor hoverColor() const;
    QColor selectedColor() const;
    QColor buttonColor() const;
    QColor buttonHoverColor() const;
    QColor accentColor() const;
    QColor accentHoverColor() const;
    QColor dangerColor() const;
    QColor dangerHoverColor() const;
    QColor cardColor() const;
    QColor cardHoverColor() const;
    QColor cardSelectedColor() const;
    QColor toolbarColor() const;

    // Stylesheet getters
    QString dialogStyleSheet() const;
    QString tabWidgetStyleSheet() const;
    QString menuStyleSheet() const;
    QString buttonStyleSheet() const;
    QString accentButtonStyleSheet() const;
    QString dangerButtonStyleSheet() const;
    QString groupBoxStyleSheet() const;
    QString comboBoxStyleSheet() const;
    QString checkBoxStyleSheet() const;
    QString treeViewStyleSheet() const;
    QString tableViewStyleSheet() const;
    QString listViewStyleSheet() const;
    QString textEditStyleSheet() const;
    QString labelStyleSheet() const;
    QString lineEditStyleSheet() const;
    QString spinBoxStyleSheet() const;

    // Icon color getter - returns appropriate color for icons based on theme
    QColor iconColor() const;

signals:
    void themeChanged();

private:
    ThemeManager();
    ~ThemeManager() = default;
    ThemeManager(const ThemeManager&) = delete;
    ThemeManager& operator=(const ThemeManager&) = delete;

    Theme m_currentTheme;
};

#endif // THEME_MANAGER_H

