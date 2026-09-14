#include "archivecore.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QRegularExpression>
#include <QResource>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTextStream>
#include <QThread>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>

namespace archive
{
namespace
{
QString nowIso()
{
    return QDateTime::currentDateTime().toString(Qt::ISODateWithMs);
}

QString localDate()
{
    return QDate::currentDate().toString(Qt::ISODate);
}

QByteArray readAll(const QString& path)
{
    QFile f(path);
    if(!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

bool atomicWrite(const QString& path,const QByteArray& data,QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile f(path);
    if(!f.open(QIODevice::WriteOnly)){
        if(error) *error=f.errorString();
        return false;
    }
    if(f.write(data)!=data.size()){
        if(error) *error=f.errorString();
        return false;
    }
    if(!f.commit()){
        if(error) *error=f.errorString();
        return false;
    }
    return true;
}

bool appendLine(const QString& path,const QByteArray& line,QString* error)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if(!f.open(QIODevice::WriteOnly|QIODevice::Append|QIODevice::Text)){
        if(error) *error=f.errorString();
        return false;
    }
    QByteArray out=line;
    if(!out.endsWith('\n')) out.append('\n');
    if(f.write(out)!=out.size()){
        if(error) *error=f.errorString();
        return false;
    }
    f.flush();
    return true;
}

QString csv(QString s)
{
    s.replace('"',"\"\"");
    return '"'+s+'"';
}

QString safeFilePart(QString s)
{
    s=s.trimmed();
    s.replace(QRegularExpression("[\\\\/:*?\"<>|\\x00-\\x1F]"),"_");
    while(s.endsWith('.')||s.endsWith(' ')) s.chop(1);
    if(s.isEmpty()) s="UNKNOWN";
    if(s.size()>120) s=s.left(120).trimmed();
    return s;
}

QJsonObject repToJson(const Representation& r)
{
    QJsonObject o;
    o["state"]=r.state;
    if(!r.path.isEmpty()) o["path"]=r.path;
    if(!r.origin.isEmpty()) o["origin"]=r.origin;
    if(!r.verifiedAt.isEmpty()) o["verified_at"]=r.verifiedAt;
    if(!r.error.isEmpty()) o["error"]=r.error;
    return o;
}

Representation repFromJson(const QJsonObject& o)
{
    Representation r;
    r.state=o.value("state").toString("missing");
    r.path=o.value("path").toString();
    r.origin=o.value("origin").toString();
    r.verifiedAt=o.value("verified_at").toString();
    r.error=o.value("error").toString();
    return r;
}

QJsonObject sourceToJson(const Source& s)
{
    QJsonObject o;
    o["key"]=s.key; o["url"]=s.url; o["title"]=s.title;
    o["added_at"]=s.addedAt; o["last_scan_at"]=s.lastScanAt;
    o["last_scan_status"]=s.lastScanStatus; o["last_error"]=s.lastError;
    return o;
}

Source sourceFromJson(const QJsonObject& o)
{
    Source s;
    s.key=o.value("key").toString(); s.url=o.value("url").toString(); s.title=o.value("title").toString();
    s.addedAt=o.value("added_at").toString(); s.lastScanAt=o.value("last_scan_at").toString();
    s.lastScanStatus=o.value("last_scan_status").toString("never"); s.lastError=o.value("last_error").toString();
    return s;
}

QJsonObject canonicalToJson(const CanonicalItem& i)
{
    QJsonObject o;
    o["key"]=i.key; o["provider"]=i.provider; o["provider_id"]=i.providerId;
    o["title"]=i.title; o["uploader"]=i.uploader; o["original_url"]=i.originalUrl;
    o["availability"]=i.availability; o["first_seen"]=i.firstSeen; o["last_seen"]=i.lastSeen;
    o["recovery_status"]=i.recoveryStatus; o["metadata_path"]=i.metadataPath;
    QJsonArray tags; for(const auto& t:i.userTags) tags.append(t); o["user_tags"]=tags;
    o["video"]=repToJson(i.video); o["audio"]=repToJson(i.audio);
    return o;
}

CanonicalItem canonicalFromJson(const QJsonObject& o)
{
    CanonicalItem i;
    i.key=o.value("key").toString(); i.provider=o.value("provider").toString("youtube");
    i.providerId=o.value("provider_id").toString(); i.title=o.value("title").toString();
    i.uploader=o.value("uploader").toString(); i.originalUrl=o.value("original_url").toString();
    i.availability=o.value("availability").toString("unknown"); i.firstSeen=o.value("first_seen").toString();
    i.lastSeen=o.value("last_seen").toString(); i.recoveryStatus=o.value("recovery_status").toString("not_required");
    i.metadataPath=o.value("metadata_path").toString();
    for(const auto& v:o.value("user_tags").toArray()) i.userTags.append(v.toString());
    i.video=repFromJson(o.value("video").toObject()); i.audio=repFromJson(o.value("audio").toObject());
    return i;
}

QJsonObject playlistItemToJson(const PlaylistItem& i)
{
    QJsonObject o;
    o["item_key"]=i.itemKey; o["provider_id"]=i.providerId; o["position"]=i.position;
    o["title"]=i.title; o["url"]=i.url; o["availability"]=i.availability; o["membership"]=i.membership;
    o["first_seen"]=i.firstSeen; o["last_seen"]=i.lastSeen; o["last_position"]=i.lastPosition;
    return o;
}

PlaylistItem playlistItemFromJson(const QJsonObject& o)
{
    PlaylistItem i;
    i.itemKey=o.value("item_key").toString(); i.providerId=o.value("provider_id").toString();
    i.position=o.value("position").toInt(-1); i.title=o.value("title").toString(); i.url=o.value("url").toString();
    i.availability=o.value("availability").toString("unknown"); i.membership=o.value("membership").toString("active");
    i.firstSeen=o.value("first_seen").toString(); i.lastSeen=o.value("last_seen").toString();
    i.lastPosition=o.value("last_position").toInt(i.position);
    return i;
}

template<typename T,typename F>
QJsonArray vectorToArray(const QVector<T>& v,F f)
{
    QJsonArray a; for(const auto& e:v) a.append(f(e)); return a;
}

QJsonDocument parseJson(const QByteArray& b,QString* error)
{
    QJsonParseError e;
    auto d=QJsonDocument::fromJson(b,&e);
    if(e.error!=QJsonParseError::NoError && error) *error=e.errorString();
    return d;
}

QString statusForRep(const Representation& r)
{
    if(r.state=="complete") return "Complete";
    if(r.state=="failed") return "Failed";
    if(r.state=="interrupted") return "Interrupted";
    if(r.state=="blocked_unavailable") return "Unavailable";
    if(r.state=="running") return "Running";
    return "Missing";
}

bool isUnavailable(const QString& a)
{
    return a=="deleted"||a=="private"||a=="unavailable"||a=="login_required"||a=="members_only"||a=="geo_blocked"||a=="copyright_blocked";
}

bool isTransientText(QString s)
{
    s=s.toLower();
    return s.contains("http error 429")||s.contains("too many requests")||s.contains("timed out")||
           s.contains("temporary failure")||s.contains("connection reset")||s.contains("network is unreachable")||
           s.contains("unable to download webpage")||s.contains("remote end closed");
}

QStringList sortedDirs(const QString& path)
{
    QDir d(path);
    return d.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name);
}

bool copyResource(const QString& resource,const QString& destination,QString* error)
{
    QFile in(resource);
    if(in.open(QIODevice::ReadOnly)) return atomicWrite(destination,in.readAll(),error);

    QString relative=resource;
    const QString prefix=":/archive/";
    if(relative.startsWith(prefix)) relative=relative.mid(prefix.size());
    const auto fallback=QDir(QCoreApplication::applicationDirPath()).filePath("archive-resources/"+relative);
    QFile external(fallback);
    if(external.open(QIODevice::ReadOnly)) return atomicWrite(destination,external.readAll(),error);

    if(error) *error=QString("Cannot read Archive resource %1 or packaged fallback %2").arg(resource,fallback);
    return false;
}

ProcessResult runProcess(const QString& program,const QStringList& args,const QString& cwd,int timeoutMs)
{
    ProcessResult r;
    if(program.isEmpty()){
        r.error="Required executable was not found";
        return r;
    }
    QProcess p;
    if(!cwd.isEmpty()) p.setWorkingDirectory(cwd);
    p.setProcessChannelMode(QProcess::SeparateChannels);
    p.start(program,args);
    if(!p.waitForStarted(10000)){
        r.error=p.errorString();
        return r;
    }
    if(!p.waitForFinished(timeoutMs)){
        p.kill(); p.waitForFinished(5000);
        r.standardOutput=QString::fromUtf8(p.readAllStandardOutput());
        r.standardError=QString::fromUtf8(p.readAllStandardError());
        r.error="Process timed out";
        return r;
    }
    r.exitCode=p.exitCode();
    r.standardOutput=QString::fromUtf8(p.readAllStandardOutput());
    r.standardError=QString::fromUtf8(p.readAllStandardError());
    r.ok=p.exitStatus()==QProcess::NormalExit && p.exitCode()==0;
    if(!r.ok) r.error=QString("Process exited with code %1").arg(p.exitCode());
    return r;
}

QJsonObject historyEvent(const QString& event,const QString& itemKey,const QJsonObject& detail={})
{
    QJsonObject o=detail;
    o["schema_version"]=1; o["timestamp"]=nowIso(); o["event"]=event; o["item_key"]=itemKey;
    return o;
}

QString sourceFolderKey(QString key)
{
    key.replace(QRegularExpression("[^A-Za-z0-9._-]"),"_");
    if(key.isEmpty()) key="unknown";
    return key;
}

void appendYtRuntimeArgs(QStringList& args,const ToolResolver& tools)
{
    const auto ffmpeg=tools.ffmpeg();
    if(!ffmpeg.isEmpty()) args << "--ffmpeg-location" << QFileInfo(ffmpeg).absolutePath();
    const auto deno=tools.deno();
    if(!deno.isEmpty()) args << "--js-runtimes" << ("deno:"+deno);
}

QStringList withoutDownloadArchive(QStringList args)
{
    for(int i=0;i<args.size();++i){
        if(args.at(i)=="--download-archive"){
            args.removeAt(i);
            if(i<args.size()) args.removeAt(i);
            break;
        }
    }
    const int urlIndex=args.isEmpty()?0:args.size()-1;
    args.insert(urlIndex,"--no-download-archive");
    return args;
}
}

Paths::Paths(QString root):m_root(QDir::cleanPath(QDir::fromNativeSeparators(std::move(root)))){}
QString Paths::root() const{return m_root;}
QString Paths::video() const{return QDir(m_root).filePath("Video");}
QString Paths::audio() const{return QDir(m_root).filePath("Audio");}
QString Paths::metadata() const{return QDir(m_root).filePath("Metadata");}
QString Paths::temp() const{return QDir(m_root).filePath("Temp");}
QString Paths::playlists() const{return QDir(m_root).filePath("Playlists");}
QString Paths::state() const{return QDir(m_root).filePath("State");}
QString Paths::archiveState() const{return QDir(state()).filePath("ArchiveMode");}
QString Paths::sourcesFile() const{return QDir(archiveState()).filePath("sources.json");}
QString Paths::itemsFile() const{return QDir(archiveState()).filePath("items.json");}
QString Paths::schemas() const{return QDir(archiveState()).filePath("Schemas");}
QString Paths::imports() const{return QDir(archiveState()).filePath("Imports");}
QString Paths::importsPending() const{return QDir(imports()).filePath("Pending");}
QString Paths::importsAccepted() const{return QDir(imports()).filePath("Accepted");}
QString Paths::importsRejected() const{return QDir(imports()).filePath("Rejected");}
QString Paths::activityLogs() const{return QDir(archiveState()).filePath("Logs/Activity");}
QString Paths::diagnosticLogs() const{return QDir(archiveState()).filePath("Logs/Diagnostic");}
QString Paths::sourceDir(const QString& sourceKey) const{return QDir(playlists()).filePath(sourceFolderKey(sourceKey));}
QString Paths::playlistFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("playlist.json");}
QString Paths::playlistItemsFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("items.json");}
QString Paths::playlistHistoryFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("history.jsonl");}

