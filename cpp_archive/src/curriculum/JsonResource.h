#pragma once

#include <QJsonDocument>
#include <QString>

namespace JsonResource {

QJsonDocument loadDocument(const QString &resourcePath);

}
