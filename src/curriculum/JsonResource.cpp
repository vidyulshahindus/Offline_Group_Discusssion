#include "JsonResource.h"

#include <QFile>

QJsonDocument JsonResource::loadDocument(const QString &resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll());
}
