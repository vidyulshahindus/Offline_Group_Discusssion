#include "UnitLibrary.h"

#include "Paths.h"

#include <QDir>
#include <QFile>

namespace {

const QStringList kBuiltinUnitFiles = {
    QStringLiteral("dp-bio-g11-cells-form-function.json"),
    QStringLiteral("myp-g9-industrial-revolution.json"),
    QStringLiteral("myp-g7-delhi-sultanate.json"),
};

} // namespace

UnitLibrary::UnitLibrary(const Paths &paths)
    : m_paths(paths)
{
    reload();
}

void UnitLibrary::reload()
{
    m_entries.clear();
    loadBuiltin();
    loadSchoolDir();
}

void UnitLibrary::loadBuiltin()
{
    for (const QString &fileName : kBuiltinUnitFiles) {
        const QString path = QStringLiteral(":/data/units/") + fileName;
        QString err;
        UnitPack pack = UnitPack::fromFile(path, &err);
        if (!pack.id().isEmpty()) {
            pack.setBuiltin(true);
            pack.setSourcePath(path);
            UnitLibraryEntry entry;
            entry.pack = pack;
            entry.fromBuiltin = true;
            entry.displayLabel =
                QStringLiteral("%1 — %2 (sample)").arg(pack.title(), pack.programme());
            if (shouldList(pack)) {
                m_entries.append(entry);
            }
        }
    }
}

void UnitLibrary::loadSchoolDir()
{
    QDir dir(m_paths.unitsDir());
    if (!dir.exists()) {
        return;
    }
    const QStringList files = dir.entryList({QStringLiteral("*.json")}, QDir::Files);
    for (const QString &fileName : files) {
        const QString path = dir.filePath(fileName);
        QString err;
        UnitPack pack = UnitPack::fromFile(path, &err);
        if (pack.id().isEmpty()) {
            continue;
        }
        if (pack.id() == QStringLiteral("domain_science_museum")) {
            continue;
        }
        UnitLibraryEntry entry;
        entry.pack = pack;
        entry.displayLabel = QStringLiteral("%1 — %2").arg(pack.title(), pack.programme());
        if (shouldList(pack)) {
            m_entries.append(entry);
        }
    }
}

bool UnitLibrary::shouldList(const UnitPack &pack) const
{
    const QString status = pack.approvalStatus();
    if (pack.isBuiltin()) {
        return true;
    }
    return status == QStringLiteral("approved") || status == QStringLiteral("reviewed");
}

QVector<UnitLibraryEntry> UnitLibrary::filtered(const QString &programme, int grade,
                                                const QString &subject) const
{
    QVector<UnitLibraryEntry> out;
    for (const UnitLibraryEntry &entry : m_entries) {
        if (!programme.isEmpty() && entry.pack.programme() != programme) {
            continue;
        }
        if (grade > 0 && entry.pack.grade() != grade) {
            continue;
        }
        if (!subject.isEmpty() && entry.pack.subject().compare(subject, Qt::CaseInsensitive) != 0) {
            continue;
        }
        out.append(entry);
    }
    return out;
}

UnitPack UnitLibrary::packById(const QString &id) const
{
    for (const UnitLibraryEntry &entry : m_entries) {
        if (entry.pack.id() == id) {
            return entry.pack;
        }
    }
    return {};
}
