#include "ColorButton.h"

#include "Icons.h"

#include <QEvent>
#include <QGridLayout>
#include <QMenu>
#include <QPainter>
#include <QWidgetAction>

namespace quill::detail {

QList<QColor> defaultPaletteColors()
{
    // Flutter Colors.*[500], in Material order.
    static const QList<QColor> colors{QColor(QRgb(0xF44336)), QColor(QRgb(0xE91E63)), QColor(QRgb(0x9C27B0)),
                                      QColor(QRgb(0x673AB7)), QColor(QRgb(0x3F51B5)), QColor(QRgb(0x2196F3)),
                                      QColor(QRgb(0x03A9F4)), QColor(QRgb(0x00BCD4)), QColor(QRgb(0x009688)),
                                      QColor(QRgb(0x4CAF50)), QColor(QRgb(0x8BC34A)), QColor(QRgb(0xCDDC39)),
                                      QColor(QRgb(0xFFEB3B)), QColor(QRgb(0xFFC107)), QColor(QRgb(0xFF9800)),
                                      QColor(QRgb(0xFF5722)), QColor(QRgb(0x795548)), QColor(QRgb(0x9E9E9E)),
                                      QColor(QRgb(0x607D8B)), QColor(QRgb(0x000000)), QColor(QRgb(0xFFFFFF))};
    return colors;
}

ColorButton::ColorButton(const QString& iconName, QWidget* parent)
    : QToolButton(parent), m_iconName(iconName), m_colors(defaultPaletteColors()), m_menu(new QMenu(this))
{
    setPopupMode(QToolButton::InstantPopup);
    setMenu(m_menu);
    setAutoRaise(true);
    rebuildMenu();
    updateIcon();
}

void ColorButton::setColors(const QList<QColor>& colors)
{
    m_colors = colors;
    rebuildMenu();
}

void ColorButton::setCurrentColor(const std::optional<QColor>& color)
{
    if (m_current == color)
        return;
    m_current = color;
    updateIcon();
}

void ColorButton::selectColor(const std::optional<QColor>& color)
{
    m_menu->close();
    Q_EMIT colorSelected(color);
}

void ColorButton::changeEvent(QEvent* e)
{
    QToolButton::changeEvent(e);
    if (e->type() == QEvent::PaletteChange || e->type() == QEvent::ApplicationPaletteChange)
        updateIcon();
}

void ColorButton::rebuildMenu()
{
    m_menu->clear();
    auto* grid = new QWidget(m_menu);
    auto* layout = new QGridLayout(grid);
    layout->setSpacing(2);
    layout->setContentsMargins(6, 6, 6, 6);
    const int columns = 7;
    for (int i = 0; i < m_colors.size(); ++i) {
        const QColor c = m_colors.at(i);
        auto* swatch = new QToolButton(grid);
        swatch->setAutoRaise(true);
        swatch->setToolTip(c.name(QColor::HexRgb).toUpper());
        QPixmap pm(16, 16);
        pm.fill(c);
        QPainter(&pm).drawRect(0, 0, 15, 15);
        swatch->setIcon(QIcon(pm));
        swatch->setObjectName(QStringLiteral("swatch-%1").arg(c.name(QColor::HexRgb)));
        connect(swatch, &QToolButton::clicked, this, [this, c] { selectColor(c); });
        layout->addWidget(swatch, i / columns, i % columns);
    }
    auto* gridAction = new QWidgetAction(m_menu);
    gridAction->setDefaultWidget(grid);
    m_menu->addAction(gridAction);
    m_menu->addSeparator();
    m_menu->addAction(tr("Clear"), this, [this] { selectColor(std::nullopt); });
    m_menu->addAction(tr("More..."), this, [this] {
        m_menu->close();
        Q_EMIT moreRequested();
    });
}

void ColorButton::updateIcon()
{
    QIcon icon;
    const QIcon glyph = toolbarIcon(m_iconName);
    for (const int size : {16, 24, 32, 48}) {
        QPixmap pm = glyph.pixmap(size, size);
        pm.setDevicePixelRatio(1);
        if (m_current) {
            QPainter p(&pm);
            const qreal unit = size / 24.0;
            p.fillRect(QRectF(3 * unit, 19 * unit, 18 * unit, 3.5 * unit), *m_current);
        }
        icon.addPixmap(pm);
    }
    setIcon(icon);
}

} // namespace quill::detail
