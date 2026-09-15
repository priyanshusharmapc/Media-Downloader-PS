#include "../src/archive/archivecore.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <functional>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace archive;
static void require(bool ok,const QString& message){if(!ok)throw std::runtime_error(message.toStdString());}
static void put(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);require(f.open(QIODevice::WriteOnly),"write "+path);require(f.write(bytes)==bytes.size(),"short write");}
static QByteArray get(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"read "+path);return f.readAll();}
struct Fixture {
 QTemporaryDir tmp; Paths paths{tmp.path()}; Store store{paths}; ActivityLogger logger{paths};
 RuntimeConfig config{tmp.path(),QCoreApplication::applicationDirPath()};
 Source source; PlaylistItem item;
 Fixture(){QString e;require(tmp.isValid()&&store.initialize(&e),e);source.key="PLAUDIT";source.url="https://www.youtube.com/playlist?list=PLAUDIT";source.title="Audit";item.providerId="abc123DEF45";item.itemKey="youtube:"+item.providerId;item.position=1;item.title="Historical title";item.availability="public";item.url="https://www.youtube.com/watch?v="+item.providerId;Snapshot s;s.sourceKey=source.key;s.complete=true;s.items={item};require(store.reconcile(source,s).committed,"fixture reconcile");}
 QString package(const QJsonObject& changes={}){const auto dir=QDir(paths.importsPending()).filePath("audit-package");QJsonObject o{{"schema_version",1},{"package_id","audit-package"},{"target",QJsonObject{{"item_key",item.itemKey},{"youtube_id",item.providerId}}},{"provenance",QJsonObject{{"method","old_local_backup"},{"confidence","high"}}},{"metadata",QJsonObject{{"canonical_title","Recovered title"}}}};for(auto i=changes.begin();i!=changes.end();++i)o[i.key()]=i.value();put(QDir(dir).filePath("manifest.json"),QJsonDocument(o).toJson());return dir;}
};
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;const QString name=app.arguments().at(1);try{
 if(name=="discovery-shape"){Source s;auto r=PlaylistDiscovery::parse(s,"{}","",0);require(!r.complete,"non-playlist JSON must not be complete");}
 else if(name=="discovery-null"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[null]})","",0);require(!r.complete,"null entries must prevent removal inference");}
 else if(name=="discovery-count"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[],"playlist_count":3})","",0);require(!r.complete,"truncated playlist must be partial");}
 else if(name=="discovery-error"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[]})","ERROR: failed to fetch page",0);require(!r.complete,"ignore-errors cannot authorize removals");}
 else if(name=="portable-paths"){Fixture f;for(const auto& p:QStringList{"C:escape","Video/../State/items.json","Video\\..\\escape","Video/NUL.mp4","Video/x:stream","Video/trailing. ","Video//x","Video/./x"})require(!f.paths.isSafeRelative(p),"unsafe portable path accepted: "+p);require(f.paths.isSafeRelative("Video/a file [abc123DEF45].mp4"),"normal path rejected");}
 else if(name=="source-traversal"){Fixture f;Source s=f.source;s.key="..";Snapshot snap;snap.sourceKey=s.key;snap.complete=true;require(!f.store.reconcile(s,snap).committed,"unsafe source key must fail");}
 else if(name=="state-shape"){Fixture f;put(f.paths.itemsFile(),"{\"unexpected\":true}");QString e;f.store.loadCanonicalItems(&e);require(!e.isEmpty(),"wrong JSON root must be an error");const auto before=get(f.paths.itemsFile());Snapshot s;s.sourceKey=f.source.key;s.items={f.item};require(!f.store.reconcile(f.source,s).committed,"corrupt store must block writes");require(get(f.paths.itemsFile())==before,"corrupt state was overwritten");}
 else if(name=="source-state-corruption"){Fixture f;put(f.paths.sourcesFile(),"not json");Snapshot s;s.sourceKey=f.source.key;s.items={f.item};require(!f.store.reconcile(f.source,s).committed,"corrupt sources must block reconciliation");require(get(f.paths.sourcesFile())=="not json","source registry destroyed");}
 else if(name=="snapshot-identity"){Fixture f;Snapshot s;s.sourceKey="PLWRONG";s.complete=true;require(!f.store.reconcile(f.source,s).committed,"cross-source snapshot accepted");}
 else if(name=="import-identity"){Fixture f;const auto p=f.package({{"target",QJsonObject{{"item_key",f.item.itemKey},{"youtube_id","xyz987QWE65"}}}});RecoveryImporter i(f.config,f.store,f.logger);require(!i.validate(p).ok,"conflicting identities accepted");}
 else if(name=="import-id-only"){Fixture f;const auto p=f.package({{"target",QJsonObject{{"youtube_id",f.item.providerId}}}});RecoveryImporter i(f.config,f.store,f.logger);require(i.validate(p).ok,"documented youtube_id-only target rejected");}
 else if(name=="import-schema"){Fixture f;const auto p=f.package({{"package_id","../unsafe"},{"provenance",QJsonObject{{"method","old_local_backup"},{"confidence","certain"}}}});RecoveryImporter i(f.config,f.store,f.logger);require(!i.validate(p).ok,"unsafe package/schema accepted");}
 else if(name=="partial-recovery-status"){Fixture f;f.item.availability="deleted";Snapshot s;s.sourceKey=f.source.key;s.items={f.item};require(f.store.reconcile(f.source,s).committed,"deleted reconcile");Representation r;r.state="complete";r.path="Video/test.mp4";require(f.store.updateRepresentation(f.item.itemKey,"video",r),"update");require(f.store.loadCanonicalItems().first().recoveryStatus!="not_required","audio still missing but recovery cleared");}
 else if(name=="nested-redaction"){Fixture f;f.logger.event("INFO","test","redaction",{{"nested",QJsonObject{{"token","do-not-leak"}}}});const auto day=QDir(f.paths.activityLogs()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).last();const auto dir=QDir(f.paths.activityLogs()).filePath(day);const auto file=QDir(dir).entryList(QDir::Files).last();require(!get(QDir(dir).filePath(file)).contains("do-not-leak"),"nested credential leaked");}
 else if(name=="deleted-title"){Fixture f;auto p=f.item;p.title="[Deleted video]";p.availability="deleted";Snapshot s;s.sourceKey=f.source.key;s.items={p};require(f.store.reconcile(f.source,s).committed,"deleted reconcile");require(get(QDir(f.paths.sourceDir(f.source.key)).filePath("missing.csv")).contains("Historical title"),"recovery report lost known title");}
  else if(name=="linked-package-file"){
   Fixture f;QTemporaryDir outside;put(outside.filePath("secret.mp4"),"secret");const auto p=f.package({{"representations",QJsonObject{{"video",QJsonObject{{"file","linked.mp4"}}}}}});
#ifdef Q_OS_WIN
   const auto target=QDir::toNativeSeparators(outside.filePath("secret.mp4"));
   const auto link=QDir::toNativeSeparators(QDir(p).filePath("linked.mp4"));
   const auto created=CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(target.utf16()),reinterpret_cast<LPCWSTR>(link.utf16()),0x2);
   const auto error=GetLastError();
   require(created!=0,QString("make symlink error=%1 target=%2 link=%3").arg(error).arg(target).arg(link));
#else
   require(QFile::link(outside.filePath("secret.mp4"),QDir(p).filePath("linked.mp4")),"make symlink");
#endif
   RecoveryImporter i(f.config,f.store,f.logger);require(!i.validate(p).ok,"symlink escaped package boundary");
  }
 else {QTextStream(stderr)<<"Unknown case\n";return 2;}
 QTextStream(stdout)<<name<<": PASS\n";return 0;
 }catch(const std::exception& e){QTextStream(stderr)<<name<<": FAIL: "<<e.what()<<"\n";return 1;}}
