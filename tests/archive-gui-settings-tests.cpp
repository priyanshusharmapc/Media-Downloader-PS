#include "../src/archive/archivesettings.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    app.setOrganizationName("MediaDownloaderTest");
    app.setApplicationName("ArchiveGuiSettingsTest");
    QStandardPaths::setTestModeEnabled(true);
    const auto tempRoot=qEnvironmentVariable("ARCHIVE_TEST_TMP",QDir::tempPath());
    QTemporaryDir package(QDir(tempRoot).filePath("archive-gui-package-XXXXXX"));QTemporaryDir archive(QDir(tempRoot).filePath("archive-gui-data-XXXXXX"));
    if(!package.isValid()||!archive.isValid())return 1;
    const auto configRoot=QDir(tempRoot).filePath("archive-gui-config");
    qputenv("ARCHIVE_TEST_CONFIG_ROOT",configRoot.toUtf8());
    archive::ui::persistRoot(archive.path());
    const auto settings=archive::ui::settingsFile();
    if(settings.isEmpty()||!QFileInfo::exists(settings))return 2;
    const auto packageSettings=QDir(package.path()).filePath("local/settings/archive-mode.ini");
    if(QFileInfo::exists(packageSettings))return 3;
    if(archive::ui::configuredRoot(package.path(),{})!=QDir::cleanPath(archive.path()))return 4;
    QTextStream(stdout)<<"archive-gui-settings-tests: PASS\n";
    return 0;
}