QString Paths::relativeToRoot(const QString& absolute) const
{
    const auto rel=QDir(m_root).relativeFilePath(QDir::cleanPath(QDir::fromNativeSeparators(absolute)));
    return QDir::fromNativeSeparators(rel);
}

QString Paths::absoluteFromRelative(const QString& relative) const
{
    if(!isSafeRelative(relative)) return {};
    return QDir::cleanPath(QDir(m_root).filePath(QDir::fromNativeSeparators(relative)));
}

bool Paths::isSafeRelative(const QString& p) const
{
    if(p.trimmed().isEmpty()) return false;
    QString n=QDir::fromNativeSeparators(p.trimmed());
    if(QDir::isAbsolutePath(n)) return false;
    if(QRegularExpression("^[A-Za-z]:/").match(n).hasMatch()) return false;
    if(n.startsWith("//")||n.startsWith("\\\\")) return false;
    n=QDir::cleanPath(n);
    if(n==".."||n.startsWith("../")||n.contains("/../")) return false;
    return n!=".";
}

bool Paths::ensureLayout(QString* error) const
{
    if(m_root.isEmpty()){
        if(error) *error="Archive root is empty";
        return false;
    }
    const QStringList dirs={video(),audio(),metadata(),temp(),playlists(),state(),archiveState(),schemas(),
                            importsPending(),importsAccepted(),importsRejected(),activityLogs(),diagnosticLogs()};
    for(const auto& d:dirs){
        if(!QDir().mkpath(d)){
            if(error) *error=QString("Unable to create %1").arg(d);
            return false;
        }
    }
    return true;
}

bool Paths::materializeAgentResources(QString* error) const
{
    if(!ensureLayout(error)) return false;
    struct Item{QString resource;QString destination;};
    const QVector<Item> items={
        {":/archive/ARCHIVE_AGENT.md",QDir(m_root).filePath("ARCHIVE_AGENT.md")},
        {":/archive/recovery-package.schema.json",QDir(schemas()).filePath("recovery-package.schema.json")},
        {":/archive/recovery-package.example.json",QDir(schemas()).filePath("recovery-package.example.json")}
    };
    for(const auto& i:items){
        if(QFileInfo::exists(i.destination)) continue;
        if(!copyResource(i.resource,i.destination,error)) return false;
    }
    return true;
}

ActivityLogger::ActivityLogger(Paths paths):m_paths(std::move(paths))
{
    m_sessionId=QString("session-%1-%2").arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmsszzz"),
        QString::number(QCoreApplication::applicationPid()));
    m_paths.ensureLayout();
}

