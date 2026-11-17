#pragma once

// quill-collab wire protocol: one JSON object per message, Delta ops in
// Quill.js JSON form (a bare array), so a Quill.js or flutter_quill client
// can take part with a small adapter.
//
//   client -> server
//     {"type":"hello","name":"Ann","color":"#e91e63"}                 first connect
//     {"type":"hello","name":..,"color":..,"clientId":"c3","rev":12} reconnect
//     {"type":"op","rev":12,"seq":4,"delta":[...]}
//     {"type":"cursor","rev":14,"index":5,"length":0}
//   server -> client
//     {"type":"init","rev":12,"doc":[...],"clientId":"c3","peers":[peer...]}
//     {"type":"resume","rev":15,"ops":[{"delta":[...],"clientId":"c1","seq":7}...],"peers":[peer...]}
//     {"type":"ack","rev":13}
//     {"type":"op","rev":14,"delta":[...],"clientId":"c1"}
//     {"type":"cursor","clientId":"c1","index":9,"length":3}
//     {"type":"join",  "clientId":"c4","name":"Bob","color":"#3f51b5"}
//     {"type":"leave", "clientId":"c1"}
//     {"type":"error", "message":"..."}
//   peer: {"clientId":..,"name":..,"color":..,"index":..,"length":..}
//
// Cursor indices are in the document at the message's revision: the client
// sends its cursor mapped to the server revision it knows; the server moves
// it to its current revision before relaying it.

#include <quill/Delta.h>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

namespace collab::proto {

inline QString type(const QJsonObject& m)
{
    return m.value(QLatin1String("type")).toString();
}

inline QJsonObject make(const char* type)
{
    return QJsonObject{{QStringLiteral("type"), QString::fromLatin1(type)}};
}

inline std::optional<quill::Delta> delta(const QJsonValue& v)
{
    return quill::Delta::fromJson(v);
}

} // namespace collab::proto
