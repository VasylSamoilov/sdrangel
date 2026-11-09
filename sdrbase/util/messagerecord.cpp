///////////////////////////////////////////////////////////////////////////////////
// Copyright (C) 2026 Vasyl Samoilov <vasyl.samoilov@gmail.com>                  //
//                                                                               //
// This program is free software; you can redistribute it and/or modify          //
// it under the terms of the GNU General Public License as published by          //
// the Free Software Foundation as version 3 of the License, or                  //
// (at your option) any later version.                                           //
//                                                                               //
// This program is distributed in the hope that it will be useful,               //
// but WITHOUT ANY WARRANTY; without even the implied warranty of                //
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the                  //
// GNU General Public License V3 for more details.                               //
//                                                                               //
// You should have received a copy of the GNU General Public License             //
// along with this program. If not, see <http://www.gnu.org/licenses/>.          //
///////////////////////////////////////////////////////////////////////////////////

#include "messagerecord.h"

#include <QJsonDocument>
#include <QJsonValue>

#include "util/csv.h"

const char * const MessageRecord::m_pipeName = "messages";

namespace {

QJsonValue stringOrNull(const QString& s)
{
    return s.isNull() ? QJsonValue(QJsonValue::Null) : QJsonValue(s);
}

QJsonValue intOrNull(qint64 v)
{
    return v < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(v);
}

QString readString(const QJsonObject& json, const char *key)
{
    QJsonValue v = json.value(key);
    return v.isString() ? v.toString() : QString();
}

qint64 readInt(const QJsonObject& json, const char *key, qint64 nullValue = -1)
{
    QJsonValue v = json.value(key);
    return v.isDouble() ? (qint64) v.toDouble() : nullValue;
}

QString csvString(const QString& s)
{
    return s.isNull() ? QString("") : CSV::escape(s);
}

QString csvInt(qint64 v)
{
    return v < 0 ? QString("") : QString::number(v);
}

} // namespace

MessageRecord::MessageRecord()
{
    resetToDefaults();
}

void MessageRecord::resetToDefaults()
{
    m_id = QString();
    m_timestamp = 0;
    m_direction = DirectionRX;
    m_protocol = QString();
    m_mode = QString();
    m_kind = KindMessage;
    m_msgType = QString();
    m_address = QString();
    m_polarity = PolarityUnknown;
    m_speed = -1;
    m_text = QString();
    m_rawData.clear();
    m_rawBits = -1;
    m_errorsCorrected = -1;
    m_errorsUncorrectable = -1;
    m_ber = -1.0f;
    m_charStatus = QString();
    m_frequency = -1;
    m_channelAlias = QString();
    m_deviceSetIndex = -1;
    m_channelIndex = -1;
    m_centerFrequency = -1;
    m_channelOffset = 0;
    m_duplicate = DuplicateNone;
    m_partial = false;
    m_protocolData.clear();
}

QString MessageRecord::dedupKey() const
{
    QString key = QString("%1|%2|%3").arg(m_protocol, m_address, polarityToString(m_polarity));

    if (m_protocolData.contains("function")) {
        key += "|f" + m_protocolData.value("function").toString();
    }
    if (m_protocolData.contains("msgNumber")) {
        key += "|n" + m_protocolData.value("msgNumber").toString();
    }

    return key;
}

QJsonObject MessageRecord::toJson() const
{
    QJsonObject json;

    json.insert("schemaVersion", m_schemaVersion);
    json.insert("id", m_id);
    json.insert("timestamp", m_timestamp);
    json.insert("direction", directionToString(m_direction));
    json.insert("protocol", stringOrNull(m_protocol));
    json.insert("mode", stringOrNull(m_mode));
    json.insert("kind", kindToString(m_kind));
    json.insert("msgType", stringOrNull(m_msgType));
    json.insert("address", stringOrNull(m_address));
    json.insert("polarity", polarityToString(m_polarity));
    json.insert("speed", intOrNull(m_speed));
    json.insert("text", stringOrNull(m_text));
    json.insert("rawData", m_rawBits < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(QString::fromLatin1(m_rawData.toHex())));
    json.insert("rawBits", intOrNull(m_rawBits));
    json.insert("errorsCorrected", intOrNull(m_errorsCorrected));
    json.insert("errorsUncorrectable", intOrNull(m_errorsUncorrectable));
    json.insert("ber", m_ber < 0.0f ? QJsonValue(QJsonValue::Null) : QJsonValue((double) m_ber));
    json.insert("charStatus", stringOrNull(m_charStatus));
    json.insert("frequency", intOrNull(m_frequency));
    json.insert("channelAlias", stringOrNull(m_channelAlias));
    json.insert("deviceSetIndex", intOrNull(m_deviceSetIndex));
    json.insert("channelIndex", intOrNull(m_channelIndex));
    json.insert("centerFrequency", intOrNull(m_centerFrequency));
    json.insert("channelOffset", m_centerFrequency < 0 ? QJsonValue(QJsonValue::Null) : QJsonValue(m_channelOffset));
    json.insert("duplicate", duplicateToString(m_duplicate));
    json.insert("partial", m_partial);
    json.insert("protocolData", QJsonObject::fromVariantMap(m_protocolData));

    return json;
}

