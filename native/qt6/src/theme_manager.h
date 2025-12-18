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
    
    // Load theme from settings and apply it
    void loadTheme();
    
    // Save theme to settings
    void saveTheme();
    
    // Apply the current theme (palette + minimal global stylesheet)
    void applyTheme();

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

