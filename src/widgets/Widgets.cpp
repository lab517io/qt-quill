#include "quill/Widgets.h"

#include <QtGlobal>

namespace quill {

QString qtRuntimeVersion()
{
    return QString::fromLatin1(qVersion());
}

} // namespace quill
