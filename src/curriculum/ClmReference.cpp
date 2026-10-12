#include "ClmReference.h"

#include "JsonResource.h"

#include <QJsonArray>
#include <QJsonObject>

bool ClmReference::loadFromResources()
{
    const QJsonDocument doc =
        JsonResource::loadDocument(QStringLiteral(":/data/clm/clm_phases.json"));
    if (!doc.isObject()) {
        return false;
    }
    m_periods.clear();
    const QJsonArray phases = doc.object().value(QStringLiteral("phases")).toArray();
    for (const QJsonValue &value : phases) {
        const QJsonObject obj = value.toObject();
        ClmPeriodInfo info;
        info.period = obj.value(QStringLiteral("period")).toInt();
        info.phase = obj.value(QStringLiteral("phase")).toString();
        info.name = obj.value(QStringLiteral("name")).toString();
        info.mode = obj.value(QStringLiteral("mode")).toString();
        info.eagleRole = obj.value(QStringLiteral("eagle_role")).toString();
        if (!obj.value(QStringLiteral("topic")).isNull()) {
            info.topic = obj.value(QStringLiteral("topic")).toInt();
        }
        m_periods.append(info);
    }
    return !m_periods.isEmpty();
}

QString ClmReference::periodLabel(int period) const
{
    for (const ClmPeriodInfo &info : m_periods) {
        if (info.period == period) {
            return QStringLiteral("P%1 %2").arg(period).arg(info.name);
        }
    }
    return QStringLiteral("P%1").arg(period);
}

QString ClmReference::periodName(int period) const
{
    for (const ClmPeriodInfo &info : m_periods) {
        if (info.period == period) {
            return info.name;
        }
    }
    return {};
}

QString ClmReference::periodMode(int period) const
{
    for (const ClmPeriodInfo &info : m_periods) {
        if (info.period == period) {
            return info.mode;
        }
    }
    return {};
}