QString ActivityLogger::redact(QString text) const
{
    const auto root=QDir::toNativeSeparators(m_paths.root());
    const auto rootForward=QDir::fromNativeSeparators(m_paths.root());
    if(!root.isEmpty()) text.replace(root,"<ARCHIVE_ROOT>",Qt::CaseInsensitive);
    if(!rootForward.isEmpty()) text.replace(rootForward,"<ARCHIVE_ROOT>",Qt::CaseInsensitive);
    text.replace(QRegularExpression("(?i)(cookie|authorization|token|password|secret)(\\s*[:=]\\s*)([^\\s,;]+)"),"\\1\\2<REDACTED>");
    return text;
}

void ActivityLogger::event(const QString& severity,const QString& category,const QString& name,const QJsonObject& details,const QString& sessionId)
{
    m_paths.ensureLayout();
    const auto dayDir=QDir(m_paths.activityLogs()).filePath(localDate());
    QDir().mkpath(dayDir);
    QJsonObject o;
    o["schema_version"]=1; o["timestamp"]=nowIso(); o["session_id"]=sessionId.isEmpty()?m_sessionId:sessionId;
    o["severity"]=severity; o["category"]=category; o["event"]=name;
    QJsonObject sanitized;
    for(auto it=details.begin();it!=details.end();++it){
        sanitized[it.key()]=it.value().isString()?QJsonValue(redact(it.value().toString())):it.value();
    }
    o["details"]=sanitized;
    appendLine(QDir(dayDir).filePath(m_sessionId+".jsonl"),QJsonDocument(o).toJson(QJsonDocument::Compact),nullptr);
    prune();
}

void ActivityLogger::diagnostic(const QString& line)
{
    m_paths.ensureLayout();
    const qint64 maxFile=2*1024*1024;
    const int count=5;
    QString current=QDir(m_paths.diagnosticLogs()).filePath("diagnostic-0.log");
    if(QFileInfo(current).size()>=maxFile){
        QFile::remove(QDir(m_paths.diagnosticLogs()).filePath(QString("diagnostic-%1.log").arg(count-1)));
        for(int i=count-2;i>=0;--i){
            const auto from=QDir(m_paths.diagnosticLogs()).filePath(QString("diagnostic-%1.log").arg(i));
            const auto to=QDir(m_paths.diagnosticLogs()).filePath(QString("diagnostic-%1.log").arg(i+1));
            if(QFileInfo::exists(from)){ QFile::remove(to); QFile::rename(from,to); }
        }
    }
    appendLine(current,QString("%1 %2").arg(nowIso(),redact(line)).toUtf8(),nullptr);
}

void ActivityLogger::prune()
{
    auto dirs=sortedDirs(m_paths.activityLogs());
    while(dirs.size()>7){
        QDir(QDir(m_paths.activityLogs()).filePath(dirs.takeFirst())).removeRecursively();
    }
}

Store::Store(Paths paths):m_paths(std::move(paths)){}
const Paths& Store::paths() const{return m_paths;}

bool Store::initialize(QString* error)
{
    if(!m_paths.ensureLayout(error)) return false;
    if(!m_paths.materializeAgentResources(error)) return false;
    if(!QFileInfo::exists(m_paths.sourcesFile()) && !atomicWrite(m_paths.sourcesFile(),"[]\n",error)) return false;
    if(!QFileInfo::exists(m_paths.itemsFile()) && !atomicWrite(m_paths.itemsFile(),"[]\n",error)) return false;
    return true;
}

QVector<Source> Store::loadSources(QString* error) const
{
    QVector<Source> v;
    if(!QFileInfo::exists(m_paths.sourcesFile())) return v;
    const auto d=parseJson(readAll(m_paths.sourcesFile()),error);
    if(!d.isArray()) return v;
    for(const auto& e:d.array()) if(e.isObject()) v.append(sourceFromJson(e.toObject()));
    return v;
}

bool Store::saveSources(const QVector<Source>& sources,QString* error) const
{
    return atomicWrite(m_paths.sourcesFile(),QJsonDocument(vectorToArray(sources,sourceToJson)).toJson(QJsonDocument::Indented),error);
}

QVector<CanonicalItem> Store::loadCanonicalItems(QString* error) const
{
    QVector<CanonicalItem> v;
    if(!QFileInfo::exists(m_paths.itemsFile())) return v;
    const auto d=parseJson(readAll(m_paths.itemsFile()),error);
    if(!d.isArray()) return v;
    for(const auto& e:d.array()) if(e.isObject()) v.append(canonicalFromJson(e.toObject()));
    return v;
}

bool Store::saveCanonicalItems(const QVector<CanonicalItem>& items,QString* error) const
{
    return atomicWrite(m_paths.itemsFile(),QJsonDocument(vectorToArray(items,canonicalToJson)).toJson(QJsonDocument::Indented),error);
}

QVector<PlaylistItem> Store::loadPlaylistItems(const QString& sourceKey,QString* error) const
{
    QVector<PlaylistItem> v;
    const auto path=m_paths.playlistItemsFile(sourceKey);
    if(!QFileInfo::exists(path)) return v;
    const auto d=parseJson(readAll(path),error);
    if(!d.isArray()) return v;
    for(const auto& e:d.array()) if(e.isObject()) v.append(playlistItemFromJson(e.toObject()));
    return v;
}

bool Store::savePlaylistItems(const QString& sourceKey,const QVector<PlaylistItem>& items,QString* error) const
{
    QDir().mkpath(m_paths.sourceDir(sourceKey));
    return atomicWrite(m_paths.playlistItemsFile(sourceKey),QJsonDocument(vectorToArray(items,playlistItemToJson)).toJson(QJsonDocument::Indented),error);
}

bool Store::savePlaylistMeta(const Source& source,QString* error) const
{
    QDir().mkpath(m_paths.sourceDir(source.key));
    return atomicWrite(m_paths.playlistFile(source.key),QJsonDocument(sourceToJson(source)).toJson(QJsonDocument::Indented),error);
}

bool Store::appendHistory(const QString& sourceKey,const QJsonObject& e,QString* error) const
{
    return appendLine(m_paths.playlistHistoryFile(sourceKey),QJsonDocument(e).toJson(QJsonDocument::Compact),error);
}

bool Store::updateRepresentation(const QString& itemKey,const QString& kind,const Representation& representation,QString* error)
{
    auto items=loadCanonicalItems(error);
    for(auto& item:items){
        if(item.key==itemKey){
            if(kind=="video") item.video=representation;
            else if(kind=="audio") item.audio=representation;
            else { if(error) *error="Unknown representation kind"; return false; }
            if(item.video.state=="complete"||item.audio.state=="complete") item.recoveryStatus="not_required";
            return saveCanonicalItems(items,error);
        }
    }
    if(error) *error="Canonical item not found";
    return false;
}

bool Store::updateCanonicalMetadata(const QString& itemKey,const QString& title,const QString& uploader,const QString& availability,const QString& originalUrl,QString* error)
{
    auto items=loadCanonicalItems(error);
    for(auto& item:items){
        if(item.key==itemKey){
            if(!title.isEmpty()) item.title=title;
            if(!uploader.isEmpty()) item.uploader=uploader;
            if(!availability.isEmpty()) item.availability=availability;
            if(!originalUrl.isEmpty()) item.originalUrl=originalUrl;
            item.lastSeen=nowIso();
            return saveCanonicalItems(items,error);
        }
    }
    if(error) *error="Canonical item not found";
    return false;
}

