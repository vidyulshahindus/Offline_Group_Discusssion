#pragma once

#include <QString>
#include <QVector>

struct ClmPeriodInfo {
    int period = 0;
    QString phase;
    QString name;
    QString mode;
    QString eagleRole;
    int topic = 0;
};

class ClmReference
{
public:
    bool loadFromResources();

    QVector<ClmPeriodInfo> periods() const { return m_periods; }
    QString periodLabel(int period) const;
    QString periodName(int period) const;
    QString periodMode(int period) const;

private:
    QVector<ClmPeriodInfo> m_periods;
};
