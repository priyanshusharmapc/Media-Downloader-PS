#include "archivecore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QUrl>
#include <QUrlQuery>

namespace
{
QString firstLine(QString text)
{
    text=text.trimmed();
    const auto newline=text.indexOf('\n');
    return newline<0?text:text.left(newline).trimmed();
}

QString bundledDeno(const QString& appDir)
{
#ifdef Q_OS_WIN
    const QStringList candidates={"local/bin/deno.exe","bin/deno.exe","deno.exe"};
    const QStringList names={"deno.exe","deno"};
#else
    const QStringList candidates={"local/bin/deno","bin/deno","deno"};
    const QStringList names={"deno"};
#endif
    for(const auto& rel:candidates){
        const auto path=QDir(appDir).filePath(rel);
        if(QFileInfo(path).exists()&&QFileInfo(path).isFile()) return QDir::cleanPath(path);
    }
    for(const auto& name:names){
        const auto path=QStandardPaths::findExecutable(name);
        if(!path.isEmpty()) return path;
    }
    return {};
}

bool checkTool(QTextStream& out,QTextStream& err,const QString& name,const QString& path,const QStringList& versionArgs)
{
    if(path.isEmpty()){
        err << name << "=MISSING\n";
        return false;
    }
    QProcess p;
    p.start(path,versionArgs);
    if(!p.waitForStarted(10000)){
        err << name << "=FAILED_TO_START path=" << QDir::toNativeSeparators(path) << " error=" << p.errorString() << "\n";
        return false;
    }
    if(!p.waitForFinished(30000)){
        p.kill(); p.waitForFinished(5000);
        err << name << "=TIMEOUT path=" << QDir::toNativeSeparators(path) << "\n";
        return false;
    }
    const auto stdoutText=QString::fromUtf8(p.readAllStandardOutput());
    const auto stderrText=QString::fromUtf8(p.readAllStandardError());
    if(p.exitStatus()!=QProcess::NormalExit||p.exitCode()!=0){
        err << name << "=FAILED path=" << QDir::toNativeSeparators(path) << " exit=" << p.exitCode()
            << " detail=" << firstLine(stderrText.isEmpty()?stdoutText:stderrText) << "\n";
        return false;
    }
    out << name << "=OK path=" << QDir::toNativeSeparators(path) << " version=" << firstLine(stdoutText.isEmpty()?stderrText:stdoutText) << "\n";
    return true;
}

QString videoIdFromUrl(const QString& text)
{
    const QUrl url=QUrl::fromUserInput(text);
    const auto host=url.host().toLower();
    if(host=="youtu.be"||host.endsWith(".youtu.be")){
        const auto path=url.path().mid(1).section('/',0,0);
        if(!path.isEmpty()) return path;
    }
    const auto id=QUrlQuery(url).queryItemValue("v");
    if(!id.isEmpty()) return id;
    return QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha1).toHex().left(16));
}