ReconcileSummary Store::reconcile(Source& source,const Snapshot& snapshot,ActivityLogger* logger)
{
    ReconcileSummary summary; summary.completeSnapshot=snapshot.complete; summary.observed=snapshot.items.size();
    QString error;
    if(!initialize(&error)){ summary.error=error; return summary; }

    auto prior=loadPlaylistItems(source.key,&error);
    if(!error.isEmpty()){ summary.error=error; return summary; }
    auto canonical=loadCanonicalItems(&error);
    if(!error.isEmpty()){ summary.error=error; return summary; }

    QHash<QString,int> priorIndex;
    for(int i=0;i<prior.size();++i) priorIndex[prior[i].itemKey]=i;
    QHash<QString,int> canonicalIndex;
    for(int i=0;i<canonical.size();++i) canonicalIndex[canonical[i].key]=i;

    QVector<PlaylistItem> result;
    QSet<QString> observedKeys;
    const auto scanTime=snapshot.scannedAt.isEmpty()?nowIso():snapshot.scannedAt;

    for(auto p:snapshot.items){
        if(p.itemKey.isEmpty()) p.itemKey=canonicalKey(p.providerId,source.key,p.position,p.title);
        observedKeys.insert(p.itemKey);
        const bool hadPrior=priorIndex.contains(p.itemKey);
        PlaylistItem previous;
        if(hadPrior) previous=prior[priorIndex.value(p.itemKey)];
        if(p.firstSeen.isEmpty()) p.firstSeen=hadPrior && !previous.firstSeen.isEmpty()?previous.firstSeen:scanTime;
        p.lastSeen=scanTime; p.lastPosition=p.position; p.membership="active";
        if(p.url.isEmpty() && !p.providerId.isEmpty()) p.url="https://www.youtube.com/watch?v="+p.providerId;
        if(hadPrior && previous.membership=="removed"){
            ++summary.reappeared;
            appendHistory(source.key,historyEvent("membership_reappeared",p.itemKey,{{"position",p.position}}),nullptr);
        }else if(!hadPrior){
            appendHistory(source.key,historyEvent("first_seen",p.itemKey,{{"position",p.position},{"title",p.title}}),nullptr);
        }

        int ci=canonicalIndex.value(p.itemKey,-1);
        if(ci<0){
            CanonicalItem c;
            c.key=p.itemKey; c.providerId=p.providerId; c.title=p.title; c.originalUrl=p.url;
            c.availability=p.availability; c.firstSeen=p.firstSeen; c.lastSeen=scanTime;
            if(isUnavailable(p.availability)) c.recoveryStatus="unrecovered";
            canonicalIndex[c.key]=canonical.size(); canonical.append(c); ci=canonical.size()-1;
        }else{
            auto& c=canonical[ci];
            if(!p.providerId.isEmpty()) c.providerId=p.providerId;
            if(!p.title.isEmpty() && !p.title.startsWith("[")) c.title=p.title;
            if(!p.url.isEmpty()) c.originalUrl=p.url;
            c.availability=p.availability; c.lastSeen=scanTime;
            if(c.firstSeen.isEmpty()) c.firstSeen=p.firstSeen;
            if(isUnavailable(p.availability) && c.video.state!="complete" && c.audio.state!="complete") c.recoveryStatus="unrecovered";
            if(c.video.state=="complete"||c.audio.state=="complete") c.recoveryStatus="not_required";
        }
        if(isUnavailable(p.availability)) ++summary.unavailable;
        result.append(p);
    }

    for(const auto& old:prior){
        if(observedKeys.contains(old.itemKey)) continue;
        auto p=old;
        if(snapshot.complete){
            if(p.membership!="removed"){
                p.membership="removed";
                appendHistory(source.key,historyEvent("membership_removed",p.itemKey,{{"last_position",p.lastPosition},{"title",p.title}}),nullptr);
                ++summary.removed;
            }
        }
        result.append(p);
    }

    std::sort(result.begin(),result.end(),[](const PlaylistItem& a,const PlaylistItem& b){
        const bool aa=a.membership=="active", bb=b.membership=="active";
        if(aa!=bb) return aa>bb;
        const int ap=a.position<0?INT_MAX:a.position, bp=b.position<0?INT_MAX:b.position;
        if(ap!=bp) return ap<bp;
        return a.itemKey<b.itemKey;
    });

    source.lastScanAt=scanTime;
    source.lastScanStatus=snapshot.complete?"complete":"partial";
    source.lastError=snapshot.error;
    auto sources=loadSources();
    bool sourceFound=false;
    for(auto& s:sources) if(s.key==source.key){s=source; sourceFound=true; break;}
    if(!sourceFound) sources.append(source);

    if(!saveCanonicalItems(canonical,&error)||!savePlaylistItems(source.key,result,&error)||!savePlaylistMeta(source,&error)||!saveSources(sources,&error)){
        summary.error=error; return summary;
    }
    if(!writeProjections(source.key,&error)){ summary.error=error; return summary; }

    for(const auto& p:result){
        if(p.membership=="active") ++summary.active;
        const int ci=canonicalIndex.value(p.itemKey,-1);
        if(ci>=0 && p.membership=="active" && !isUnavailable(p.availability)){
            if(canonical[ci].video.state!="complete") ++summary.needsVideo;
            if(canonical[ci].audio.state!="complete") ++summary.needsAudio;
        }
    }
    summary.committed=true;
    if(logger) logger->event("INFO","reconciliation","snapshot_reconciled",{
        {"source_key",source.key},{"complete",snapshot.complete},{"observed",summary.observed},{"active",summary.active},
        {"removed",summary.removed},{"reappeared",summary.reappeared},{"unavailable",summary.unavailable},
        {"needs_video",summary.needsVideo},{"needs_audio",summary.needsAudio}});
    return summary;
}

bool Store::writeProjections(const QString& sourceKey,QString* error) const
{
    const auto p=loadPlaylistItems(sourceKey,error);
    if(error && !error->isEmpty()) return false;
    const auto c=loadCanonicalItems(error);
    if(error && !error->isEmpty()) return false;
    QHash<QString,CanonicalItem> cm; for(const auto& i:c) cm[i.key]=i;
    QByteArray catalog="Position,Title,YouTube ID,Original URL,Membership,Availability,Video,Audio,Recovery Status,First Seen,Last Seen,Video Path,Audio Path\n";
    QByteArray missing="Last Position,Title,YouTube ID,Original URL,Membership,Availability,Video,Audio,Recovery Status,First Seen,Last Seen\n";
    QByteArray video="#EXTM3U\n", audio="#EXTM3U\n";
    QByteArray unavailable, removed;
    for(const auto& item:p){
        const auto canon=cm.value(item.itemKey);
        const QStringList cols={QString::number(item.position),item.title,item.providerId,item.url,item.membership,item.availability,
            statusForRep(canon.video),statusForRep(canon.audio),canon.recoveryStatus,item.firstSeen,item.lastSeen,canon.video.path,canon.audio.path};
        QStringList quoted; for(const auto& x:cols) quoted<<csv(x); catalog+=quoted.join(',').toUtf8()+"\n";
        const bool needs=(canon.video.state!="complete"||canon.audio.state!="complete") && (isUnavailable(item.availability)||canon.recoveryStatus=="unrecovered");
        if(needs){
            const QStringList mc={QString::number(item.lastPosition),item.title,item.providerId,item.url,item.membership,item.availability,
                                  statusForRep(canon.video),statusForRep(canon.audio),canon.recoveryStatus,item.firstSeen,item.lastSeen};
            QStringList mq; for(const auto& x:mc) mq<<csv(x); missing+=mq.join(',').toUtf8()+"\n";
        }
        if(item.membership=="active" && canon.video.state=="complete" && m_paths.isSafeRelative(canon.video.path)){
            video+=("#EXTINF:-1,"+item.title+"\n"+QDir::fromNativeSeparators(QDir(m_paths.sourceDir(sourceKey)).relativeFilePath(m_paths.absoluteFromRelative(canon.video.path)))+"\n").toUtf8();
        }
        if(item.membership=="active" && canon.audio.state=="complete" && m_paths.isSafeRelative(canon.audio.path)){
            audio+=("#EXTINF:-1,"+item.title+"\n"+QDir::fromNativeSeparators(QDir(m_paths.sourceDir(sourceKey)).relativeFilePath(m_paths.absoluteFromRelative(canon.audio.path)))+"\n").toUtf8();
        }
        if(isUnavailable(item.availability)) unavailable+=QJsonDocument(playlistItemToJson(item)).toJson(QJsonDocument::Compact)+"\n";
        if(item.membership=="removed") removed+=QJsonDocument(playlistItemToJson(item)).toJson(QJsonDocument::Compact)+"\n";
    }
    const QDir d(m_paths.sourceDir(sourceKey));
    return atomicWrite(d.filePath("catalog.csv"),catalog,error) && atomicWrite(d.filePath("missing.csv"),missing,error) &&
           atomicWrite(d.filePath("video.m3u8"),video,error) && atomicWrite(d.filePath("audio.m3u8"),audio,error) &&
           atomicWrite(d.filePath("unavailable.jsonl"),unavailable,error) && atomicWrite(d.filePath("removed.jsonl"),removed,error);
}

