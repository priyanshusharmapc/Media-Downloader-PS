#include "archivecore.h"
#include "archivesafety.h"
#include <QMutex>
#include <QMutexLocker>
#include <QUuid>
#include <cmath>
#include <QElapsedTimer>
#include <QTemporaryDir>

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
using namespace detail;
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
    return detail::writeBytes(path,data,error);
}

bool appendLine(const QString& path,const QByteArray& line,QString* error)
{
    if(!detail::noLinks(path)) return detail::reject(error,"Linked log path refused");
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
    if(!f.flush()) return detail::reject(error,f.errorString());
    return true;
}

QString csv(QString s)
{
    if(!s.trimmed().isEmpty() && (QString("=+-@").contains(s.trimmed().front())||QString("\t\r\n").contains(s.front()))) s.prepend('\'');
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
    o["item_key"]=i.itemKey; o["entry_key"]=i.entryKey; o["provider_id"]=i.providerId; o["position"]=i.position;
    o["title"]=i.title; o["url"]=i.url; o["availability"]=i.availability; o["membership"]=i.membership;
    o["first_seen"]=i.firstSeen; o["last_seen"]=i.lastSeen; o["last_position"]=i.lastPosition;
    return o;
}

PlaylistItem playlistItemFromJson(const QJsonObject& o)
{
    PlaylistItem i;
    i.itemKey=o.value("item_key").toString(); i.entryKey=o.value("entry_key").toString(); i.providerId=o.value("provider_id").toString();
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
    QByteArray bytes;QFile in(resource);
    if(in.open(QIODevice::ReadOnly))bytes=in.readAll();
    else {
        QString relative=resource;const QString prefix=":/archive/";if(relative.startsWith(prefix))relative=relative.mid(prefix.size());
        QFile external(QDir(QCoreApplication::applicationDirPath()).filePath("archive-resources/"+relative));
        if(!external.open(QIODevice::ReadOnly))return detail::reject(error,"Cannot read Archive resource: "+resource);
        bytes=external.readAll();
    }
    if(QFileInfo::exists(destination)){
        QByteArray previous;if(!detail::readBytes(destination,&previous,error))return false;if(previous==bytes)return true;
        const auto backup=destination+".previous-"+detail::digest(previous).left(12);
        if(!QFileInfo::exists(backup)){if(!atomicWrite(backup,previous,error))return false;}
        else if(detail::fileDigest(backup,error)!=detail::digest(previous))return detail::reject(error,"Resource backup collision; existing bytes preserved");
    }
    return atomicWrite(destination,bytes,error);
}

ProcessResult runProcess(const QString& program,const QStringList& args,const QString& cwd,int timeoutMs)
{
    ProcessResult r;
    if(program.isEmpty()){r.error="Required executable was not found";return r;}
    QProcess process;if(!cwd.isEmpty())process.setWorkingDirectory(cwd);
    process.setProcessChannelMode(QProcess::SeparateChannels);process.start(program,args);
    if(!process.waitForStarted(10000)){r.error=process.errorString();return r;}
    process.closeWriteChannel();
    QByteArray output,errors;QElapsedTimer timer;timer.start();bool overflow=false,timedOut=false;
    const auto drain=[&]{
        const auto chunk=process.readAllStandardOutput();
        if(output.size()+chunk.size()>64*1024*1024)overflow=true;else output+=chunk;
        errors+=process.readAllStandardError();
        if(errors.size()>256*1024)errors=QByteArray("WARNING: earlier diagnostic output truncated\n")+errors.right(256*1024-64);
    };
    while(process.state()!=QProcess::NotRunning){
        process.waitForReadyRead(100);drain();
        timedOut=timer.elapsed()>timeoutMs;
        if(overflow||timedOut){
#ifdef Q_OS_WIN
            const auto pid=process.processId();
            if(pid>0){
                QProcess killer;killer.start("taskkill",{"/PID",QString::number(pid),"/T","/F"});
                killer.waitForFinished(5000);
            }else process.kill();
#else
            process.kill();
#endif
            process.waitForFinished(5000);break;
        }
    }
    drain();r.standardOutput=QString::fromUtf8(output);r.standardError=QString::fromUtf8(errors);
    if(overflow||timedOut){r.error=overflow?"Process output exceeded the safety limit":"Process timed out";return r;}
    r.exitCode=process.exitCode();r.ok=process.exitStatus()==QProcess::NormalExit&&r.exitCode==0;
    if(!r.ok)r.error=QString("Process exited with code %1").arg(r.exitCode);return r;
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
    const int separator=args.indexOf("--");
    const int urlIndex=separator>=0?separator:(args.isEmpty()?0:args.size()-1);
    args.insert(urlIndex,"--no-download-archive");
    return args;
}
}

Paths::Paths(QString root)
{
    if(root.trimmed().isEmpty()) return;
    // Preserve the operator-selected path. Canonicalizing here would erase a
    // root or parent symlink before the safety layer can reject it.
    m_root=QDir::cleanPath(QFileInfo(root).absoluteFilePath());
}

QString placeholderFingerprint(const QString& sourceKey,const QString& title,const QString& url);
QString placeholderBaseKey(const QString& sourceKey,const QString& title,const QString& url=QString());
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
QString Paths::sourceDir(const QString& sourceKey) const{return detail::sourceKeySafe(sourceKey)?QDir(playlists()).filePath(sourceKey):QString();}
QString Paths::playlistFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("playlist.json");}
QString Paths::playlistItemsFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("items.json");}
QString Paths::playlistHistoryFile(const QString& sourceKey) const{return QDir(sourceDir(sourceKey)).filePath("history.jsonl");}

QString Paths::relativeToRoot(const QString& absolute) const
{
    if(m_root.isEmpty()) return {};
    const auto rel=QDir(m_root).relativeFilePath(QDir::cleanPath(QDir::fromNativeSeparators(absolute)));
    return QDir::fromNativeSeparators(rel);
}

QString Paths::absoluteFromRelative(const QString& relative) const
{
    if(!isSafeRelative(relative)) return {};
    return QDir::cleanPath(QDir(m_root).filePath(QDir::fromNativeSeparators(relative)));
}

bool Paths::isSafeRelative(const QString& path) const
{
    return !m_root.isEmpty() && detail::relativeSafe(path) && detail::noLinks(QDir(m_root).filePath(path));
}

