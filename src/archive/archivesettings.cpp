#include "archivesettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>

namespace archive { namespace ui {
namespace {
QString settingsPath()
{
    const auto testRoot=qEnvironmentVariable("ARCHIVE_TEST_CONFIG_ROOT");
    if(!testRoot.isEmpty()){
        QDir().mkpath(testRoot);
        return QDir(testRoot).filePath("archive-mode.ini");
    }
    auto directory=QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if(directory.isEmpty())directory=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if(directory.isEmpty())directory=QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath("MediaDownloader");
    QDir().mkpath(directory);
    return QDir(directory).filePath("archive-mode.ini");
}
}

QString settingsFile(){return settingsPath();}

QString configuredRoot(const QString& applicationDir,const QString& downloadFolder)
{
    QSettings settings(settingsPath(),QSettings::IniFormat);
    const auto stored=settings.value("ArchiveRoot").toString();
    if(!stored.isEmpty()&&QFileInfo(stored).isDir())return QDir::cleanPath(stored);
    // Read the pre-release relative setting once for a non-destructive
    // migration; persistence is converted to an absolute user setting when
    // the operator next selects the root.
    const auto relative=settings.value("ArchiveRootRelativeToApp").toString();
    if(!relative.isEmpty()){
        const auto candidate=QDir::cleanPath(QDir(applicationDir).filePath(relative));
        if(QFileInfo(candidate).isDir())return candidate;
    }
    const auto parent=QFileInfo(applicationDir).absoluteDir().absolutePath();
    if(QFileInfo(QDir(parent).filePath("State")).exists()||QFileInfo(QDir(parent).filePath("Video")).exists()||QFileInfo(QDir(parent).filePath("Audio")).exists())return parent;
    if(!downloadFolder.isEmpty()&&QFileInfo(downloadFolder).isDir())return downloadFolder;
    return parent;
}

void persistRoot(const QString& root)
{
    QSettings settings(settingsPath(),QSettings::IniFormat);
    settings.setValue("ArchiveRoot",QDir::cleanPath(root));
    settings.remove("ArchiveRootRelativeToApp");
    settings.sync();
}
} }