bool Store::writeReceipt(const QString& packageDir,const QJsonObject& receipt,QString* error) const
{
    return atomicWrite(QDir(packageDir).filePath("receipt.json"),QJsonDocument(receipt).toJson(QJsonDocument::Indented),error);
}

ToolResolver::ToolResolver(RuntimeConfig config):m_config(std::move(config)){}

QString ToolResolver::find(const QStringList& names,const QStringList& relativeCandidates) const
{
    for(const auto& rel:relativeCandidates){
        const auto p=QDir(m_config.appDir).filePath(rel);
        if(QFileInfo(p).exists() && QFileInfo(p).isFile()) return QDir::cleanPath(p);
    }
    for(const auto& name:names){ const auto p=QStandardPaths::findExecutable(name); if(!p.isEmpty()) return p; }
    return {};
}

QString ToolResolver::ytDlp() const
{
#ifdef Q_OS_WIN
    return find({"yt-dlp.exe","yt-dlp"},{"local/bin/yt-dlp.exe","bin/yt-dlp.exe","yt-dlp.exe"});
#else
    return find({"yt-dlp"},{"local/bin/yt-dlp","bin/yt-dlp","yt-dlp"});
#endif
}
QString ToolResolver::ffmpeg() const
{
#ifdef Q_OS_WIN
    return find({"ffmpeg.exe","ffmpeg"},{"3rdParty/ffmpeg/bin/ffmpeg.exe","local/bin/ffmpeg.exe","bin/ffmpeg.exe","ffmpeg.exe"});
#else
    return find({"ffmpeg"},{"3rdParty/ffmpeg/bin/ffmpeg","local/bin/ffmpeg","bin/ffmpeg","ffmpeg"});
#endif
}
QString ToolResolver::ffprobe() const
{
#ifdef Q_OS_WIN
    return find({"ffprobe.exe","ffprobe"},{"3rdParty/ffmpeg/bin/ffprobe.exe","local/bin/ffprobe.exe","bin/ffprobe.exe","ffprobe.exe"});
#else
    return find({"ffprobe"},{"3rdParty/ffmpeg/bin/ffprobe","local/bin/ffprobe","bin/ffprobe","ffprobe"});
#endif
}
QString ToolResolver::deno() const
{
#ifdef Q_OS_WIN
    return find({"deno.exe","deno"},{"local/bin/deno.exe","bin/deno.exe","deno.exe"});
#else
    return find({"deno"},{"local/bin/deno","bin/deno","deno"});
#endif
}

QString sourceKeyFromUrl(const QString& text)
{
    const QUrl url=QUrl::fromUserInput(text);
    const auto list=QUrlQuery(url).queryItemValue("list");
    if(!list.isEmpty()) return list;
    const auto h=QCryptographicHash::hash(text.toUtf8(),QCryptographicHash::Sha1).toHex().left(16);
    return "source-"+QString::fromLatin1(h);
}

QString availabilityFromEntry(const QJsonObject& e)
{
    QString a=e.value("availability").toString().toLower();
    const QString title=e.value("title").toString().toLower();
    if(title.contains("deleted video")) return "deleted";
    if(title.contains("private video")) return "private";
    if(a=="public"||a=="unlisted") return "public";
    if(a=="private") return "private";
    if(a=="premium_only"||a=="subscriber_only") return "members_only";
    if(a=="needs_auth") return "login_required";
    if(a=="unavailable") return "unavailable";
    return a.isEmpty()?"public":a;
}

QString canonicalKey(const QString& providerId,const QString& sourceKey,int position,const QString& title)
{
    if(!providerId.trimmed().isEmpty()) return "youtube:"+providerId.trimmed();
    const QByteArray material=(sourceKey+"|"+QString::number(position)+"|"+title).toUtf8();
    return "placeholder:"+sourceKey+":"+QString::fromLatin1(QCryptographicHash::hash(material,QCryptographicHash::Sha1).toHex().left(16));
}

QString derivedStatus(const PlaylistItem& p,const CanonicalItem* c)
{
    if(!c) return "Unknown";
    const bool v=c->video.state=="complete", a=c->audio.state=="complete";
    if(p.membership=="removed") return v&&a?"Removed · Archived":"Removed";
    if(isUnavailable(p.availability)) return v&&a?"Unavailable · Archived":"Missing";
    if(c->video.state=="failed"||c->audio.state=="failed") return "Failed";
    if(c->video.state=="interrupted"||c->audio.state=="interrupted") return "Interrupted";
    if(v&&a) return "Protected";
    return "Needs Sync";
}

PlaylistDiscovery::PlaylistDiscovery(RuntimeConfig c,ActivityLogger& l):m_config(std::move(c)),m_logger(l){}

Snapshot PlaylistDiscovery::discover(const Source& source)
{
    Snapshot s; s.sourceKey=source.key; s.scannedAt=nowIso();
    ToolResolver tools(m_config);
    const auto exe=tools.ytDlp();
    m_logger.event("INFO","discovery","scan_started",{{"source_key",source.key},{"url",source.url}});
    QStringList args={"--ignore-config","--flat-playlist","--dump-single-json","--skip-download","--ignore-errors"};
    appendYtRuntimeArgs(args,tools);
    args << source.url;
    const auto r=runProcess(exe,args,m_config.archiveRoot,300000);
    s=parse(source,r.standardOutput.toUtf8(),r.standardError,r.exitCode);
    s.scannedAt=nowIso();
    if(!r.ok && s.error.isEmpty()) s.error=r.error;
    m_logger.diagnostic(QString("discovery %1 exit=%2 stderr=%3").arg(source.key).arg(r.exitCode).arg(r.standardError.left(12000)));
    m_logger.event(s.complete?"INFO":"WARNING","discovery",s.complete?"scan_completed":"scan_partial_or_failed",
                   {{"source_key",source.key},{"items",s.items.size()},{"error",s.error}});
    return s;
}

