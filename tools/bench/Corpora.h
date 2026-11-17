#pragma once

// Deterministic documents for quill-bench and tst_scaling (milestone M11).

#include <quill/Delta.h>

#include <QString>
#include <QStringList>

namespace quillbench {

/// Names accepted by corpus(): mixed, plain, long-line, many-embeds,
/// nested-lists, flutter-sample.
QStringList corpusNames();

/// A document of the named shape. `scale` multiplies the size (1 = the sizes
/// in plans/M11-large-documents.md: 10 000 mixed lines, 100 000 plain lines,
/// one 200 000-character line, 2 000 embeds, 5 000 list items, the
/// flutter_quill sample 200 times). `fixturesDir` is needed for
/// flutter-sample only.
quill::Delta corpus(const QString& name, double scale = 1.0, const QString& fixturesDir = {});

/// 1 000 lines of HTML with headings, lists and inline formats, for paste.
QString pasteHtml(int lines = 1000);

} // namespace quillbench
