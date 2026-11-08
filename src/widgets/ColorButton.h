#pragma once

// Tool button with a colour swatch and a popup palette (private header).

#include <QColor>
#include <QList>
#include <QToolButton>

#include <optional>

class QMenu;

namespace quill::detail {

class ColorButton : public QToolButton
{
    Q_OBJECT
public:
    ColorButton(const QString& iconName, QWidget* parent = nullptr);

    void setColors(const QList<QColor>& colors);
    QList<QColor> colors() const { return m_colors; }
    /// Colour shown in the swatch bar; nullopt shows none.
    void setCurrentColor(const std::optional<QColor>& color);
    std::optional<QColor> currentColor() const { return m_current; }

public Q_SLOTS:
    /// What clicking a swatch (or "Clear" for nullopt) does.
    void selectColor(const std::optional<QColor>& color);

Q_SIGNALS:
    void colorSelected(const std::optional<QColor>& color);
    void moreRequested();

protected:
    void changeEvent(QEvent* e) override;

private:
    void rebuildMenu();
    void updateIcon();

    QString m_iconName;
    QList<QColor> m_colors;
    std::optional<QColor> m_current;
    QMenu* m_menu;
};

/// The 21 colours of the popup: Material 500 shades, grey, black and white.
QList<QColor> defaultPaletteColors();

} // namespace quill::detail