Snapshot PlaylistDiscovery::parse(const Source& source,const QByteArray& json,const QString& stderrText,int exitCode)
{
    Snapshot s; s.sourceKey=source.key; s.scannedAt=nowIso();
    QString parseError;
    const auto doc=parseJson(json,&parseError);
    if(!doc.isObject()){
        s.error=parseError.isEmpty()?QString("yt-dlp returned no playlist JSON (exit %1)").arg(exitCode):parseError;
        s.complete=false; return s;
    }
    const auto root=doc.object();
    const auto entries=root.value("entries").toArray();
    int pos=0;
    for(const auto& value:entries){
        ++pos;
        if(value.isNull()||!value.isObject()) continue;
        const auto e=value.toObject();
        PlaylistItem p;
        p.position=e.value("playlist_index").toInt(pos);
        p.providerId=e.value("id").toString();
        p.title=e.value("title").toString();
        if(p.title.isEmpty()) p.title="[Unavailable item]";
        p.url=e.value("webpage_url").toString();
        if(p.url.isEmpty()) p.url=e.value("url").toString();
        if(!p.providerId.isEmpty() && !p.url.startsWith("http")) p.url="https://www.youtube.com/watch?v="+p.providerId;
        p.availability=availabilityFromEntry(e);
        p.itemKey=canonicalKey(p.providerId,source.key,p.position,p.title);
        s.items.append(p);
    }
    const bool transient=isTransientText(stderrText);
    s.complete=!transient && exitCode==0;
    if(transient) s.error="Transient discovery failure detected; removal inference disabled";
    else if(exitCode!=0) s.error=QString("yt-dlp exit %1; partial observations retained but removal inference disabled").arg(exitCode);
    return s;
}

MediaVerifier::MediaVerifier(RuntimeConfig c,ActivityLogger& l):m_config(std::move(c)),m_logger(l){}
ValidationResult MediaVerifier::verifyVideo(const QString& p) const{return probe(p,true);}
ValidationResult MediaVerifier::verifyAudio(const QString& p) const{return probe(p,false);}

ValidationResult MediaVerifier::probe(const QString& relativePath,bool video) const
{
    ValidationResult v;
    Paths paths(m_config.archiveRoot);
    if(!paths.isSafeRelative(relativePath)){v.errors<<"Path is not archive-root-relative";return v;}
    const auto abs=paths.absoluteFromRelative(relativePath);
    if(!QFileInfo::exists(abs)){v.errors<<"File does not exist";return v;}
    ToolResolver tools(m_config);
    const auto r=runProcess(tools.ffprobe(),{"-v","error","-show_streams","-show_format","-of","json",abs},m_config.archiveRoot,60000);
    if(!r.ok){v.errors<<(r.error+": "+r.standardError.left(1000));return v;}
    QString pe; const auto d=parseJson(r.standardOutput.toUtf8(),&pe);
    if(!d.isObject()){v.errors<<("Invalid ffprobe JSON: "+pe);return v;}
    bool hasVideo=false, videoOk=false, hasAudio=false, audioOk=false;
    for(const auto& sv:d.object().value("streams").toArray()){
        const auto s=sv.toObject(); const auto type=s.value("codec_type").toString(); const auto codec=s.value("codec_name").toString();
        if(type=="video" && s.value("disposition").toObject().value("attached_pic").toInt()==0){
            hasVideo=true; const auto pix=s.value("pix_fmt").toString(); if(codec=="h264" && pix=="yuv420p") videoOk=true;
        }
        if(type=="audio"){hasAudio=true;if(codec=="aac")audioOk=true;}
    }
    if(video){
        if(!hasVideo) v.errors<<"No video stream"; else if(!videoOk) v.errors<<"Video is not H.264/yuv420p";
        if(hasAudio&&!audioOk) v.errors<<"Audio stream is not AAC";
    }else{
        if(!hasAudio) v.errors<<"No audio stream"; else if(!audioOk) v.errors<<"Audio is not AAC";
    }
    v.ok=v.errors.isEmpty();
    return v;
}

MediaExecutor::MediaExecutor(RuntimeConfig c,Store& s,ActivityLogger& l):m_config(std::move(c)),m_store(s),m_logger(l),m_tools(m_config),m_verifier(m_config,l){}

ProcessResult MediaExecutor::run(const QString& program,const QStringList& args,const QString& purpose) const
{
    m_logger.diagnostic(QString("%1 command: %2 %3").arg(purpose,program,args.join(' ')));
    auto r=runProcess(program,args,m_config.archiveRoot,60*60*1000);
    m_logger.diagnostic(QString("%1 result exit=%2 stderr=%3").arg(purpose).arg(r.exitCode).arg(r.standardError.left(16000)));
    return r;
}

QString MediaExecutor::findExistingById(const QString& relativeDir,const QString& id,const QStringList& extensions) const
{
    if(id.isEmpty()) return {};
    QDirIterator it(QDir(m_config.archiveRoot).filePath(relativeDir),QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext()){
        const auto p=it.next(); const QFileInfo fi(p);
        if(!fi.fileName().contains("["+id+"]")) continue;
        if(!extensions.isEmpty() && !extensions.contains(fi.suffix().toLower())) continue;
        return Paths(m_config.archiveRoot).relativeToRoot(p);
    }
    return {};
}

bool MediaExecutor::downloadVideo(const CanonicalItem& item,QString* error)
{
    if(item.providerId.isEmpty()){if(error)*error="No provider ID";return false;}
    Representation running; running.state="running"; running.origin="automatic_download";
    m_store.updateRepresentation(item.key,"video",running,nullptr);
    m_logger.event("INFO","download","video_started",{{"item_key",item.key}});
    const auto fail=[&](const QString& message,const QString& path=QString()){
        Representation failed=running; failed.state="failed"; failed.path=path; failed.error=message;
        m_store.updateRepresentation(item.key,"video",failed,nullptr);
        if(error) *error=message;
        m_logger.event("ERROR","download","video_failed",{{"item_key",item.key},{"error",message}});
        return false;
    };
    const QString url=item.originalUrl.isEmpty()?"https://www.youtube.com/watch?v="+item.providerId:item.originalUrl;
    const QString output="Video/%(title).120s [%(artist|UNKNOWN)s] [%(height)sp %(duration)ss %(upload_date>%y%m%d|UNKNOWN)s] [%(id)s].%(ext)s";
    QStringList args={
        "--ignore-config","--no-playlist","--output-na-placeholder","NA",
        "-f","bv[height<=1080][vcodec^=avc]+ba[ext=m4a]/bv[height<=1080][vcodec^=avc]+ba/bv[height<=1080]+ba/b[height<=1080]",
        "--paths","temp:Temp","--merge-output-format","mp4","-o",output,
        "-o","infojson:Metadata/%(title).80s [%(artist|UNKNOWN)s] [%(id)s]/source [%(id)s].%(ext)s",
        "-o","description:Metadata/%(title).80s [%(artist|UNKNOWN)s] [%(id)s]/description [%(id)s].%(ext)s",
        "-o","thumbnail:Metadata/%(title).80s [%(artist|UNKNOWN)s] [%(id)s]/thumbnail [%(id)s].%(ext)s",
        "-o","subtitle:Metadata/%(title).80s [%(artist|UNKNOWN)s] [%(id)s]/Subtitles/%(language)s [%(id)s].%(ext)s",
        "--download-archive","State/video-archive.txt","--windows-filenames","--embed-metadata","--embed-thumbnail","--embed-chapters",
        "--write-info-json","--write-description","--write-thumbnail","--extractor-args","youtube:skip=translated_subs",
        "--write-subs","--write-auto-subs","--sub-langs","en.*","--sleep-subtitles","1","--embed-subs",
        "--parse-metadata","%(artist|UNKNOWN)s:%(meta_artist)s",
        "--print-to-file","after_move:%(.{id,title,artist,meta_artist,uploader,upload_date,duration,ext,webpage_url,filepath})j","State/video-catalog.jsonl"};
    appendYtRuntimeArgs(args,m_tools);
    args << url;
    auto r=run(m_tools.ytDlp(),args,"video-download");
    QString rel=findExistingById("Video",item.providerId,{"mp4","mkv","webm"});
    if(r.ok && rel.isEmpty()){
        m_logger.event("WARNING","download","video_archive_retry",{{"item_key",item.key}});
        const auto retryArgs=withoutDownloadArchive(args);
        r=run(m_tools.ytDlp(),retryArgs,"video-download-retry-without-archive");
        rel=findExistingById("Video",item.providerId,{"mp4","mkv","webm"});
    }
    if(!r.ok && rel.isEmpty()) return fail(r.error+" "+r.standardError.left(600));
    if(rel.isEmpty()) return fail("Downloaded video could not be located");
    auto verify=m_verifier.verifyVideo(rel);
    if(!verify.ok){
        const auto input=Paths(m_config.archiveRoot).absoluteFromRelative(rel);
        const auto tmp=QDir(Paths(m_config.archiveRoot).temp()).filePath("normalize-"+item.providerId+".mp4");
        const QStringList fargs={"-y","-i",input,"-map","0:v:0","-map","0:a:0?","-map","0:s?","-map_metadata","0","-map_chapters","0",
                                 "-c:v","libx264","-preset","medium","-crf","18","-pix_fmt","yuv420p","-c:a","aac","-b:a","192k","-c:s","mov_text",tmp};
        const auto tr=run(m_tools.ffmpeg(),fargs,"video-normalization");
        if(!tr.ok) return fail("Conditional video normalization failed: "+tr.standardError.left(700),rel);
        QFile::remove(input);
        if(!QFile::rename(tmp,input)) return fail("Unable to replace video with normalized output",rel);
        verify=m_verifier.verifyVideo(rel);
    }
    if(!verify.ok) return fail("Video verification failed: "+verify.errors.join("; "),rel);
    Representation done; done.state="complete"; done.path=rel; done.origin="automatic_download"; done.verifiedAt=nowIso();
    m_store.updateRepresentation(item.key,"video",done,nullptr);
    m_logger.event("INFO","verification","video_complete",{{"item_key",item.key},{"path",rel}});
    return true;
}