bool Paths::ensureLayout(QString* error) const
{
    if(m_root.isEmpty() || !QDir::isAbsolutePath(m_root) || !detail::noLinks(m_root)){
        if(error) *error="Archive root is empty";
        return false;
    }
    const QStringList dirs={video(),audio(),metadata(),temp(),playlists(),state(),archiveState(),schemas(),
                            importsPending(),importsAccepted(),importsRejected(),activityLogs(),diagnosticLogs()};
    for(const auto& d:dirs){
        if(!detail::noLinks(d) || !QDir().mkpath(d)){
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
    text.replace(QRegularExpression("(?i)(authorization\\s*[:=]\\s*(?:bearer|basic)\\s+)[^\\s,;]+"),"\\1<REDACTED>");
    text.replace(QRegularExpression("(?i)(cookie|authorization|token|password|secret)(\\s*[:=]\\s*)([^\\s,;]+)"),"\\1\\2<REDACTED>");
    return text;
}

void ActivityLogger::event(const QString& severity,const QString& category,const QString& name,const QJsonObject& details,const QString& sessionId)
{
    if(!m_paths.ensureLayout())return;
    SyncLock lock(m_paths);if(!lock.tryLock())return;
    const auto dayDir=QDir(m_paths.activityLogs()).filePath(localDate());
    QDir().mkpath(dayDir);
    QJsonObject o;
    o["schema_version"]=1; o["timestamp"]=nowIso(); o["session_id"]=sessionId.isEmpty()?m_sessionId:sessionId;
    o["severity"]=severity; o["category"]=category; o["event"]=name;
    std::function<QJsonValue(const QJsonValue&)> sanitize=[&](const QJsonValue& value)->QJsonValue {
        if(value.isString()) return redact(value.toString());
        if(value.isArray()){QJsonArray a;for(const auto& v:value.toArray())a.append(sanitize(v));return a;}
        if(value.isObject()){QJsonObject out;const auto object=value.toObject();for(auto it=object.begin();it!=object.end();++it){
            const bool secret=QRegularExpression("(?i)(cookie|authorization|token|password|secret)").match(it.key()).hasMatch();
            out[it.key()]=secret?QJsonValue("<REDACTED>"):sanitize(it.value());}return out;}
        return value;
    };
    const auto sanitized=sanitize(details).toObject();
    o["details"]=sanitized;
    appendLine(QDir(dayDir).filePath(m_sessionId+".jsonl"),QJsonDocument(o).toJson(QJsonDocument::Compact),nullptr);
    prune();
}

void ActivityLogger::diagnostic(const QString& line)
{
    if(!m_paths.ensureLayout())return;
    SyncLock lock(m_paths);if(!lock.tryLock())return;
    const qint64 maxFile=2*1024*1024;const int count=5;
    QByteArray bytes=QString("%1 %2").arg(nowIso(),redact(line)).toUtf8();
    if(bytes.size()>256*1024)bytes=bytes.left(256*1024-40)+" [diagnostic entry truncated]";
    const auto current=QDir(m_paths.diagnosticLogs()).filePath("diagnostic-0.log");
    if(QFileInfo(current).size()+bytes.size()+1>maxFile){
        for(int i=count-1;i>=0;--i){
            const auto from=QDir(m_paths.diagnosticLogs()).filePath(QString("diagnostic-%1.log").arg(i));
            const auto to=QDir(m_paths.diagnosticLogs()).filePath(QString("diagnostic-%1.log").arg(i+1));
            if(!detail::noLinks(from)||!detail::noLinks(to))return;
            if(i==count-1)QFile::remove(from);else if(QFileInfo::exists(from))QFile::rename(from,to);
        }
    }
    appendLine(current,bytes,nullptr);
}

void ActivityLogger::prune()
{
    auto dirs=sortedDirs(m_paths.activityLogs());
    dirs.erase(std::remove_if(dirs.begin(),dirs.end(),[](const QString& s){return !QDate::fromString(s,Qt::ISODate).isValid();}),dirs.end());
    while(dirs.size()>7 || (!dirs.isEmpty()&&QDate::fromString(dirs.first(),Qt::ISODate)<QDate::currentDate().addDays(-6))){
        const auto path=QDir(m_paths.activityLogs()).filePath(dirs.takeFirst());
        if(detail::noLinks(path)) QDir(path).removeRecursively();
    }
}

Store::Store(Paths paths):m_paths(std::move(paths)){}
const Paths& Store::paths() const{return m_paths;}

bool Store::initialize(QString* error)
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    if(error)error->clear();
    if(!m_paths.ensureLayout(error)||!detail::recoverTransaction(m_paths.root(),error))return false;
    const bool sources=QFileInfo::exists(m_paths.sourcesFile()),items=QFileInfo::exists(m_paths.itemsFile());
    if(sources!=items)return detail::reject(error,"Incomplete Archive registry; existing state is preserved");
    if(!sources){
        // Existing playlist history means this is not a fresh archive.
        if(!QDir(m_paths.playlists()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).isEmpty())
            return detail::reject(error,"Canonical registries missing beside existing playlist history");
        if(!detail::commitTransaction(m_paths.root(),{{"State/ArchiveMode/sources.json","[]\n"},{"State/ArchiveMode/items.json","[]\n"}},error))return false;
    }
    QJsonArray a;
    if(!detail::readArray(m_paths.sourcesFile(),"source",&a,error)||!detail::readArray(m_paths.itemsFile(),"canonical",&a,error))return false;
    if(!m_paths.materializeAgentResources(error))return false;
    if(QFileInfo::exists(QDir(m_paths.archiveState()).filePath("projections-dirty.json")))return writeAllProjections(error);
    return true;
}

QVector<Source> Store::loadSources(QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock()){detail::reject(error,lock.errorString());return {};}
    QJsonArray a;QVector<Source> out;if(!detail::readArray(m_paths.sourcesFile(),"source",&a,error))return out;
    for(const auto& e:a)out.append(sourceFromJson(e.toObject()));return out;
}
bool Store::saveSources(const QVector<Source>& sources,QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    QJsonArray prior;const auto next=vectorToArray(sources,sourceToJson);
    if(!detail::readArray(m_paths.sourcesFile(),"source",&prior,error)||!detail::arrayShape(next,"source",error))return false;
    return detail::commitTransaction(m_paths.root(),{{"State/ArchiveMode/sources.json",QJsonDocument(next).toJson()}},error);
}
QVector<CanonicalItem> Store::loadCanonicalItems(QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock()){detail::reject(error,lock.errorString());return {};}
    QJsonArray a;QVector<CanonicalItem> out;if(!detail::readArray(m_paths.itemsFile(),"canonical",&a,error))return out;
    for(const auto& e:a)out.append(canonicalFromJson(e.toObject()));return out;
}
bool Store::saveCanonicalItems(const QVector<CanonicalItem>& items,QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    QJsonArray prior;const auto next=vectorToArray(items,canonicalToJson);
    if(!detail::readArray(m_paths.itemsFile(),"canonical",&prior,error)||!detail::arrayShape(next,"canonical",error))return false;
    return detail::commitTransaction(m_paths.root(),{{"State/ArchiveMode/items.json",QJsonDocument(next).toJson()}},error);
}
QVector<PlaylistItem> Store::loadPlaylistItems(const QString& sourceKey,QString* error) const
{
    if(!detail::sourceKeySafe(sourceKey)){detail::reject(error,"Invalid source key");return {};}
    SyncLock lock(m_paths);if(!lock.tryLock()){detail::reject(error,lock.errorString());return {};}
    QJsonArray a;QVector<PlaylistItem> out;if(!detail::readArray(m_paths.playlistItemsFile(sourceKey),"playlist",&a,error,true))return out;
    for(const auto& e:a)out.append(playlistItemFromJson(e.toObject()));return out;
}
bool Store::savePlaylistItems(const QString& sourceKey,const QVector<PlaylistItem>& items,QString* error) const
{
    if(!detail::sourceKeySafe(sourceKey))return detail::reject(error,"Invalid source key");
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    QJsonArray prior;const auto next=vectorToArray(items,playlistItemToJson);
    if(!detail::readArray(m_paths.playlistItemsFile(sourceKey),"playlist",&prior,error,true)||!detail::arrayShape(next,"playlist",error))return false;
    return detail::commitTransaction(m_paths.root(),{{"Playlists/"+sourceKey+"/items.json",QJsonDocument(next).toJson()}},error);
}

bool Store::savePlaylistMeta(const Source& source,QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    if(!detail::sourceKeySafe(source.key))return detail::reject(error,"Invalid source key");
    QDir().mkpath(m_paths.sourceDir(source.key));
    return atomicWrite(m_paths.playlistFile(source.key),QJsonDocument(sourceToJson(source)).toJson(QJsonDocument::Indented),error);
}

bool Store::appendHistory(const QString& sourceKey,const QJsonObject& e,QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    if(!detail::sourceKeySafe(sourceKey))return detail::reject(error,"Invalid source key");
    return appendLine(m_paths.playlistHistoryFile(sourceKey),QJsonDocument(e).toJson(QJsonDocument::Compact),error);
}

bool Store::updateRepresentation(const QString& itemKey,const QString& kind,const Representation& representation,QString* error)
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    QString stateError;auto items=loadCanonicalItems(&stateError);
    if(!stateError.isEmpty())return detail::reject(error,stateError);
    for(auto& item:items){
        if(item.key==itemKey){
            if(kind=="video") item.video=representation;
            else if(kind=="audio") item.audio=representation;
            else { if(error) *error="Unknown representation kind"; return false; }
            if(item.video.state=="complete"&&item.audio.state=="complete") item.recoveryStatus="not_required";
            else if(isUnavailable(item.availability)) item.recoveryStatus="unrecovered";
            return saveCanonicalItems(items,error);
        }
    }
    if(error) *error="Canonical item not found";
    return false;
}

