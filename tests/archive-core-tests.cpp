#include "../src/archive/archivecore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include <cstdlib>

using namespace archive;

namespace
{
void fail(const QString& message)
{
    QTextStream(stderr) << "FAIL: " << message << "\n";
    std::exit(1);
}
void check(bool condition,const QString& message){if(!condition)fail(message);}
void writeFile(const QString& path,const QByteArray& data)
{
    QDir().mkpath(QFileInfo(path).absolutePath()); QFile f(path); check(f.open(QIODevice::WriteOnly),"open "+path); f.write(data); f.close();
}
QByteArray readFile(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return f.readAll();}
CanonicalItem byKey(const QVector<CanonicalItem>& items,const QString& key)
{
    for(const auto& i:items)if(i.key==key)return i; fail("missing canonical "+key); return {};
}
PlaylistItem byPlaylistKey(const QVector<PlaylistItem>& items,const QString& key)
{
    for(const auto& i:items)if(i.itemKey==key)return i; fail("missing playlist item "+key); return {};
}
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    QTemporaryDir temp; check(temp.isValid(),"temporary directory");
    Paths paths(temp.path()); Store store(paths); QString error;
    check(store.initialize(&error),"initialize: "+error);
    check(QFileInfo::exists(QDir(temp.path()).filePath("ARCHIVE_AGENT.md")),"agent contract materialized");
    check(QFileInfo::exists(QDir(paths.schemas()).filePath("recovery-package.schema.json")),"schema materialized");
    check(paths.isSafeRelative("Video/test.mp4"),"relative path accepted");
    check(!paths.isSafeRelative("../escape.mp4"),"path traversal rejected");
    check(!paths.isSafeRelative("C:/Music/test.mp4"),"Windows absolute path rejected");
    check(!paths.isSafeRelative("/tmp/test.mp4"),"POSIX absolute path rejected");

    Source source; source.key="PLTEST123"; source.url="https://www.youtube.com/playlist?list=PLTEST123"; source.title="Songs"; source.addedAt=QDateTime::currentDateTime().toString(Qt::ISODate);
    check(store.saveSources({source},&error),"save sources: "+error);
    auto round=store.loadSources(&error); check(round.size()==1&&round[0].key==source.key,"source registry round-trip");

    // Parsed observations from a nonzero yt-dlp run are useful evidence, but never a complete snapshot.
    const QByteArray partialJson=R"({"entries":[{"id":"abc123DEF45","title":"Observed before failure","playlist_index":1,"url":"https://www.youtube.com/watch?v=abc123DEF45","availability":"public"}]})";
    const auto parsedNonzero=PlaylistDiscovery::parse(source,partialJson,"extractor failed after partial output",1);
    check(parsedNonzero.items.size()==1,"nonzero discovery retains observed entries");
    check(!parsedNonzero.complete,"nonzero discovery cannot be complete");
    check(parsedNonzero.error.contains("removal inference disabled"),"nonzero discovery records removal-safety reason");

    Snapshot first; first.sourceKey=source.key; first.complete=true; first.scannedAt="2026-09-14T10:00:00+05:30";
    PlaylistItem a; a.providerId="abc123DEF45"; a.position=1; a.title="Unique edit"; a.url="https://www.youtube.com/watch?v=abc123DEF45"; a.availability="public"; a.itemKey=canonicalKey(a.providerId,source.key,a.position,a.title);
    PlaylistItem b; b.providerId="xyz987QWE65"; b.position=2; b.title="Lost edit"; b.url="https://www.youtube.com/watch?v=xyz987QWE65"; b.availability="public"; b.itemKey=canonicalKey(b.providerId,source.key,b.position,b.title);
    first.items={a,b};
    auto s=store.reconcile(source,first); check(s.committed&&s.active==2&&s.removed==0,"initial complete reconciliation");
    check(store.loadCanonicalItems().size()==2,"canonical identity created");

    // Simulate interrupted work and verify it survives state round-trip.
    Representation interrupted; interrupted.state="interrupted"; interrupted.origin="automatic_download"; interrupted.error="process interrupted";
    check(store.updateRepresentation(a.itemKey,"video",interrupted,&error),"write interrupted representation");
    check(byKey(store.loadCanonicalItems(),a.itemKey).video.state=="interrupted","interrupted representation retained");

    // A partial/429-like snapshot may observe only A but must not remove B.
    Snapshot partial; partial.sourceKey=source.key; partial.complete=false; partial.scannedAt="2026-09-14T10:10:00+05:30"; partial.error="HTTP 429"; partial.items={a};
    s=store.reconcile(source,partial); check(s.committed&&s.removed==0,"partial snapshot cannot infer removal");
    check(byPlaylistKey(store.loadPlaylistItems(source.key),b.itemKey).membership=="active","missing item retained active after partial scan");