bool MediaExecutor::downloadAudio(const CanonicalItem& item,QString* error)
{
    if(item.providerId.isEmpty()){if(error)*error="No provider ID";return false;}
    Representation running; running.state="running"; running.origin="automatic_download";
    m_store.updateRepresentation(item.key,"audio",running,nullptr);
    m_logger.event("INFO","download","audio_started",{{"item_key",item.key}});
    const auto fail=[&](const QString& message,const QString& path=QString()){
        Representation failed=running; failed.state="failed"; failed.path=path; failed.error=message;
        m_store.updateRepresentation(item.key,"audio",failed,nullptr);
        if(error) *error=message;
        m_logger.event("ERROR","download","audio_failed",{{"item_key",item.key},{"error",message}});
        return false;
    };
    const QString url=item.originalUrl.isEmpty()?"https://www.youtube.com/watch?v="+item.providerId:item.originalUrl;
    QStringList args={"--ignore-config","--no-playlist","--output-na-placeholder","NA","-f","ba[ext=m4a]/ba","--paths","temp:Temp",
        "-o","Audio/%(title).120s [%(artist|UNKNOWN)s] [m4a %(duration)ss %(upload_date>%y%m%d|UNKNOWN)s] [%(id)s].%(ext)s",
        "--download-archive","State/audio-archive.txt","--windows-filenames","-x","--audio-format","m4a","--audio-quality","192K","--no-keep-video",
        "--embed-metadata","--embed-thumbnail","--embed-chapters","--parse-metadata","%(artist|UNKNOWN)s:%(meta_artist)s",
        "--print-to-file","after_move:%(.{id,title,artist,meta_artist,uploader,upload_date,duration,ext,webpage_url,filepath})j","State/audio-catalog.jsonl"};
    appendYtRuntimeArgs(args,m_tools);
    args << url;
    auto r=run(m_tools.ytDlp(),args,"audio-download");
    QString rel=findExistingById("Audio",item.providerId,{"m4a","mp4"});
    if(r.ok && rel.isEmpty()){
        m_logger.event("WARNING","download","audio_archive_retry",{{"item_key",item.key}});
        const auto retryArgs=withoutDownloadArchive(args);
        r=run(m_tools.ytDlp(),retryArgs,"audio-download-retry-without-archive");
        rel=findExistingById("Audio",item.providerId,{"m4a","mp4"});
    }
    if(!r.ok && rel.isEmpty()) return fail(r.error+" "+r.standardError.left(600));
    if(rel.isEmpty()) return fail("Downloaded audio could not be located");
    const auto verify=m_verifier.verifyAudio(rel);
    if(!verify.ok) return fail("Audio verification failed: "+verify.errors.join("; "),rel);
    Representation done; done.state="complete"; done.path=rel; done.origin="automatic_download"; done.verifiedAt=nowIso();
    m_store.updateRepresentation(item.key,"audio",done,nullptr);
    m_logger.event("INFO","verification","audio_complete",{{"item_key",item.key},{"path",rel}});
    return true;
}

bool MediaExecutor::syncItem(const CanonicalItem& item,bool wantVideo,bool wantAudio,QString* error)
{
    if(isUnavailable(item.availability)){if(error)*error="Source is unavailable";return false;}
    bool ok=true; QStringList errors;
    if(wantVideo && item.video.state!="complete"){QString e;if(!downloadVideo(item,&e)){ok=false;errors<<e;}}
    if(wantAudio && item.audio.state!="complete"){QString e;if(!downloadAudio(item,&e)){ok=false;errors<<e;}}
    if(error)*error=errors.join(" | ");
    return ok;
}

bool MediaExecutor::syncItems(const QVector<CanonicalItem>& items,const std::function<bool()>& shouldStop,QStringList* failures)
{
    bool all=true;
    for(const auto& item:items){
        if(shouldStop && shouldStop()) break;
        QString e;
        if(!syncItem(item,true,true,&e)){ all=false; if(failures) failures->append(item.key+": "+e); }
    }
    return all;
}

RecoveryImporter::RecoveryImporter(RuntimeConfig c,Store& s,ActivityLogger& l):m_config(std::move(c)),m_store(s),m_logger(l),m_tools(m_config),m_verifier(m_config,l){}

ValidationResult RecoveryImporter::validate(const QString& packageDir) const
{
    ValidationResult r; Paths paths(m_config.archiveRoot);
    const QFileInfo dirInfo(packageDir);
    if(!dirInfo.exists()||!dirInfo.isDir()){r.errors<<"Package directory does not exist";return r;}
    const auto manifestPath=QDir(packageDir).filePath("manifest.json");
    QString pe; const auto doc=parseJson(readAll(manifestPath),&pe);
    if(!doc.isObject()){r.errors<<("Invalid manifest.json: "+pe);return r;}
    const auto o=doc.object();
    if(o.value("schema_version").toInt()!=1) r.errors<<"schema_version must be 1";
    r.packageId=o.value("package_id").toString(); if(r.packageId.isEmpty()) r.errors<<"package_id is required";
    const auto target=o.value("target").toObject(); r.itemKey=target.value("item_key").toString(); if(r.itemKey.isEmpty()) r.errors<<"target.item_key is required";
    const auto provenance=o.value("provenance").toObject(); if(provenance.value("method").toString().isEmpty()) r.errors<<"provenance.method is required";
    const auto reps=o.value("representations").toObject();
    for(const auto& kind:QStringList{"video","audio"}){
        if(!reps.contains(kind)) continue;
        const auto file=reps.value(kind).toObject().value("file").toString();
        if(file.isEmpty()){r.errors<<(kind+" file is empty");continue;}
        if(!paths.isSafeRelative(file)){r.errors<<(kind+" file path must be package-relative");continue;}
        const auto abs=QDir::cleanPath(QDir(packageDir).filePath(file));
        const auto rel=QDir(packageDir).relativeFilePath(abs);
        if(rel.startsWith("..")||!QFileInfo::exists(abs)) r.errors<<(kind+" file is missing or escapes package directory");
    }
    const auto canonical=m_store.loadCanonicalItems();
    bool found=false;
    for(const auto& i:canonical){
        if(i.key==r.itemKey){found=true;
            if(reps.contains("video")&&i.video.state=="complete") r.errors<<"Canonical video is already complete";
            if(reps.contains("audio")&&i.audio.state=="complete") r.errors<<"Canonical audio is already complete";
            break;
        }
    }
    if(!found) r.errors<<"target.item_key does not exist in canonical state";
    r.ok=r.errors.isEmpty(); return r;
}

