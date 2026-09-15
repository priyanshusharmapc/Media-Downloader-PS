#include "../src/archive/archivecore.h"
#include "../src/archive/archivesafety.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <QFileInfo>
#include <functional>
#include <stdexcept>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace archive;
static void require(bool ok,const QString& message){if(!ok)throw std::runtime_error(message.toStdString());}
static QString testTempTemplate(){return QDir(qEnvironmentVariable("ARCHIVE_TEST_TMP",QDir::tempPath())).filePath("archive-hardening-XXXXXX");}
static void put(const QString& path,const QByteArray& bytes){QDir().mkpath(QFileInfo(path).absolutePath());QFile f(path);require(f.open(QIODevice::WriteOnly),"write "+path);require(f.write(bytes)==bytes.size(),"short write");}
static QByteArray get(const QString& path){QFile f(path);require(f.open(QIODevice::ReadOnly),"read "+path);return f.readAll();}
static quint32 lastLinkError=0;
static bool makeDirectoryLink(const QString& target,const QString& link)
{
#ifdef Q_OS_WIN
 const auto created=CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),reinterpret_cast<LPCWSTR>(target.utf16()),0x3);
 lastLinkError=created?0:static_cast<quint32>(GetLastError());return created!=0;
#else
 return QFile::link(target,link);
