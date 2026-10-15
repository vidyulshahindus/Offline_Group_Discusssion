#pragma once

#include <QString>

namespace EagleLog {

void install(const QString &preferredLogsDir);
void shutdown();
QString directory();
QString filePath();

} // namespace EagleLog