bool RecoveryImporter::normalizeVideo(const QString& input,const QString& output,QString* error) const
{
    const QStringList args={"-y","-i",input,"-map","0:v:0","-map","0:a:0?","-map_metadata","0","-map_chapters","0",
                            "-c:v","libx264","-preset","medium","-crf","18","-pix_fmt","yuv420p","-c:a","aac","-b:a","192k",output};
    const auto r=runProcess(m_tools.ffmpeg(),args,m_config.archiveRoot,60*60*1000);
    if(!r.ok){if(error)*error=r.error+" "+r.standardError.left(1000);return false;} return true;
}

bool RecoveryImporter::normalizeAudio(const QString& input,const QString& output,QString* error) const
{
    const QStringList args={"-y","-i",input,"-vn","-map_metadata","0","-map_chapters","0","-c:a","aac","-b:a","192k",output};
    const auto r=runProcess(m_tools.ffmpeg(),args,m_config.archiveRoot,60*60*1000);
    if(!r.ok){if(error)*error=r.error+" "+r.standardError.left(1000);return false;} return true;
}

bool RecoveryImporter::ingest(const QString& packageDir,QString* error)
{
    const auto vr=validate(packageDir);
    if(!vr.ok){if(error)*error=vr.errors.join("; ");return false;}
    const auto doc=QJsonDocument::fromJson(readAll(QDir(packageDir).filePath("manifest.json")));
    const auto manifest=doc.object(); const auto reps=manifest.value("representations").toObject();
    auto canonical=m_store.loadCanonicalItems(); CanonicalItem target; bool found=false;
    for(const auto& i:canonical) if(i.key==vr.itemKey){target=i;found=true;break;}
    if(!found){if(error)*error="Canonical item disappeared during import";return false;}
    const QString id=target.providerId.isEmpty()?QString::fromLatin1(QCryptographicHash::hash(target.key.toUtf8(),QCryptographicHash::Sha1).toHex().left(11)):target.providerId;
    const QString base=safeFilePart(target.title)+" [RECOVERED] ["+safeFilePart(id)+"]";
    QStringList promoted;
    if(reps.contains("video")){
        const auto rel=reps.value("video").toObject().value("file").toString(); const auto in=QDir(packageDir).filePath(rel);
        const auto abs=QDir(Paths(m_config.archiveRoot).video()).filePath(base+".mp4"); const auto tmp=QDir(Paths(m_config.archiveRoot).temp()).filePath("import-"+id+"-video.mp4");
        QString e; if(!normalizeVideo(in,tmp,&e)){if(error)*error=e;return false;}
        const auto tempRel=Paths(m_config.archiveRoot).relativeToRoot(tmp); auto vv=m_verifier.verifyVideo(tempRel); if(!vv.ok){if(error)*error=vv.errors.join("; ");QFile::remove(tmp);return false;}
        QFile::remove(abs); if(!QFile::rename(tmp,abs)){if(error)*error="Failed to promote recovered video";return false;}
        Representation rp;rp.state="complete";rp.path=Paths(m_config.archiveRoot).relativeToRoot(abs);rp.origin="external_recovery";rp.verifiedAt=nowIso();m_store.updateRepresentation(vr.itemKey,"video",rp,nullptr);promoted<<rp.path;
    }
    if(reps.contains("audio")){
        const auto rel=reps.value("audio").toObject().value("file").toString(); const auto in=QDir(packageDir).filePath(rel);
        const auto abs=QDir(Paths(m_config.archiveRoot).audio()).filePath(base+".m4a"); const auto tmp=QDir(Paths(m_config.archiveRoot).temp()).filePath("import-"+id+"-audio.m4a");
        QString e;if(!normalizeAudio(in,tmp,&e)){if(error)*error=e;return false;}
        const auto tempRel=Paths(m_config.archiveRoot).relativeToRoot(tmp);auto av=m_verifier.verifyAudio(tempRel);if(!av.ok){if(error)*error=av.errors.join("; ");QFile::remove(tmp);return false;}
        QFile::remove(abs);if(!QFile::rename(tmp,abs)){if(error)*error="Failed to promote recovered audio";return false;}
        Representation rp;rp.state="complete";rp.path=Paths(m_config.archiveRoot).relativeToRoot(abs);rp.origin="external_recovery";rp.verifiedAt=nowIso();m_store.updateRepresentation(vr.itemKey,"audio",rp,nullptr);promoted<<rp.path;
    }
    QJsonObject receipt{{"schema_version",1},{"package_id",vr.packageId},{"item_key",vr.itemKey},{"result","accepted"},{"accepted_at",nowIso()}};
    QJsonArray a;for(const auto& p:promoted)a.append(p);receipt["promoted_paths"]=a; m_store.writeReceipt(packageDir,receipt,nullptr);
    const auto dest=QDir(m_store.paths().importsAccepted()).filePath(safeFilePart(vr.packageId));
    if(QFileInfo::exists(dest)) QDir(dest).removeRecursively();
    if(!QDir().rename(packageDir,dest)){if(error)*error="Accepted package could not be moved to Accepted";return false;}
    m_logger.event("INFO","import","recovery_package_accepted",{{"package_id",vr.packageId},{"item_key",vr.itemKey}});
    return true;
}

int RecoveryImporter::ingestPending(QStringList* failures)
{
    QDir d(m_store.paths().importsPending()); const auto dirs=d.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name); int accepted=0;
    for(const auto& name:dirs){
        const auto dir=d.filePath(name); QString e;
        if(ingest(dir,&e)){++accepted;continue;}
        QJsonObject receipt{{"schema_version",1},{"package_id",name},{"result","rejected"},{"rejected_at",nowIso()},{"reason",e}};
        m_store.writeReceipt(dir,receipt,nullptr);
        const auto dest=QDir(m_store.paths().importsRejected()).filePath(name+"-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
        QDir().rename(dir,dest); if(failures) failures->append(name+": "+e);
        m_logger.event("WARNING","import","recovery_package_rejected",{{"package_id",name},{"reason",e}});
    }
    return accepted;
}

SyncLock::SyncLock(const Paths& paths):m_lock(new QLockFile(QDir(paths.archiveState()).filePath("sync.lock")))
{
    m_lock->setStaleLockTime(10*60*1000);
}
bool SyncLock::tryLock(int timeoutMs){const bool ok=m_lock->tryLock(timeoutMs);if(!ok)m_error="Another Archive operation is active or the lock could not be acquired";return ok;}
void SyncLock::unlock(){if(m_lock)m_lock->unlock();}
QString SyncLock::errorString() const{return m_error;}

} // namespace archive
