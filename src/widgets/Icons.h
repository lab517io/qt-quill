#pragma once

// Toolbar icons drawn with QPainter in the current palette's text colour, so
// they follow light and dark themes without image files (private header).

#include <QIcon>
#include <QString>
#include <QStringList>

namespace quill::detail {

/// Icon for a toolbar action name ("bold", "list-bullet", ...). Unknown
/// names give an empty icon.
QIcon toolbarIcon(const QString& name);
/// Names toolbarIcon() can draw.
QStringList toolbarIconNames();

} // namespace quill::detail