void usage(QTextStream& err)
{
    err << "Usage:\n"
           "  archive-cli preflight <archive-root>\n"
           "  archive-cli validate <archive-root> <package-dir>\n"
           "  archive-cli ingest-pending <archive-root>\n"
           "  archive-cli scan <archive-root> <playlist-url> [display-name]\n"
           "  archive-cli sync-item <archive-root> <video-url>\n";
}
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTextStream out(stdout),err(stderr);
    const auto args=app.arguments();
    if(args.size()<3){usage(err);return 2;}

    archive::RuntimeConfig config;
    config.archiveRoot=QDir::cleanPath(args.at(2));
    config.appDir=QCoreApplication::applicationDirPath();
    archive::Paths paths(config.archiveRoot);
    archive::Store store(paths);
    QString initError;
    if(!store.initialize(&initError)){
        err << "Archive initialization failed: " << initError << "\n";
        return 1;
    }
    archive::ActivityLogger logger(paths);

    const auto cmd=args.at(1);
    if(cmd=="preflight"&&args.size()==3){
        archive::ToolResolver tools(config);
        bool ok=true;
        ok=checkTool(out,err,"yt-dlp",tools.ytDlp(),{"--version"})&&ok;
        ok=checkTool(out,err,"ffmpeg",tools.ffmpeg(),{"-version"})&&ok;
        ok=checkTool(out,err,"ffprobe",tools.ffprobe(),{"-version"})&&ok;
        ok=checkTool(out,err,"deno",bundledDeno(config.appDir),{"--version"})&&ok;
        const QStringList resources={
            QDir(config.archiveRoot).filePath("ARCHIVE_AGENT.md"),
            QDir(paths.schemas()).filePath("recovery-package.schema.json"),
            QDir(paths.schemas()).filePath("recovery-package.example.json")
        };
        for(const auto& resource:resources){
            if(!QFileInfo::exists(resource)){
                err << "resource=MISSING path=" << QDir::toNativeSeparators(resource) << "\n";
                ok=false;
            }else{
                out << "resource=OK path=" << QDir::toNativeSeparators(resource) << "\n";
            }
        }
        out << "preflight=" << (ok?"PASS":"FAIL") << "\n";
        return ok?0:1;
    }
    if(cmd=="validate"&&args.size()==4){
        archive::RecoveryImporter importer(config,store,logger);
        const auto result=importer.validate(QDir::cleanPath(args.at(3)));
        if(result.ok){
            out << "VALID\npackage_id=" << result.packageId << "\nitem_key=" << result.itemKey << "\n";
            return 0;
        }
        err << "INVALID\n";
        for(const auto& e:result.errors) err << "- " << e << "\n";
        return 1;
    }
    if(cmd=="ingest-pending"&&args.size()==3){
        archive::RecoveryImporter importer(config,store,logger);
        QStringList failures;
        const auto accepted=importer.ingestPending(&failures);
        out << "accepted=" << accepted << "\n";
        for(const auto& f:failures) err << "- " << f << "\n";
        return failures.isEmpty()?0:1;
    }
    if(cmd=="scan"&&(args.size()==4||args.size()==5)){
        archive::Source source;
        source.url=args.at(3);
        source.key=archive::sourceKeyFromUrl(source.url);
        source.title=args.size()==5?args.at(4):source.key;
        source.addedAt=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        auto sources=store.loadSources();
        bool found=false;
        for(auto& s:sources){
            if(s.key==source.key){
                source=s;
                source.url=args.at(3);
                if(args.size()==5) source.title=args.at(4);
                found=true;
                break;
            }
        }
        if(!found){sources.append(source);store.saveSources(sources);}
        archive::PlaylistDiscovery discovery(config,logger);
        auto snapshot=discovery.discover(source);
        auto result=store.reconcile(source,snapshot,&logger);
        out << "complete=" << (snapshot.complete?"true":"false") << "\nobserved=" << result.observed
            << "\nactive=" << result.active << "\nremoved=" << result.removed << "\nunavailable=" << result.unavailable << "\n";
        if(!result.committed){err << result.error << "\n";return 1;}
        return snapshot.complete?0:3;
    }
    if(cmd=="sync-item"&&args.size()==4){
        const QString url=args.at(3);
        const QString id=videoIdFromUrl(url);
        const QString key="youtube:"+id;
        auto items=store.loadCanonicalItems();
        int index=-1;
        for(int i=0;i<items.size();++i){if(items.at(i).key==key){index=i;break;}}
        if(index<0){
            archive::CanonicalItem item;
            item.key=key;
            item.provider="youtube";
            item.providerId=id;
            item.title=id;
            item.originalUrl=url;
            item.availability="public";
            item.firstSeen=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
            item.lastSeen=item.firstSeen;
            items.append(item);
            index=items.size()-1;
            QString saveError;
            if(!store.saveCanonicalItems(items,&saveError)){
                err << "Unable to create canonical smoke item: " << saveError << "\n";
                return 1;
            }
        }else{
            items[index].originalUrl=url;
            items[index].availability="public";
            if(items[index].providerId.isEmpty()) items[index].providerId=id;
            QString saveError;
            if(!store.saveCanonicalItems(items,&saveError)){
                err << "Unable to update canonical smoke item: " << saveError << "\n";
                return 1;
            }
        }
        archive::MediaExecutor executor(config,store,logger);
        QString syncError;
        if(!executor.syncItem(items.at(index),true,true,&syncError)){
            err << "sync=FAIL item_key=" << key << " error=" << syncError << "\n";
            return 1;
        }
        const auto finalItems=store.loadCanonicalItems();
        for(const auto& item:finalItems){
            if(item.key!=key) continue;
            out << "sync=PASS\nitem_key=" << key << "\nvideo_state=" << item.video.state << "\nvideo_path=" << item.video.path
                << "\naudio_state=" << item.audio.state << "\naudio_path=" << item.audio.path << "\n";
            return item.video.state=="complete"&&item.audio.state=="complete"?0:1;
        }
        err << "sync=FAIL canonical item disappeared after execution\n";
        return 1;
    }
    usage(err);
    return 2;
}