#endif
}
struct Fixture {
 QTemporaryDir tmp{testTempTemplate()}; Paths paths{tmp.path()}; Store store{paths}; ActivityLogger logger{paths};
 RuntimeConfig config{tmp.path(),QCoreApplication::applicationDirPath()};
 Source source; PlaylistItem item;
 Fixture(){QString e;require(tmp.isValid()&&store.initialize(&e),e);source.key="PLAUDIT";source.url="https://www.youtube.com/playlist?list=PLAUDIT";source.title="Audit";item.providerId="abc123DEF45";item.itemKey="youtube:"+item.providerId;item.position=1;item.title="Historical title";item.availability="public";item.url="https://www.youtube.com/watch?v="+item.providerId;Snapshot s;s.sourceKey=source.key;s.complete=true;s.items={item};require(store.reconcile(source,s).committed,"fixture reconcile");}
 QString package(const QJsonObject& changes={}){const auto dir=QDir(paths.importsPending()).filePath("audit-package");QJsonObject o{{"schema_version",1},{"package_id","audit-package"},{"target",QJsonObject{{"item_key",item.itemKey},{"youtube_id",item.providerId}}},{"provenance",QJsonObject{{"method","old_local_backup"},{"confidence","high"}}},{"metadata",QJsonObject{{"canonical_title","Recovered title"}}}};for(auto i=changes.begin();i!=changes.end();++i)o[i.key()]=i.value();put(QDir(dir).filePath("manifest.json"),QJsonDocument(o).toJson());return dir;}
};
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 2;const QString name=app.arguments().at(1);try{
#ifdef Q_OS_WIN
 const QStringList linkCases{"root-symlink","parent-symlink","linked-state","linked-media","linked-package-file"};
 if(linkCases.contains(name)){
  QTemporaryDir probe(testTempTemplate());const auto target=QDir(probe.path()).filePath("target");const auto link=QDir(probe.path()).filePath("link");QDir().mkpath(target);
  if(!makeDirectoryLink(target,link)&&lastLinkError==1314u){QTextStream(stdout)<<"SKIP: symbolic-link privilege unavailable\n";return 77;}
 }
#endif
 if(name=="discovery-shape"){Source s;auto r=PlaylistDiscovery::parse(s,"{}","",0);require(!r.complete,"non-playlist JSON must not be complete");}
 else if(name=="discovery-null"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[null]})","",0);require(!r.complete,"null entries must prevent removal inference");}
 else if(name=="discovery-count"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[],"playlist_count":3})","",0);require(!r.complete,"truncated playlist must be partial");}
 else if(name=="discovery-error"){Source s;auto r=PlaylistDiscovery::parse(s,R"({"entries":[]})","ERROR: failed to fetch page",0);require(!r.complete,"ignore-errors cannot authorize removals");}
  else if(name=="portable-paths"){Fixture f;for(const auto& p:QStringList{"C:escape","Video/../State/items.json","Video\\..\\escape","Video/NUL.mp4","Video/x:stream","Video/trailing. ","Video//x","Video/./x"})require(!f.paths.isSafeRelative(p),"unsafe portable path accepted: "+p);require(f.paths.isSafeRelative("Video/a file [abc123DEF45].mp4"),"normal path rejected");}
  else if(name=="reserved-device-variants"){Fixture f;for(const auto& p:QStringList{"Video/CON.txt","Video/com1.mp4","Video/COM¹.mp4","Video/COM².mp4","Video/COM³.mp4","Video/LPT¹.txt"})require(!f.paths.isSafeRelative(p),"reserved device name accepted: "+p);}
  else if(name=="reparse-policy"){require(detail::isPermittedCloudFilesTag(0x9000601Au),"Cloud Files family rejected");require(!detail::isPermittedCloudFilesTag(0xA000000Cu),"symbolic-link tag permitted");require(!detail::isPermittedCloudFilesTag(0xA0000003u),"junction tag permitted");require(!detail::isPermittedCloudFilesTag(0x12345678u),"unknown tag permitted");}
  else if(name=="root-symlink"){QTemporaryDir base,target;require(base.isValid()&&target.isValid(),"temporary roots");const auto link=QDir(base.path()).filePath("root-link");require(makeDirectoryLink(target.path(),link),"create root symbolic link");Paths p(link);QString e;require(!p.ensureLayout(&e),"symbolic-link Archive Root accepted");}
  else if(name=="parent-symlink"){QTemporaryDir base,target;require(base.isValid()&&target.isValid(),"temporary roots");const auto link=QDir(base.path()).filePath("parent-link");require(makeDirectoryLink(target.path(),link),"create parent symbolic link");Paths p(QDir(link).filePath("archive"));QString e;require(!p.ensureLayout(&e),"symbolic-link Archive Root parent accepted");}
  else if(name=="linked-state"){Fixture f;const auto outside=QDir(f.tmp.path()).filePath("outside-state");QDir().mkpath(outside);QDir(f.paths.state()).removeRecursively();require(makeDirectoryLink(outside,f.paths.state()),"create linked State directory");QString e;require(!f.paths.ensureLayout(&e),"linked State directory accepted");}
  else if(name=="linked-media"){Fixture f;const auto outside=QDir(f.tmp.path()).filePath("outside-video");QDir().mkpath(outside);QDir(f.paths.video()).removeRecursively();require(makeDirectoryLink(outside,f.paths.video()),"create linked Video directory");QString e;require(!f.paths.ensureLayout(&e),"linked Video directory accepted");}
  else if(name=="placeholder-position"){require(canonicalKey({},"PLAUDIT",1,"Unresolved")==canonicalKey({},"PLAUDIT",99,"Unresolved"),"placeholder identity depends on mutable position");}
  else if(name=="placeholder-reorder"){
   Fixture f;Source source;source.key="PLUNKNOWN";source.url="https://www.youtube.com/playlist?list=PLUNKNOWN";source.title="Unknown";Snapshot first;first.sourceKey=source.key;first.complete=true;
   for(int position:QList<int>{3,4}){PlaylistItem item;item.title="Same unresolved title";item.url="https://example.invalid/unresolved";item.position=position;item.itemKey=canonicalKey({},source.key,position,item.title);item.availability="public";first.items.append(item);}
   require(f.store.reconcile(source,first).committed,"initial unresolved reconciliation failed");QString e;const auto before=f.store.loadPlaylistItems(source.key,&e);require(e.isEmpty()&&before.size()==2,"initial unresolved occurrences missing");
   Snapshot reordered;reordered.sourceKey=source.key;reordered.complete=true;
   for(int position:QList<int>{1,2}){PlaylistItem item;item.title="Same unresolved title";item.url="https://example.invalid/unresolved";item.position=position;item.itemKey=canonicalKey({},source.key,position,item.title);item.availability="public";reordered.items.append(item);}
   require(f.store.reconcile(source,reordered).committed,"reordered unresolved reconciliation failed");const auto after=f.store.loadPlaylistItems(source.key,&e);require(e.isEmpty()&&after.size()==2,"reordered unresolved occurrences missing");
   QSet<QString> beforeKeys,afterKeys;for(const auto& item:before)beforeKeys.insert(item.itemKey);for(const auto& item:after)afterKeys.insert(item.itemKey);require(beforeKeys==afterKeys,"unresolved reorder changed canonical identities");
  }
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
   Fixture f;QTemporaryDir outside;const auto p=f.package({{"representations",QJsonObject{{"video",QJsonObject{{"file","linked.mp4"}}}}}});
#ifdef Q_OS_WIN
   const auto target=QDir::toNativeSeparators(outside.filePath("secret.mp4"));
   const auto link=QDir::toNativeSeparators(QDir(p).filePath("linked.mp4"));
   const auto created=CreateSymbolicLinkW(reinterpret_cast<LPCWSTR>(link.utf16()),reinterpret_cast<LPCWSTR>(target.utf16()),0x2);
   const auto error=GetLastError();
   require(created!=0,QString("make symlink error=%1 target=%2 link=%3").arg(error).arg(target).arg(link));
   require(QFileInfo(link).isSymLink(),"package-side symlink missing");
#else
   require(QFile::link(outside.filePath("secret.mp4"),QDir(p).filePath("linked.mp4")),"make symlink");
#endif
   RecoveryImporter i(f.config,f.store,f.logger);require(!i.validate(p).ok,"symlink escaped package boundary");
  }
 else {QTextStream(stderr)<<"Unknown case\n";return 2;}
 QTextStream(stdout)<<name<<": PASS\n";return 0;
 }catch(const std::exception& e){QTextStream(stderr)<<name<<": FAIL: "<<e.what()<<"\n";return 1;}}