bool MessageRecord::fromJson(const QJsonObject& json, MessageRecord& record)
{
    record.resetToDefaults();

    if (!json.contains("id") || !json.contains("timestamp") || !json.contains("protocol")) {
        return false;
    }

    record.m_id = readString(json, "id");
    record.m_timestamp = readInt(json, "timestamp", 0);
    record.m_direction = directionFromString(readString(json, "direction"));
    record.m_protocol = readString(json, "protocol");
    record.m_mode = readString(json, "mode");
    record.m_kind = kindFromString(readString(json, "kind"));
    record.m_msgType = readString(json, "msgType");
    record.m_address = readString(json, "address");
    record.m_polarity = polarityFromString(readString(json, "polarity"));
    record.m_speed = (int) readInt(json, "speed");
    record.m_text = readString(json, "text");
    record.m_rawBits = (int) readInt(json, "rawBits");
    if (record.m_rawBits >= 0) {
        record.m_rawData = QByteArray::fromHex(readString(json, "rawData").toLatin1());
    }
    record.m_errorsCorrected = (int) readInt(json, "errorsCorrected");
    record.m_errorsUncorrectable = (int) readInt(json, "errorsUncorrectable");
    QJsonValue ber = json.value("ber");
    record.m_ber = ber.isDouble() ? (float) ber.toDouble() : -1.0f;
    record.m_charStatus = readString(json, "charStatus");
    record.m_frequency = readInt(json, "frequency");
    record.m_channelAlias = readString(json, "channelAlias");
    record.m_deviceSetIndex = (int) readInt(json, "deviceSetIndex");
    record.m_channelIndex = (int) readInt(json, "channelIndex");
    record.m_centerFrequency = readInt(json, "centerFrequency");
    QJsonValue offset = json.value("channelOffset");
    record.m_channelOffset = offset.isDouble() ? (qint64) offset.toDouble() : 0;
    record.m_duplicate = duplicateFromString(readString(json, "duplicate"));
    record.m_partial = json.value("partial").toBool(false);
    record.m_protocolData = json.value("protocolData").toObject().toVariantMap();

    return true;
}

QByteArray MessageRecord::toJsonLine() const
{
    QByteArray line = QJsonDocument(toJson()).toJson(QJsonDocument::Compact);
    line.append('\n');
    return line;
}

QStringList MessageRecord::csvHeader()
{
    return {
        "id", "timestamp", "direction", "protocol", "mode", "kind", "msgType",
        "address", "polarity", "speed", "text", "rawData", "rawBits",
        "errorsCorrected", "errorsUncorrectable", "ber", "charStatus",
        "frequency", "channelAlias", "deviceSetIndex", "channelIndex",
        "centerFrequency", "channelOffset", "duplicate", "partial", "protocolData"
    };
}

QString MessageRecord::toCSV() const
{
    QStringList fields;

    fields.append(CSV::escape(m_id));
    fields.append(QString::number(m_timestamp));
    fields.append(directionToString(m_direction));
    fields.append(csvString(m_protocol));
    fields.append(csvString(m_mode));
    fields.append(kindToString(m_kind));
    fields.append(csvString(m_msgType));
    fields.append(csvString(m_address));
    fields.append(polarityToString(m_polarity));
    fields.append(csvInt(m_speed));
    fields.append(csvString(m_text));
    fields.append(m_rawBits < 0 ? QString("") : QString::fromLatin1(m_rawData.toHex()));
    fields.append(csvInt(m_rawBits));
    fields.append(csvInt(m_errorsCorrected));
    fields.append(csvInt(m_errorsUncorrectable));
    fields.append(m_ber < 0.0f ? QString("") : QString::number(m_ber));
    fields.append(csvString(m_charStatus));
    fields.append(csvInt(m_frequency));
    fields.append(csvString(m_channelAlias));
    fields.append(csvInt(m_deviceSetIndex));
    fields.append(csvInt(m_channelIndex));
    fields.append(csvInt(m_centerFrequency));
    fields.append(m_centerFrequency < 0 ? QString("") : QString::number(m_channelOffset));
    fields.append(duplicateToString(m_duplicate));
    fields.append(m_partial ? "1" : "0");
    fields.append(CSV::escape(QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(m_protocolData)).toJson(QJsonDocument::Compact))));

    return fields.join(',');
}

QString MessageRecord::directionToString(Direction direction)
{
    return direction == DirectionTX ? "tx" : "rx";
}

MessageRecord::Direction MessageRecord::directionFromString(const QString& string)
{
    return string == "tx" ? DirectionTX : DirectionRX;
}

QString MessageRecord::kindToString(Kind kind)
{
    switch (kind)
    {
    case KindEvent:
        return "event";
    case KindDiagnostic:
        return "diagnostic";
    case KindMessage:
    default:
        return "message";
    }
}

MessageRecord::Kind MessageRecord::kindFromString(const QString& string)
{
    if (string == "event") {
        return KindEvent;
    } else if (string == "diagnostic") {
        return KindDiagnostic;
    } else {
        return KindMessage;
    }
}

QString MessageRecord::polarityToString(Polarity polarity)
{
    switch (polarity)
    {
    case PolarityNormal:
        return "normal";
    case PolarityInverted:
        return "inverted";
    case PolarityUnknown:
    default:
        return "unknown";
    }
}

MessageRecord::Polarity MessageRecord::polarityFromString(const QString& string)
{
    if (string == "normal") {
        return PolarityNormal;
    } else if (string == "inverted") {
        return PolarityInverted;
    } else {
        return PolarityUnknown;
    }
}

QString MessageRecord::duplicateToString(Duplicate duplicate)
{
    switch (duplicate)
    {
    case DuplicateDup:
        return "dup";
    case DuplicateDupPlus:
        return "dup+";
    case DuplicateNone:
    default:
        return "none";
    }
}

MessageRecord::Duplicate MessageRecord::duplicateFromString(const QString& string)
{
    if (string == "dup") {
        return DuplicateDup;
    } else if (string == "dup+") {
        return DuplicateDupPlus;
    } else {
        return DuplicateNone;
    }
}
