#pragma once

#include "UnitPack.h"

#include <QString>
#include <QVector>

class Paths;

struct UnitLibraryEntry {
    UnitPack pack;
    QString displayLabel;
    bool fromBuiltin = false;
};

class UnitLibrary
{
public:
    explicit UnitLibrary(const Paths &paths);

    void reload();
    QVector<UnitLibraryEntry> entries() const { return m_entries; }

    QVector<UnitLibraryEntry> filtered(const QString &programme, int grade,
                                       const QString &subject) const;
    UnitPack packById(const QString &id) const;

private:
    void loadBuiltin();
    void loadSchoolDir();
    bool shouldList(const UnitPack &pack) const;

    const Paths &m_paths;
    QVector<UnitLibraryEntry> m_entries;
};
