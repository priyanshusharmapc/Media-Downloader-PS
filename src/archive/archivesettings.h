#ifndef MDPS_ARCHIVESETTINGS_H
#define MDPS_ARCHIVESETTINGS_H

#include <QString>

namespace archive { namespace ui {
QString settingsFile();
QString configuredRoot(const QString& applicationDir,const QString& downloadFolder);
void persistRoot(const QString& root);
} }

#endif
