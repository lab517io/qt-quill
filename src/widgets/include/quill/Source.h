#pragma once

#include <QMetaType>

namespace quill {

/// Who caused a change, as Quill's "api" / "user" / "silent" sources.
enum class Source {
    Api,    ///< a call of the editor API
    User,   ///< keyboard, mouse, clipboard, undo/redo
    Silent, ///< a call of the editor API that emits no signal
};

} // namespace quill

Q_DECLARE_METATYPE(quill::Source)
