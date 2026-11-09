#pragma once

// Shared helpers for quill-qt tests.

#include <quill/Delta.h>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QTest>

namespace testutil {

inline QString fixturePath(const QString& relative)
{
    return QStringLiteral(QUILLQT_FIXTURES_DIR) + QLatin1Char('/') + relative;
}

inline QJsonDocument loadJson(const QString& relative)
{
    QFile file(fixturePath(relative));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("cannot open fixture %s", qPrintable(file.fileName()));
    QJsonParseError error;
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        qFatal("cannot parse fixture %s: %s", qPrintable(file.fileName()), qPrintable(error.errorString()));
    return doc;
}

/// Parses Delta JSON written inline in a test; fails hard on invalid input.
/// Single quotes are accepted in place of double quotes, so JSON can be
/// written in plain string literals: "[{'insert':'a\\n'}]".
inline quill::Delta delta(const char* json)
{
    QString error;
    QByteArray bytes(json);
    bytes.replace('\'', '"');
    std::optional<quill::Delta> d = quill::Delta::fromJson(bytes, &error);
    if (!d)
        qFatal("invalid delta literal %s: %s", json, qPrintable(error));
    return *d;
}

inline QByteArray str(const quill::Delta& d)
{
    return d.toJson();
}

} // namespace testutil

// Readable failure output for QCOMPARE on Deltas and attribute maps.
namespace quill {
inline char* toString(const Delta& d)
{
    return QTest::toString(QString::fromUtf8(d.toJson()));
}
} // namespace quill

namespace QTest {
template<>
inline char* toString(const QJsonObject& o)
{
    return QTest::toString(QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)));
}
} // namespace QTest