bool Store::updateCanonicalMetadata(const QString& itemKey,const QString& title,const QString& uploader,const QString& availability,const QString& originalUrl,QString* error)
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    QString stateError;auto items=loadCanonicalItems(&stateError);
    if(!stateError.isEmpty())return detail::reject(error,stateError);
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
    SyncLock lock(m_paths);if(!lock.tryLock()){summary.error=lock.errorString();return summary;}
    if(!detail::sourceKeySafe(source.key)||snapshot.sourceKey!=source.key){summary.error="Snapshot/source identity mismatch or unsafe source key";return summary;}
    if(!initialize(&error)){ summary.error=error; return summary; }

    auto prior=loadPlaylistItems(source.key,&error);
    if(!error.isEmpty()){ summary.error=error; return summary; }
    auto canonical=loadCanonicalItems(&error);
    if(!error.isEmpty()){ summary.error=error; return summary; }

    QHash<QString,int> priorIndex;
    QHash<QString,QVector<int>> priorPlaceholders;
    QHash<QString,int> priorOccurrences;
    for(int i=0;i<prior.size();++i){
        const int ordinal=++priorOccurrences[prior[i].itemKey];
        if(prior[i].entryKey.isEmpty())prior[i].entryKey=prior[i].itemKey+"#"+QString::number(ordinal);
        priorIndex[prior[i].entryKey]=i;
        if(prior[i].providerId.isEmpty()&&prior[i].itemKey.startsWith("placeholder:"))
            priorPlaceholders[placeholderFingerprint(source.key,prior[i].title,prior[i].url)].append(i);
    }
    QHash<QString,int> canonicalIndex;
    for(int i=0;i<canonical.size();++i) canonicalIndex[canonical[i].key]=i;

    QByteArray history;
    if(QFileInfo::exists(m_paths.playlistHistoryFile(source.key)) && !detail::readBytes(m_paths.playlistHistoryFile(source.key),&history,&error)){summary.error=error;return summary;}
    if(!history.isEmpty()&&!history.endsWith('\n')){summary.error="Incomplete history record; preserve and repair before reconciliation";return summary;}
    const auto record=[&](const QJsonObject& event){history+=QJsonDocument(event).toJson(QJsonDocument::Compact)+"\n";};
    QVector<PlaylistItem> result;
    QSet<QString> observedKeys;
    QHash<QString,int> observedOccurrences;
    const auto scanTime=snapshot.scannedAt.isEmpty()?nowIso():snapshot.scannedAt;
    QSet<int> matchedPrior;

    for(auto p:snapshot.items){
        if(!p.providerId.isEmpty()&&!detail::videoIdSafe(p.providerId)){summary.error="Invalid provider identity";return summary;}
        const bool unresolved=p.providerId.isEmpty();
        const auto expectedKey=unresolved?placeholderBaseKey(source.key,p.title,p.url):canonicalKey(p.providerId,source.key,p.position,p.title);
        if(p.itemKey.isEmpty()) p.itemKey=expectedKey;
        if(unresolved){
            if(!p.itemKey.startsWith("placeholder:"+source.key+":")){summary.error="Invalid unresolved placeholder identity";return summary;}
            const auto fingerprint=placeholderFingerprint(source.key,p.title,p.url);
            const auto candidates=priorPlaceholders.value(fingerprint);
            int priorIndexForPlaceholder=-1;
            for(const auto candidate:candidates)if(!matchedPrior.contains(candidate)){priorIndexForPlaceholder=candidate;break;}
            if(priorIndexForPlaceholder>=0){
                matchedPrior.insert(priorIndexForPlaceholder);
                p.itemKey=prior[priorIndexForPlaceholder].itemKey;
                p.entryKey=prior[priorIndexForPlaceholder].entryKey;
            }else{
                p.itemKey=expectedKey;
                int ordinal=1;
                while(canonicalIndex.contains(p.itemKey)){
                    ++ordinal;
                    const auto suffix=QString::fromLatin1(QCryptographicHash::hash((fingerprint+"|"+QString::number(ordinal)).toUtf8(),QCryptographicHash::Sha1).toHex().left(8));
                    p.itemKey=expectedKey+":occurrence-"+suffix;
                }
                p.entryKey=p.itemKey+"#1";
            }
        }else{
            if(p.itemKey!=expectedKey){summary.error="Canonical identity mismatch";return summary;}
            p.entryKey=p.itemKey+"#"+QString::number(++observedOccurrences[p.itemKey]);
        }
        observedKeys.insert(p.entryKey);
        const bool hadPrior=priorIndex.contains(p.entryKey);
        PlaylistItem previous;
        if(hadPrior) previous=prior[priorIndex.value(p.entryKey)];
        if(p.firstSeen.isEmpty()) p.firstSeen=hadPrior && !previous.firstSeen.isEmpty()?previous.firstSeen:scanTime;
        p.lastSeen=scanTime; p.lastPosition=p.position; p.membership="active";
        if(p.url.isEmpty() && !p.providerId.isEmpty()) p.url="https://www.youtube.com/watch?v="+p.providerId;
        if(hadPrior && previous.membership=="removed"){
            ++summary.reappeared;
            record(historyEvent("membership_reappeared",p.itemKey,{{"position",p.position},{"entry_key",p.entryKey}}));
        }else if(!hadPrior){
            record(historyEvent("first_seen",p.itemKey,{{"position",p.position},{"title",p.title},{"entry_key",p.entryKey}}));
        }
        if(hadPrior&&(previous.position!=p.position||previous.availability!=p.availability||previous.title!=p.title))
            record(historyEvent("observation_changed",p.itemKey,{{"entry_key",p.entryKey},{"position",p.position},{"title",p.title},{"availability",p.availability},{"previous_position",previous.position},{"previous_title",previous.title},{"previous_availability",previous.availability}}));

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
            if(isUnavailable(p.availability) && (c.video.state!="complete" || c.audio.state!="complete")) c.recoveryStatus="unrecovered";
            if(c.video.state=="complete"&&c.audio.state=="complete") c.recoveryStatus="not_required";
        }
        if(isUnavailable(p.availability)) ++summary.unavailable;
        result.append(p);
    }

    for(const auto& old:prior){
        if(observedKeys.contains(old.entryKey)) continue;
        auto p=old;
        if(snapshot.complete){
            if(p.membership!="removed"){
                p.membership="removed";
                record(historyEvent("membership_removed",p.itemKey,{{"last_position",p.lastPosition},{"title",p.title},{"entry_key",p.entryKey}}));
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
    auto sources=loadSources(&error);
    if(!error.isEmpty()){summary.error=error;return summary;}
    bool sourceFound=false;
    for(auto& s:sources) if(s.key==source.key){s=source; sourceFound=true; break;}
    if(!sourceFound) sources.append(source);

    const auto canonicalJson=vectorToArray(canonical,canonicalToJson);
    const auto playlistJson=vectorToArray(result,playlistItemToJson);
    const auto sourcesJson=vectorToArray(sources,sourceToJson);
    if(!detail::arrayShape(canonicalJson,"canonical",&error)||!detail::arrayShape(playlistJson,"playlist",&error)||!detail::arrayShape(sourcesJson,"source",&error)){summary.error=error;return summary;}
    const QString dir="Playlists/"+source.key+"/";
    const QMap<QString,QByteArray> files={{"State/ArchiveMode/items.json",QJsonDocument(canonicalJson).toJson()},
        {"State/ArchiveMode/sources.json",QJsonDocument(sourcesJson).toJson()},
        {dir+"items.json",QJsonDocument(playlistJson).toJson()},
        {dir+"playlist.json",QJsonDocument(sourceToJson(source)).toJson()},
        {dir+"history.jsonl",history}};
    if(!detail::commitTransaction(m_paths.root(),files,&error)){summary.error=error;return summary;}
    if(!writeAllProjections(&error)){ summary.error="State committed, projection rebuild required: "+error; return summary; }

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
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
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
        const QString displayTitle=canon.title.isEmpty()?item.title:canon.title;
        QString playlistTitle=displayTitle;playlistTitle.replace('\n',' ');playlistTitle.replace('\r',' ');
        const QStringList cols={QString::number(item.position),displayTitle,item.providerId,item.url,item.membership,item.availability,
            statusForRep(canon.video),statusForRep(canon.audio),canon.recoveryStatus,item.firstSeen,item.lastSeen,canon.video.path,canon.audio.path};
        QStringList quoted; for(const auto& x:cols) quoted<<csv(x); catalog+=quoted.join(',').toUtf8()+"\n";
        const bool needs=(canon.video.state!="complete"||canon.audio.state!="complete") && (isUnavailable(item.availability)||canon.recoveryStatus=="unrecovered");
        if(needs){
            const QStringList mc={QString::number(item.lastPosition),displayTitle,item.providerId,item.url,item.membership,item.availability,
                                  statusForRep(canon.video),statusForRep(canon.audio),canon.recoveryStatus,item.firstSeen,item.lastSeen};
            QStringList mq; for(const auto& x:mc) mq<<csv(x); missing+=mq.join(',').toUtf8()+"\n";
        }
        if(item.membership=="active" && canon.video.state=="complete" && m_paths.isSafeRelative(canon.video.path)){
            video+=("#EXTINF:-1,"+playlistTitle+"\n"+QDir::fromNativeSeparators(QDir(m_paths.sourceDir(sourceKey)).relativeFilePath(m_paths.absoluteFromRelative(canon.video.path)))+"\n").toUtf8();
        }
        if(item.membership=="active" && canon.audio.state=="complete" && m_paths.isSafeRelative(canon.audio.path)){
            audio+=("#EXTINF:-1,"+playlistTitle+"\n"+QDir::fromNativeSeparators(QDir(m_paths.sourceDir(sourceKey)).relativeFilePath(m_paths.absoluteFromRelative(canon.audio.path)))+"\n").toUtf8();
        }
        if(isUnavailable(item.availability)) unavailable+=QJsonDocument(playlistItemToJson(item)).toJson(QJsonDocument::Compact)+"\n";
        if(item.membership=="removed") removed+=QJsonDocument(playlistItemToJson(item)).toJson(QJsonDocument::Compact)+"\n";
    }
    const QDir d(m_paths.sourceDir(sourceKey));
    return atomicWrite(d.filePath("catalog.csv"),catalog,error) && atomicWrite(d.filePath("missing.csv"),missing,error) &&
           atomicWrite(d.filePath("video.m3u8"),video,error) && atomicWrite(d.filePath("audio.m3u8"),audio,error) &&
           atomicWrite(d.filePath("unavailable.jsonl"),unavailable,error) && atomicWrite(d.filePath("removed.jsonl"),removed,error);
}

bool Store::writeAllProjections(QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    for(const auto& dir:QDir(m_paths.playlists()).entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(!detail::sourceKeySafe(dir)||!detail::noLinks(m_paths.sourceDir(dir)))return detail::reject(error,"Unsafe playlist projection directory");
        if(QFileInfo::exists(m_paths.playlistItemsFile(dir))&&!writeProjections(dir,error))return false;
    }
    const auto marker=QDir(m_paths.archiveState()).filePath("projections-dirty.json");
    if(QFileInfo::exists(marker)&&!QFile::remove(marker))return detail::reject(error,"Cannot clear projection rebuild marker");
    return true;
}

bool Store::writeReceipt(const QString& packageDir,const QJsonObject& receipt,QString* error) const
{
    SyncLock lock(m_paths);if(!lock.tryLock())return detail::reject(error,lock.errorString());
    return atomicWrite(QDir(packageDir).filePath("receipt.json"),QJsonDocument(receipt).toJson(QJsonDocument::Indented),error);
}

ToolResolver::ToolResolver(RuntimeConfig config):m_config(std::move(config)){}

QString ToolResolver::find(const QStringList& names,const QStringList& relativeCandidates) const
{
    for(const auto& rel:relativeCandidates){
        const auto p=QDir(m_config.appDir).filePath(rel);
        if(QFileInfo(p).exists() && QFileInfo(p).isFile() && QFileInfo(p).isExecutable()) return QDir::cleanPath(p);
    }
    for(const auto& name:names){ const auto p=QStandardPaths::findExecutable(name); if(!p.isEmpty()) return p; }
    return {};
}

QString ToolResolver::ytDlp() const
{
#ifdef Q_OS_WIN
    return find({"yt-dlp.exe","yt-dlp"},{"bin/yt-dlp.exe","local/bin/yt-dlp.exe","yt-dlp.exe"});
#else
    return find({"yt-dlp"},{"bin/yt-dlp","local/bin/yt-dlp","yt-dlp"});
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
    return find({"deno.exe","deno"},{"bin/deno.exe","local/bin/deno.exe","deno.exe"});
#else
    return find({"deno"},{"bin/deno","local/bin/deno","deno"});
#endif
}

QString sourceKeyFromUrl(const QString& text)
{
    const QUrl url(text.trimmed());const auto host=url.host().toLower();
    if((url.scheme()!="https"&&url.scheme()!="http")||!url.userInfo().isEmpty()||!(host=="youtube.com"||host.endsWith(".youtube.com")))return {};
    const auto id=QUrlQuery(url).queryItemValue("list");return detail::sourceKeySafe(id)?id:QString();
}
QString videoIdFromUrl(const QString& text)
{
    const QUrl url(text.trimmed());const auto host=url.host().toLower();
    if((url.scheme()!="https"&&url.scheme()!="http")||!url.userInfo().isEmpty())return {};
    QString id;
    if(host=="youtu.be")id=url.path().section('/',1,1);
    else if(host=="youtube.com"||host.endsWith(".youtube.com")){
        if(url.path()=="/watch")id=QUrlQuery(url).queryItemValue("v");
        else if(QStringList{"shorts","live","embed"}.contains(url.path().section('/',1,1)))id=url.path().section('/',2,2);
    }
    return detail::videoIdSafe(id)?id:QString();
}

QString availabilityFromEntry(const QJsonObject& e)
{
    QString a=e.value("availability").toString().toLower();
    const QString title=e.value("title").toString().toLower();
    if(title=="[deleted video]"||title=="deleted video") return "deleted";
    if(title=="[private video]"||title=="private video") return "private";
    if(a=="public"||a=="unlisted") return "public";
    if(a=="private") return "private";
    if(a=="premium_only"||a=="subscriber_only") return "members_only";
    if(a=="needs_auth") return "login_required";
    if(a=="unavailable") return "unavailable";
    return a.isEmpty()?"public":a;
}

QString placeholderFingerprint(const QString& sourceKey,const QString& title,const QString& url)
{
    const auto normalize=[](QString value){return value.trimmed().normalized(QString::NormalizationForm_KC).toCaseFolded();};
    return sourceKey+"|"+normalize(title)+"|"+normalize(url);
}

QString placeholderBaseKey(const QString& sourceKey,const QString& title,const QString& url)
{
    const auto material=placeholderFingerprint(sourceKey,title,url).toUtf8();
    return "placeholder:"+sourceKey+":"+QString::fromLatin1(QCryptographicHash::hash(material,QCryptographicHash::Sha1).toHex().left(16));
}

QString canonicalKey(const QString& providerId,const QString& sourceKey,int,const QString& title)
{
    if(!providerId.trimmed().isEmpty()) return "youtube:"+providerId.trimmed();
    // Position is mutable playlist metadata, not identity. Unresolved
    // duplicate occurrences are separated during reconciliation.
    return placeholderBaseKey(sourceKey,title);
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
    if(sourceKeyFromUrl(source.url)!=source.key){s.error="Invalid playlist source URL or identity";return s;}
    ToolResolver tools(m_config);
    const auto exe=tools.ytDlp();
    m_logger.event("INFO","discovery","scan_started",{{"source_key",source.key},{"url",source.url}});
    QStringList args={"--ignore-config","--flat-playlist","--dump-single-json","--skip-download","--ignore-errors"};
    appendYtRuntimeArgs(args,tools);
    args << "--" << source.url;
    const auto r=runProcess(exe,args,m_config.archiveRoot,300000);
    s=parse(source,r.standardOutput.toUtf8(),r.standardError,r.exitCode);
    s.scannedAt=nowIso();
    if(!r.ok){s.complete=false;if(s.error.isEmpty())s.error=r.error;}
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
    if(!root.value("entries").isArray()){s.error="Playlist entries must be an array; removal inference disabled";return s;}
    const auto entries=root.value("entries").toArray();
    bool malformed=root.contains("id")&&!source.key.isEmpty()&&root.value("id").toString()!=source.key;
    int pos=0;
    for(const auto& value:entries){
        ++pos;
        if(!value.isObject()){malformed=true;continue;}
        const auto e=value.toObject();
        PlaylistItem p;
        p.position=e.value("playlist_index").toInt(pos);
        p.providerId=e.value("id").toString();
        if(!p.providerId.isEmpty()&&!detail::videoIdSafe(p.providerId)){malformed=true;continue;}
        if(p.position<1){malformed=true;continue;}
        p.title=e.value("title").toString();
        if(p.title.isEmpty()) p.title="[Unavailable item]";
        p.url=e.value("webpage_url").toString();
        if(p.url.isEmpty()) p.url=e.value("url").toString();
        if(!p.providerId.isEmpty()) p.url="https://www.youtube.com/watch?v="+p.providerId;
        p.availability=availabilityFromEntry(e);
        p.itemKey=p.providerId.isEmpty()?placeholderBaseKey(source.key,p.title,p.url):canonicalKey(p.providerId,source.key,p.position,p.title);
        s.items.append(p);
    }
    const bool transient=isTransientText(stderrText);
    bool truncated=false;
    for(const auto& key:QStringList{"playlist_count","n_entries"}){const auto v=root.value(key);if(v.isDouble()&&v.toDouble()>entries.size())truncated=true;}
    const bool reportedError=stderrText.contains(QRegularExpression("(?im)^\\s*(?:ERROR|WARNING):"));
    s.complete=!transient && !malformed && !truncated && !reportedError && exitCode==0;
    if(malformed||truncated||reportedError)s.error="Incomplete or suspect discovery output; removal inference disabled";
    if(transient) s.error="Transient discovery failure detected; removal inference disabled";
    else if(exitCode!=0) s.error=QString("yt-dlp exit %1; partial observations retained but removal inference disabled").arg(exitCode);
    return s;
}

MediaVerifier::MediaVerifier(RuntimeConfig c,ActivityLogger& l):m_config(std::move(c)),m_logger(l){}
ValidationResult MediaVerifier::verifyVideo(const QString& p) const{return probe(p,true);}
ValidationResult MediaVerifier::verifyAudio(const QString& p) const{return probe(p,false);}

ValidationResult MediaVerifier::probe(const QString& relativePath,bool video) const
{
    ValidationResult result;Paths paths(m_config.archiveRoot);
    const auto absolute=paths.absoluteFromRelative(relativePath);const QFileInfo file(absolute);
    if(absolute.isEmpty()||!file.isFile()||file.size()==0){result.errors<<"Missing, empty or unsafe media path";return result;}
    if(video?file.suffix().toLower()!="mp4":!QStringList{"m4a","mp4"}.contains(file.suffix().toLower()))result.errors<<"Media has the wrong container extension";
    ToolResolver tools(m_config);
    const auto process=runProcess(tools.ffprobe(),{"-v","error","-show_streams","-show_format","-of","json",absolute},m_config.archiveRoot,60000);
    if(!process.ok||!process.standardError.trimmed().isEmpty()){result.errors<<(process.error+": "+process.standardError.left(1000));return result;}
    QString parseError;const auto doc=parseJson(process.standardOutput.toUtf8(),&parseError);
    if(!doc.isObject()){result.errors<<("Invalid ffprobe JSON: "+parseError);return result;}
    const auto format=doc.object().value("format").toObject();const auto names=format.value("format_name").toString().split(',');
    if(!names.contains("mov")&&!names.contains("mp4")&&!names.contains("m4a"))result.errors<<"Media is not an MP4-family container";
    bool durationOk=false;const auto duration=format.value("duration").toString().toDouble(&durationOk);
    if(!durationOk||!std::isfinite(duration)||duration<=0)result.errors<<"Media has no finite positive duration";
    int videos=0,audios=0;
    for(const auto& value:doc.object().value("streams").toArray()){
        const auto stream=value.toObject();const auto type=stream.value("codec_type").toString();
        if(type=="video"&&stream.value("disposition").toObject().value("attached_pic").toInt()==0){
            ++videos;
            if(stream.value("codec_name")!="h264"||stream.value("pix_fmt")!="yuv420p"||stream.value("width").toInt()<=0||stream.value("height").toInt()<=0||stream.value("height").toInt()>1080)result.errors<<"Video must be H.264/yuv420p at no more than 1080 lines";
        }
        if(type=="audio"){++audios;if(stream.value("codec_name")!="aac")result.errors<<"Every audio stream must be AAC";}
    }
    if(video&&videos==0)result.errors<<"No video stream";
    if(!video&&(audios==0||videos!=0))result.errors<<"Audio representation needs AAC audio without a moving-video stream";
    if(!result.errors.isEmpty())return result;

    // Metadata can survive a truncated fast-start MP4. Decode every canonical
    // media stream before allowing the representation to become complete.
    const auto ffmpeg=tools.ffmpeg();
    if(ffmpeg.isEmpty()){result.errors<<"FFmpeg is required for full media integrity validation";return result;}
    QStringList integrityArgs={"-hide_banner","-nostdin","-v","error","-xerror","-i",absolute};
    if(video) integrityArgs<<"-map"<<"0:v?"<<"-map"<<"0:a?";
    else integrityArgs<<"-map"<<"0:a?";
    integrityArgs<<"-f"<<"null"<<"-";
    const auto integrity=runProcess(ffmpeg,integrityArgs,m_config.archiveRoot,10*60*1000);
    if(!integrity.ok){
        const auto detail=integrity.standardError.trimmed().left(1600);
        result.errors<<("Full media integrity decode failed: "+(detail.isEmpty()?integrity.error:detail));
        return result;
    }
    result.ok=true;return result;
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
    if(!detail::videoIdSafe(id)) return {};
    QString fallback;
    QDirIterator it(QDir(m_config.archiveRoot).filePath(relativeDir),QDir::Files,QDirIterator::Subdirectories);
    while(it.hasNext()){
        const auto p=it.next(); const QFileInfo fi(p);
        if(!fi.fileName().contains("["+id+"]")) continue;
        if(!extensions.isEmpty() && !extensions.contains(fi.suffix().toLower())) continue;
        const auto rel=Paths(m_config.archiveRoot).relativeToRoot(p);
        if(!Paths(m_config.archiveRoot).isSafeRelative(rel))continue;
        if(fallback.isEmpty())fallback=rel;
        const auto verified=relativeDir=="Video"?m_verifier.verifyVideo(rel):m_verifier.verifyAudio(rel);
        if(verified.ok)return rel;
    }
    return fallback;
}

bool MediaExecutor::downloadVideo(const CanonicalItem& item,QString* error)
{
    if(item.providerId.isEmpty()){if(error)*error="No provider ID";return false;}
    Representation running=item.video; running.state="running"; running.origin="automatic_download";
    if(!m_store.updateRepresentation(item.key,"video",running,error))return false;
    m_logger.event("INFO","download","video_started",{{"item_key",item.key}});
    const auto fail=[&](const QString& message,const QString& path=QString()){
        Representation failed=running; failed.state="failed"; failed.path=path; failed.error=message;
        m_store.updateRepresentation(item.key,"video",failed,nullptr);
        if(error) *error=message;
        m_logger.event("ERROR","download","video_failed",{{"item_key",item.key},{"error",message}});
        return false;
    };
    const QString url=item.originalUrl.isEmpty()?"https://www.youtube.com/watch?v="+item.providerId:item.originalUrl;
    QString output="Video/%(title).120s [%(artist|UNKNOWN)s] [%(height)sp %(duration)ss %(upload_date>%y%m%d|UNKNOWN)s] [%(id)s].%(ext)s";
    const auto previous=findExistingById("Video",item.providerId,{"mp4","mkv","webm"});
    if(!previous.isEmpty())output.replace(".%(ext)s"," [repair-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+"].%(ext)s");
    QStringList args={
        "--ignore-config","--no-playlist","--no-overwrites","--output-na-placeholder","NA",
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
    args << "--" << url;
    if(!previous.isEmpty())args=withoutDownloadArchive(args);
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
        const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto tempRel="Temp/normalize-"+token+".mp4";const auto tmp=Paths(m_config.archiveRoot).absoluteFromRelative(tempRel);
        const QStringList fargs={"-nostdin","-n","-i",input,"-map","0:v:0","-map","0:a:0?","-map","0:s?","-map_metadata","0","-map_chapters","0",
            "-vf","scale=w='min(1920,iw)':h='min(1080,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2",
            "-c:v","libx264","-preset","medium","-crf","18","-pix_fmt","yuv420p","-c:a","aac","-b:a","192k","-c:s","mov_text",tmp};
        const auto tr=run(m_tools.ffmpeg(),fargs,"video-normalization");
        if(!tr.ok)return fail("Conditional video normalization failed: "+tr.standardError.left(700),rel);
        const auto staged=m_verifier.verifyVideo(tempRel);if(!staged.ok)return fail("Normalized video failed verification: "+staged.errors.join("; "),rel);
        const auto normalized="Video/"+safeFilePart(item.title)+" [NORMALIZED] ["+item.providerId+"] ["+token.left(8)+"].mp4";
        const auto destination=Paths(m_config.archiveRoot).absoluteFromRelative(normalized);
        if(destination.isEmpty()||QFileInfo::exists(destination)||!QFile::rename(tmp,destination))return fail("Unable to publish verified normalized video; original preserved",rel);
        rel=normalized;verify=m_verifier.verifyVideo(rel);
    }
    if(!verify.ok) return fail("Video verification failed: "+verify.errors.join("; "),rel);
    Representation done; done.state="complete"; done.path=rel; done.origin="automatic_download"; done.verifiedAt=nowIso();
    if(!m_store.updateRepresentation(item.key,"video",done,error))return false;
    m_logger.event("INFO","verification","video_complete",{{"item_key",item.key},{"path",rel}});
    return true;
}

bool MediaExecutor::downloadAudio(const CanonicalItem& item,QString* error)
{
    if(item.providerId.isEmpty()){if(error)*error="No provider ID";return false;}
    Representation running=item.audio; running.state="running"; running.origin="automatic_download";
    if(!m_store.updateRepresentation(item.key,"audio",running,error))return false;
    m_logger.event("INFO","download","audio_started",{{"item_key",item.key}});
    const auto fail=[&](const QString& message,const QString& path=QString()){
        Representation failed=running; failed.state="failed"; failed.path=path; failed.error=message;
        m_store.updateRepresentation(item.key,"audio",failed,nullptr);
        if(error) *error=message;
        m_logger.event("ERROR","download","audio_failed",{{"item_key",item.key},{"error",message}});
        return false;
    };
    const QString url=item.originalUrl.isEmpty()?"https://www.youtube.com/watch?v="+item.providerId:item.originalUrl;
    const auto previous=findExistingById("Audio",item.providerId,{"m4a","mp4"});
    QStringList args={"--ignore-config","--no-playlist","--no-overwrites","--output-na-placeholder","NA","-f","ba[ext=m4a]/ba","--paths","temp:Temp",
        "-o","Audio/%(title).120s [%(artist|UNKNOWN)s] [m4a %(duration)ss %(upload_date>%y%m%d|UNKNOWN)s] [%(id)s].%(ext)s",
        "--download-archive","State/audio-archive.txt","--windows-filenames","-x","--audio-format","m4a","--audio-quality","192K","--no-keep-video",
        "--embed-metadata","--embed-thumbnail","--embed-chapters","--parse-metadata","%(artist|UNKNOWN)s:%(meta_artist)s",
        "--print-to-file","after_move:%(.{id,title,artist,meta_artist,uploader,upload_date,duration,ext,webpage_url,filepath})j","State/audio-catalog.jsonl"};
    appendYtRuntimeArgs(args,m_tools);
    args << "--" << url;
    if(!previous.isEmpty()){
        const auto outputIndex=args.indexOf("-o")+1;
        args[outputIndex].replace(".%(ext)s"," [repair-"+QUuid::createUuid().toString(QUuid::WithoutBraces).left(8)+"].%(ext)s");
        args=withoutDownloadArchive(args);
    }
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
    auto verify=m_verifier.verifyAudio(rel);
    if(!verify.ok){
        const auto token=QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto tempRel="Temp/normalize-"+token+".m4a";const auto tmp=Paths(m_config.archiveRoot).absoluteFromRelative(tempRel);
        const auto input=Paths(m_config.archiveRoot).absoluteFromRelative(rel);
        const auto result=run(m_tools.ffmpeg(),{"-nostdin","-n","-i",input,"-map","0:a:0","-vn","-map_metadata","0","-map_chapters","0","-c:a","aac","-b:a","192k",tmp},"audio-normalization");
        const auto staged=m_verifier.verifyAudio(tempRel);
        if(!result.ok||!staged.ok)return fail("Audio normalization/verification failed: "+result.error+" "+staged.errors.join("; "),rel);
        const auto normalized="Audio/"+safeFilePart(item.title)+" [NORMALIZED] ["+item.providerId+"] ["+token.left(8)+"].m4a";
        const auto destination=Paths(m_config.archiveRoot).absoluteFromRelative(normalized);
        if(destination.isEmpty()||QFileInfo::exists(destination)||!QFile::rename(tmp,destination))return fail("Audio publication failed; original preserved",rel);
        rel=normalized;verify=m_verifier.verifyAudio(rel);
    }
    if(!verify.ok)return fail("Audio verification failed: "+verify.errors.join("; "),rel);
    Representation done; done.state="complete"; done.path=rel; done.origin="automatic_download"; done.verifiedAt=nowIso();
    if(!m_store.updateRepresentation(item.key,"audio",done,error))return false;
    m_logger.event("INFO","verification","audio_complete",{{"item_key",item.key},{"path",rel}});
    return true;
}

bool MediaExecutor::syncItem(const CanonicalItem& requested,bool wantVideo,bool wantAudio,QString* error)
{
    SyncLock lock(m_store.paths());if(!lock.tryLock())return detail::reject(error,lock.errorString());
    if(!m_store.initialize(error))return false;
    QString stateError;const auto current=m_store.loadCanonicalItems(&stateError);if(!stateError.isEmpty())return detail::reject(error,stateError);
    CanonicalItem item;bool found=false;for(const auto& c:current)if(c.key==requested.key){item=c;found=true;break;}
    if(!found||!detail::videoIdSafe(item.providerId))return detail::reject(error,"Missing or invalid canonical download target");
    QStringList errors;
    for(const auto& kind:QStringList{"video","audio"}){
        if((kind=="video"&&!wantVideo)||(kind=="audio"&&!wantAudio))continue;
        auto representation=kind=="video"?item.video:item.audio;
        const auto check=[&](const QString& p){return kind=="video"?m_verifier.verifyVideo(p):m_verifier.verifyAudio(p);};
        if(representation.state=="complete"&&check(representation.path).ok)continue;
        // A state flag is not evidence that the file still exists or is readable.
        const auto existing=findExistingById(kind=="video"?"Video":"Audio",item.providerId,kind=="video"?QStringList{"mp4","mkv","webm"}:QStringList{"m4a","mp4"});
        if(!existing.isEmpty()&&check(existing).ok){
            representation.state="complete";representation.path=existing;representation.verifiedAt=nowIso();representation.error.clear();
            if(representation.origin.isEmpty())representation.origin="existing_archive";
            QString e;if(!m_store.updateRepresentation(item.key,kind,representation,&e))errors<<e;continue;
        }
        if(representation.state=="complete"){
            representation.state="failed";representation.error="Previously complete media is missing or failed verification";
            QString e;if(!m_store.updateRepresentation(item.key,kind,representation,&e)){errors<<e;continue;}
        }
        if(isUnavailable(item.availability)){errors<<kind+": source is unavailable; external recovery required";continue;}
        QString e;const bool ok=kind=="video"?downloadVideo(item,&e):downloadAudio(item,&e);if(!ok)errors<<kind+": "+e;
    }
    if(!m_store.writeAllProjections(&stateError))errors<<stateError;
    if(error)*error=errors.join(" | ");return errors.isEmpty();
}

bool MediaExecutor::syncItems(const QVector<CanonicalItem>& items,const std::function<bool()>& shouldStop,QStringList* failures)
{
    bool all=true;
    for(const auto& item:items){
        if(shouldStop && shouldStop()){all=false;if(failures)failures->append("Stopped before all items completed");break;}
        QString e;
        if(!syncItem(item,true,true,&e)){ all=false; if(failures) failures->append(item.key+": "+e); }
    }
    return all;
}

RecoveryImporter::RecoveryImporter(RuntimeConfig c,Store& s,ActivityLogger& l):m_config(std::move(c)),m_store(s),m_logger(l),m_tools(m_config),m_verifier(m_config,l){}

ValidationResult RecoveryImporter::validate(const QString& packageDir) const
{
    ValidationResult r;SyncLock lock(m_store.paths());
    if(!lock.tryLock()){r.errors<<lock.errorString();return r;}
    const QFileInfo dirInfo(packageDir);
    if(!dirInfo.isDir()||!detail::noLinks(packageDir)){r.errors<<"Package is not an unlinked directory";return r;}
    QByteArray bytes;QString pe;
    if(!detail::readBytes(QDir(packageDir).filePath("manifest.json"),&bytes,&pe)){r.errors<<pe;return r;}
    if(bytes.size()>1024*1024){r.errors<<"Manifest exceeds the 1 MiB safety limit";return r;}
    const auto doc=parseJson(bytes,&pe);
    if(!doc.isObject()){r.errors<<("Invalid manifest.json: "+pe);return r;}
    const auto o=doc.object();
    if(o.value("schema_version")!=QJsonValue(1))r.errors<<"schema_version must be the number 1";
    r.packageId=o.value("package_id").toString();
    if(!detail::sourceKeySafe(r.packageId)||r.packageId!=dirInfo.fileName())r.errors<<"package_id must match its safe directory name (letters, digits, underscore or hyphen, 1 to 160 characters)";
    if(detail::sourceKeySafe(r.packageId)&&QFileInfo::exists(QDir(m_store.paths().importsAccepted()).filePath(r.packageId)))r.errors<<"An accepted package already has this identity; existing evidence is immutable";
    const auto target=o.value("target").toObject();const auto id=target.value("youtube_id").toString();r.itemKey=target.value("item_key").toString();
    if(r.itemKey.isEmpty()&&detail::videoIdSafe(id))r.itemKey="youtube:"+id;
    if(r.itemKey.isEmpty())r.errors<<"target.item_key or a valid target.youtube_id is required";
    if(target.contains("youtube_id")&&!detail::videoIdSafe(id))r.errors<<"target.youtube_id must be an 11-character YouTube ID";
    const auto provenance=o.value("provenance").toObject();
    if(provenance.value("method").toString().trimmed().isEmpty())r.errors<<"provenance.method is required";
    if(provenance.contains("confidence")&&!QStringList{"low","medium","high","verified"}.contains(provenance.value("confidence").toString()))r.errors<<"Invalid provenance.confidence";
    for(const auto& key:QStringList{"recovered_by","recovered_at","source_url","notes"})if(provenance.contains(key)&&!provenance.value(key).isString())r.errors<<("provenance."+key+" must be a string");
    const auto reps=o.value("representations").toObject();
    if(o.contains("representations")&&!o.value("representations").isObject())r.errors<<"representations must be an object";
    for(auto it=reps.begin();it!=reps.end();++it){
        if(it.key()!="video"&&it.key()!="audio"){r.errors<<"Unsupported representation kind";continue;}
        const auto file=it.value().toObject().value("file").toString();
        const auto path=QDir(packageDir).filePath(file);
        if(!it.value().isObject()||!detail::relativeSafe(file)||!detail::noLinks(path)||!QFileInfo(path).isFile()||QFileInfo(path).size()==0)r.errors<<(it.key()+" must name a nonempty, unlinked, package-relative regular file");
    }
    const auto metadata=o.value("metadata").toObject();
    if(o.contains("metadata")&&!o.value("metadata").isObject())r.errors<<"metadata must be an object";
    for(const auto& key:QStringList{"canonical_title","source_title","original_url"})if(metadata.contains(key)&&!metadata.value(key).isString())r.errors<<("metadata."+key+" must be a string");
    for(const auto& key:QStringList{"source_tags","user_tags"})if(metadata.contains(key)){
        if(!metadata.value(key).isArray())r.errors<<("metadata."+key+" must be an array");
        else for(const auto& tag:metadata.value(key).toArray())if(!tag.isString())r.errors<<("metadata."+key+" entries must be strings");
    }
    bool evidence=false;const auto evidenceDir=QDir(packageDir).filePath("evidence");
    if(QFileInfo::exists(evidenceDir)){
        if(!detail::noLinks(evidenceDir)||!QFileInfo(evidenceDir).isDir())r.errors<<"Unsafe evidence directory";
        else {QDirIterator it(evidenceDir,QDir::AllEntries|QDir::NoDotAndDotDot,QDirIterator::Subdirectories);while(it.hasNext()){const auto path=it.next();if(!detail::noLinks(path))r.errors<<"Linked evidence is forbidden";else if(QFileInfo(path).isFile())evidence=true;}}
    }
    if(reps.isEmpty()&&metadata.isEmpty()&&!evidence)r.errors<<"Package contains no media, metadata, or evidence";
    QString stateError;const auto canonical=m_store.loadCanonicalItems(&stateError);if(!stateError.isEmpty())r.errors<<stateError;
    bool found=false;
    for(const auto& i:canonical)if(i.key==r.itemKey){found=true;
        if(!id.isEmpty()&&id!=i.providerId)r.errors<<"target.item_key and target.youtube_id disagree";
        const auto original=metadata.value("original_url").toString();
        if(!original.isEmpty()&&videoIdFromUrl(original)!=i.providerId)r.errors<<"metadata.original_url must identify the historical YouTube item";
        if(reps.contains("video")&&i.video.state=="complete")r.errors<<"Canonical video is already complete";
        if(reps.contains("audio")&&i.audio.state=="complete")r.errors<<"Canonical audio is already complete";
        break;
    }
    if(!found)r.errors<<"Target does not exist in canonical state";
    r.ok=r.errors.isEmpty();return r;
}

bool RecoveryImporter::normalizeVideo(const QString& input,const QString& output,QString* error) const
{
    const QStringList args={"-nostdin","-n","-i",input,"-map","0:v:0","-map","0:a:0?","-map_metadata","0","-map_chapters","0",
                            "-vf","scale=w='min(1920,iw)':h='min(1080,ih)':force_original_aspect_ratio=decrease:force_divisible_by=2",
                            "-c:v","libx264","-preset","medium","-crf","18","-pix_fmt","yuv420p","-c:a","aac","-b:a","192k",output};
    const auto r=runProcess(m_tools.ffmpeg(),args,m_config.archiveRoot,60*60*1000);
    if(!r.ok){if(error)*error=r.error+" "+r.standardError.left(1000);return false;} return true;
}

bool RecoveryImporter::normalizeAudio(const QString& input,const QString& output,QString* error) const
{
    const QStringList args={"-nostdin","-n","-i",input,"-vn","-map_metadata","0","-map_chapters","0","-c:a","aac","-b:a","192k",output};
    const auto r=runProcess(m_tools.ffmpeg(),args,m_config.archiveRoot,60*60*1000);
    if(!r.ok){if(error)*error=r.error+" "+r.standardError.left(1000);return false;} return true;
}

bool RecoveryImporter::ingest(const QString& packageDir,QString* error)
{
    SyncLock lock(m_store.paths());if(!lock.tryLock())return detail::reject(error,lock.errorString());
    if(!m_store.initialize(error))return false;
    const auto absolute=QFileInfo(packageDir).absoluteFilePath();
    const auto pending=m_store.paths().importsPending();
    if(QFileInfo(absolute).absolutePath()!=pending||!detail::noLinks(absolute))return detail::reject(error,"Only direct, unlinked Pending packages can be ingested");
    const auto vr=validate(absolute);if(!vr.ok)return detail::reject(error,vr.errors.join("; "));
    QByteArray manifestBytes;if(!detail::readBytes(QDir(absolute).filePath("manifest.json"),&manifestBytes,error))return false;
    const auto manifest=QJsonDocument::fromJson(manifestBytes).object();const auto reps=manifest.value("representations").toObject();
    QString stateError;auto items=m_store.loadCanonicalItems(&stateError);if(!stateError.isEmpty())return detail::reject(error,stateError);
    int index=-1;for(int i=0;i<items.size();++i)if(items[i].key==vr.itemKey){index=i;break;}
    if(index<0)return detail::reject(error,"Canonical target disappeared");
    auto& target=items[index];
    const auto metadata=manifest.value("metadata").toObject();
    const auto title=metadata.value("canonical_title").toString().trimmed();if(!title.isEmpty())target.title=title;
    // original_url remains the historical source identity. Mirror URLs stay in provenance.
    if(target.originalUrl.isEmpty()&&!metadata.value("original_url").toString().isEmpty())target.originalUrl="https://www.youtube.com/watch?v="+target.providerId;
    for(const auto& tag:metadata.value("user_tags").toArray())if(!target.userTags.contains(tag.toString()))target.userTags.append(tag.toString());
    const auto transactionId=QUuid::createUuid().toString(QUuid::WithoutBraces);
    QTemporaryDir staging(QDir(m_store.paths().temp()).filePath("import-"+transactionId+"-XXXXXX"));
    if(!staging.isValid())return detail::reject(error,"Cannot create safe import staging directory");
    const auto stage=staging.path();const auto stageRel=m_store.paths().relativeToRoot(stage);
    const auto base=safeFilePart(target.title)+" [RECOVERED] ["+safeFilePart(target.providerId.isEmpty()?vr.itemKey:target.providerId)+"] ["+detail::digest(vr.packageId.toUtf8()).left(12)+"]";
    QJsonArray moves,promoted;QMap<QString,QString> inputHashes;
    for(const auto& kind:QStringList{"video","audio"}){
        if(!reps.contains(kind))continue;
        const auto input=QDir(absolute).filePath(reps.value(kind).toObject().value("file").toString());
        const auto inputHash=detail::fileDigest(input,error);if(inputHash.isEmpty())return false;inputHashes[input]=inputHash;
        const auto stagedRel=stageRel+"/"+kind+(kind=="video"?".mp4":".m4a");
        const auto output=m_store.paths().absoluteFromRelative(stagedRel);
        const auto destRel=(kind=="video"?"Video/":"Audio/")+base+(kind=="video"?".mp4":".m4a");
        const auto dest=m_store.paths().absoluteFromRelative(destRel);
        if(dest.isEmpty()||QFileInfo::exists(dest))return detail::reject(error,"Canonical destination collision; existing media was preserved");
        if(kind=="video"?!normalizeVideo(input,output,error):!normalizeAudio(input,output,error))return false;
        const auto verified=kind=="video"?m_verifier.verifyVideo(stagedRel):m_verifier.verifyAudio(stagedRel);
        if(!verified.ok)return detail::reject(error,verified.errors.join("; "));
        const auto hash=detail::fileDigest(output,error);if(hash.isEmpty())return false;
        moves.append(QJsonObject{{"from",stagedRel},{"to",destRel},{"sha256",hash}});promoted.append(destRel);
        Representation done;done.state="complete";done.path=destRel;done.origin="external_recovery";done.verifiedAt=nowIso();
        if(kind=="video")target.video=done;else target.audio=done;
    }
    target.recoveryStatus=target.video.state=="complete"&&target.audio.state=="complete"?"not_required":(isUnavailable(target.availability)?"unrecovered":target.recoveryStatus);
    const auto canonical=vectorToArray(items,canonicalToJson);if(!detail::arrayShape(canonical,"canonical",error))return false;
    for(auto it=inputHashes.begin();it!=inputHashes.end();++it)if(detail::fileDigest(it.key(),error)!=it.value())return detail::reject(error,"Submitted media changed during normalization");
    const auto manifestHash=detail::digest(manifestBytes);
    if(detail::fileDigest(QDir(absolute).filePath("manifest.json"),error)!=manifestHash)return detail::reject(error,"Submitted manifest changed during normalization");
    // Bind every included package file, not just the manifest. Evidence remains inspectable in Accepted.
    QJsonObject evidenceHashes;QDirIterator files(absolute,QDir::Files|QDir::Hidden,QDirIterator::Subdirectories);
    while(files.hasNext()){const auto file=files.next();const auto rel=QDir(absolute).relativeFilePath(file);if(rel=="receipt.json")continue;
        if(!detail::relativeSafe(rel)||!detail::noLinks(file))return detail::reject(error,"Unsafe file in recovery package");
        const auto hash=detail::fileDigest(file,error);if(hash.isEmpty())return false;evidenceHashes[rel]=hash;
    }
    const QJsonObject receipt{{"schema_version",1},{"package_id",vr.packageId},{"item_key",vr.itemKey},{"result","accepted"},{"accepted_at",nowIso()},{"manifest_sha256",manifestHash},{"file_sha256",evidenceHashes},{"promoted_paths",promoted},{"transaction_id",transactionId}};
    QMap<QString,QByteArray> writes={{"State/ArchiveMode/items.json",QJsonDocument(canonical).toJson()},
        {"State/ArchiveMode/Imports/Pending/"+vr.packageId+"/receipt.json",QJsonDocument(receipt).toJson()}};
    for(const auto& sourceDir:QDir(m_store.paths().playlists()).entryList(QDir::Dirs|QDir::NoDotAndDotDot)){
        if(!detail::sourceKeySafe(sourceDir))return detail::reject(error,"Unsafe playlist directory");
        const auto playlist=m_store.loadPlaylistItems(sourceDir,&stateError);if(!stateError.isEmpty())return detail::reject(error,stateError);
        bool contains=false;for(const auto& p:playlist)if(p.itemKey==vr.itemKey)contains=true;if(!contains)continue;
        QByteArray history;const auto file=m_store.paths().playlistHistoryFile(sourceDir);
        if(QFileInfo::exists(file)&&!detail::readBytes(file,&history,error))return false;
        if(!detail::historyValid(history,error))return false;
        history+=QJsonDocument(historyEvent("external_recovery_accepted",vr.itemKey,{{"package_id",vr.packageId},{"manifest_sha256",manifestHash},{"promoted_paths",promoted}})).toJson(QJsonDocument::Compact)+"\n";
        writes["Playlists/"+sourceDir+"/history.jsonl"]=history;
    }
    staging.setAutoRemove(false);
    if(!detail::commitTransaction(m_config.archiveRoot,writes,error,moves,{{"package_id",vr.packageId},{"manifest_sha256",manifestHash},{"file_sha256",evidenceHashes}})){
        if(!QFileInfo::exists(detail::journalPath(m_config.archiveRoot)))staging.setAutoRemove(true);
        return false;
    }
    staging.setAutoRemove(true);
    if(!m_store.writeAllProjections(error))return false;
    m_logger.event("INFO","import","recovery_package_accepted",{{"package_id",vr.packageId},{"item_key",vr.itemKey}});return true;
}

int RecoveryImporter::ingestPending(QStringList* failures,const std::function<bool()>& shouldStop)
{
    SyncLock lock(m_store.paths());if(!lock.tryLock()){if(failures)failures->append(lock.errorString());return 0;}
    QString initError;if(!m_store.initialize(&initError)){if(failures)failures->append(initError);return 0;}
    QDir d(m_store.paths().importsPending());int accepted=0;
    for(const auto& name:d.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name)){
        if(shouldStop&&shouldStop()){if(failures)failures->append("Stopped before all packages completed");break;}
        const auto dir=d.filePath(name);const auto validation=validate(dir);QString error;
        if(validation.ok){
            if(ingest(dir,&error)){++accepted;continue;}
            // Tool, storage and interrupted-transaction failures stay retryable in Pending.
            if(failures)failures->append(name+": retryable failure: "+error);
            m_logger.event("ERROR","import","recovery_package_pending",{{"package_id",name},{"reason",error}});
            if(QFileInfo::exists(detail::journalPath(m_config.archiveRoot)))break;
            continue;
        }
        error=validation.errors.join("; ");
        if(!detail::sourceKeySafe(name)||!detail::noLinks(dir)){if(failures)failures->append(name+": "+error);continue;}
        QJsonObject receipt{{"schema_version",1},{"package_id",name},{"result","rejected"},{"rejected_at",nowIso()},{"reason",error}};
        QString receiptError;
        if(!m_store.writeReceipt(dir,receipt,&receiptError)){if(failures)failures->append(name+": "+receiptError);continue;}
        const auto dest=QDir(m_store.paths().importsRejected()).filePath(name+"-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        if(!QDir().rename(dir,dest))error+="; rejection move failed, package remains Pending";
        if(failures)failures->append(name+": "+error);
        m_logger.event("WARNING","import","recovery_package_rejected",{{"package_id",name},{"reason",error}});
    }
    return accepted;
}

struct ArchiveLockState
{
    explicit ArchiveLockState(const QString& path):file(path),owner(QThread::currentThreadId()){file.setStaleLockTime(0);}
    QLockFile file;
    Qt::HANDLE owner;
};
namespace {
QMutex archiveLocksMutex;
QHash<QString,std::weak_ptr<ArchiveLockState>> archiveLocks;
}
SyncLock::SyncLock(const Paths& paths):m_paths(paths){}
SyncLock::~SyncLock(){unlock();}
bool SyncLock::tryLock(int timeoutMs)
{
    if(m_lock)return true;
    if(!m_paths.ensureLayout(&m_error))return false;
    auto path=QDir(m_paths.archiveState()).filePath("sync.lock");
    if(!detail::noLinks(path)){m_error="Linked Archive lock path refused";return false;}
#ifdef Q_OS_WIN
    path=path.toCaseFolded();
#endif
    QMutexLocker guard(&archiveLocksMutex);
    auto held=archiveLocks.value(path).lock();
    if(held){
        if(held->owner!=QThread::currentThreadId()){m_error="Another Archive operation is active";return false;}
        m_lock=held;return true;
    }
    auto next=std::make_shared<ArchiveLockState>(path);
    if(!next->file.tryLock(qMax(0,timeoutMs))){m_error="Another Archive operation is active or the lock could not be acquired";return false;}
    m_lock=next;archiveLocks[path]=next;m_error.clear();return true;
}
void SyncLock::unlock(){QMutexLocker guard(&archiveLocksMutex);m_lock.reset();}
QString SyncLock::errorString() const{return m_error;}

} // namespace archive
