#include "archivecore.h"

#include <QCoreApplication>
#include <QDir>
#include <QTextStream>

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTextStream out(stdout),err(stderr);
    const auto args=app.arguments();
    if(args.size()<3){
        err << "Usage:\n"
               "  archive-cli validate <archive-root> <package-dir>\n"
               "  archive-cli ingest-pending <archive-root>\n"
               "  archive-cli scan <archive-root> <playlist-url> [display-name]\n";
        return 2;
    }
    archive::RuntimeConfig config; config.archiveRoot=QDir::cleanPath(args.at(2)); config.appDir=QCoreApplication::applicationDirPath();
    archive::Paths paths(config.archiveRoot); archive::Store store(paths); QString initError;
    if(!store.initialize(&initError)){err << "Archive initialization failed: " << initError << "\n";return 1;}
    archive::ActivityLogger logger(paths);

    const auto cmd=args.at(1);
    if(cmd=="validate" && args.size()==4){
        archive::RecoveryImporter importer(config,store,logger); const auto result=importer.validate(QDir::cleanPath(args.at(3)));
        if(result.ok){out << "VALID\npackage_id=" << result.packageId << "\nitem_key=" << result.itemKey << "\n";return 0;}
        err << "INVALID\n"; for(const auto& e:result.errors)err << "- " << e << "\n";return 1;
    }
    if(cmd=="ingest-pending" && args.size()==3){
        archive::RecoveryImporter importer(config,store,logger); QStringList failures; const auto accepted=importer.ingestPending(&failures);
        out << "accepted=" << accepted << "\n"; for(const auto& f:failures)err << "- " << f << "\n"; return failures.isEmpty()?0:1;
    }
    if(cmd=="scan" && (args.size()==4||args.size()==5)){
        archive::Source source; source.url=args.at(3); source.key=archive::sourceKeyFromUrl(source.url); source.title=args.size()==5?args.at(4):source.key; source.addedAt=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
        auto sources=store.loadSources(); bool found=false; for(auto& s:sources)if(s.key==source.key){source=s;source.url=args.at(3);if(args.size()==5)source.title=args.at(4);found=true;break;} if(!found){sources.append(source);store.saveSources(sources);}
        archive::PlaylistDiscovery discovery(config,logger); auto snapshot=discovery.discover(source); auto result=store.reconcile(source,snapshot,&logger);
        out << "complete=" << (snapshot.complete?"true":"false") << "\nobserved=" << result.observed << "\nactive=" << result.active << "\nremoved=" << result.removed << "\nunavailable=" << result.unavailable << "\n";
        if(!result.committed){err << result.error << "\n";return 1;} return snapshot.complete?0:3;
    }
    err << "Unsupported command. Run archive-cli without enough arguments for usage.\n";return 2;
}