    // A complete snapshot without B marks B removed.
    Snapshot complete; complete.sourceKey=source.key; complete.complete=true; complete.scannedAt="2026-09-14T10:20:00+05:30"; complete.items={a};
    s=store.reconcile(source,complete); check(s.committed&&s.removed==1,"complete snapshot infers removal");
    check(byPlaylistKey(store.loadPlaylistItems(source.key),b.itemKey).membership=="removed","removed membership persisted");

    // B reappears, preserving historical identity and generating reappearance state.
    Snapshot reappear; reappear.sourceKey=source.key; reappear.complete=true; reappear.scannedAt="2026-09-14T10:30:00+05:30"; reappear.items={a,b};
    s=store.reconcile(source,reappear); check(s.committed&&s.reappeared==1,"removed item reappears");
    check(byPlaylistKey(store.loadPlaylistItems(source.key),b.itemKey).membership=="active","reappeared item active");
    const auto history=QString::fromUtf8(readFile(paths.playlistHistoryFile(source.key)));
    check(history.contains("membership_reappeared"),"reappearance history recorded");

    // Deleted missing media becomes a recovery candidate.
    b.availability="deleted"; b.title="[Deleted video]";
    Snapshot deleted; deleted.sourceKey=source.key; deleted.complete=true; deleted.scannedAt="2026-09-14T10:40:00+05:30"; deleted.items={a,b};
    s=store.reconcile(source,deleted); check(s.unavailable==1,"deleted availability counted");
    check(byKey(store.loadCanonicalItems(),b.itemKey).recoveryStatus=="unrecovered","deleted missing item marked unrecovered");

    // Same source ID in another playlist reuses canonical identity.
    Source source2; source2.key="PLSECOND"; source2.url="https://www.youtube.com/playlist?list=PLSECOND"; source2.title="Second"; source2.addedAt=source.addedAt;
    auto sources=store.loadSources(); sources.append(source2); store.saveSources(sources);
    PlaylistItem cross=a; cross.position=4; cross.itemKey=canonicalKey(cross.providerId,source2.key,cross.position,cross.title);
    check(cross.itemKey==a.itemKey,"canonical key independent of playlist");
    Snapshot second; second.sourceKey=source2.key; second.complete=true; second.items={cross};
    store.reconcile(source2,second); check(store.loadCanonicalItems().size()==2,"cross-playlist item does not duplicate canonical identity");

    // Projections are generated and relative.
    check(QFileInfo::exists(QDir(paths.sourceDir(source.key)).filePath("catalog.csv")),"catalog projection");
    check(QFileInfo::exists(QDir(paths.sourceDir(source.key)).filePath("missing.csv")),"missing projection");
    check(QFileInfo::exists(QDir(paths.sourceDir(source.key)).filePath("video.m3u8")),"video m3u8 projection");

    // Recovery Package validation accepts package-relative media and rejects absolute paths.
    const auto pending=QDir(paths.importsPending()).filePath("pkg-good"); QDir().mkpath(QDir(pending).filePath("files")); writeFile(QDir(pending).filePath("files/video.bin"),"x");
    QJsonObject manifest{{"schema_version",1},{"package_id","pkg-good"},
        {"target",QJsonObject{{"item_key",b.itemKey},{"youtube_id",b.providerId}}},
        {"provenance",QJsonObject{{"method","old_local_backup"},{"confidence","high"}}},
        {"representations",QJsonObject{{"video",QJsonObject{{"file","files/video.bin"}}}}}};
    writeFile(QDir(pending).filePath("manifest.json"),QJsonDocument(manifest).toJson());
    RuntimeConfig config{temp.path(),QCoreApplication::applicationDirPath()}; ActivityLogger logger(paths); RecoveryImporter importer(config,store,logger);
    auto vr=importer.validate(pending); check(vr.ok,"valid Recovery Package: "+vr.errors.join("; "));
    manifest["representations"]=QJsonObject{{"video",QJsonObject{{"file","C:/escape/video.mp4"}}}}; writeFile(QDir(pending).filePath("manifest.json"),QJsonDocument(manifest).toJson());
    vr=importer.validate(pending); check(!vr.ok,"absolute Recovery Package path rejected");

    // Activity logs retain no more than seven date directories. Seed old days then trigger pruning.
    for(int i=1;i<=9;++i) QDir().mkpath(QDir(paths.activityLogs()).filePath(QString("2026-08-%1").arg(i,2,10,QChar('0'))));
    logger.event("INFO","test","retention_test");
    check(QDir(paths.activityLogs()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).size()<=7,"activity retention <= 7 days");

    // Diagnostic logging rotates around 10 MiB total.
    const QString payload(220000,'x'); for(int i=0;i<60;++i) logger.diagnostic(payload);
    qint64 diag=0; for(const auto& f:QDir(paths.diagnosticLogs()).entryInfoList(QDir::Files)) diag+=f.size();
    check(diag<=11LL*1024*1024,"diagnostic logs bounded near 10 MiB");

    QTextStream(stdout) << "archive-core-tests: PASS\n";
    return 0;
}
