#ifndef MDPS_ARCHIVESAFETY_H
#define MDPS_ARCHIVESAFETY_H
// Internal filesystem and state integrity helpers. No network or provider access.
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QMap>
#ifdef Q_OS_WIN
#include <windows.h>
#include <winioctl.h>
#ifndef FSCTL_GET_REPARSE_POINT
#define FSCTL_GET_REPARSE_POINT 0x000900A8
#endif
#endif

namespace archive { namespace detail {
inline bool reject(QString* error,const QString& message){if(error)*error=message;return false;}
inline bool sourceKeySafe(const QString& s){return QRegularExpression("^[A-Za-z0-9_-]{1,160}$").match(s).hasMatch();}
inline bool videoIdSafe(const QString& s){return QRegularExpression("^[A-Za-z0-9_-]{11}$").match(s).hasMatch();}
inline bool relativeSafe(const QString& path){
    if(path.isEmpty()||QDir::isAbsolutePath(path)||path.contains('\\'))return false;
    const auto parts=path.split('/');
    const QRegularExpression bad("[\\x00-\\x1F\\x7F:*?\"<>|]");
    const QRegularExpression reserved("^(CON|PRN|AUX|NUL|COM(?:[1-9]|[¹²³])|LPT(?:[1-9]|[¹²³]))(?:\\.|$)",QRegularExpression::CaseInsensitiveOption);
    for(const auto& part:parts)
        if(part.isEmpty()||part=="."||part==".."||part.size()>240||part.endsWith('.')||part.endsWith(' ')||bad.match(part).hasMatch()||reserved.match(part).hasMatch())return false;
    return true;
}
inline bool isPermittedCloudFilesTag(quint32 tag){
    // IO_REPARSE_TAG_CLOUD_0..15 are 0x9000n01A. These are not name-surrogate
    // links and may be traversed as ordinary synchronized Archive ancestors.
    return (tag&0xFFFF0FFFu)==0x9000001Au;
}
#ifdef Q_OS_WIN
inline bool windowsReparseSafe(const QString& path){
    const auto native=QDir::toNativeSeparators(path);
    const auto attributes=GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return true;
    const auto handle=CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()),FILE_READ_ATTRIBUTES,
                                  FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
                                  FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(handle==INVALID_HANDLE_VALUE)return false;
    BYTE buffer[MAXIMUM_REPARSE_DATA_BUFFER_SIZE]{};DWORD returned=0;
    const bool queried=DeviceIoControl(handle,FSCTL_GET_REPARSE_POINT,nullptr,0,buffer,sizeof(buffer),&returned,nullptr)!=FALSE;
    CloseHandle(handle);
    if(!queried||returned<sizeof(quint32))return false;
    const auto tag=*reinterpret_cast<const quint32*>(buffer);
    return isPermittedCloudFilesTag(tag);
}
#endif
inline bool noLinks(const QString& path){
    QString current=QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    for(;;){
        QFileInfo info(current);
        if(info.isSymLink())return false;
#if QT_VERSION >= QT_VERSION_CHECK(6,2,0)
        if(info.isJunction())return false;
#endif
#ifdef Q_OS_WIN
        if(!windowsReparseSafe(current))return false;
#endif
        const auto parent=info.absolutePath();if(parent==current)break;current=parent;
    }
    return true;
}
inline bool readBytes(const QString& path,QByteArray* bytes,QString* error){
    if(!noLinks(path)||!QFileInfo(path).isFile())return reject(error,"Not a safe regular file: "+path);
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return reject(error,"Cannot read "+path+": "+f.errorString());
    *bytes=f.readAll();if(f.error()!=QFileDevice::NoError)return reject(error,"Read failed: "+path);
    return true;
}
inline bool writeBytes(const QString& path,const QByteArray& bytes,QString* error){
    if(!noLinks(path))return reject(error,"Linked path refused: "+path);
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))return reject(error,"Cannot create parent: "+path);
    QSaveFile f(path);f.setDirectWriteFallback(false);
    if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size())return reject(error,"Cannot stage "+path+": "+f.errorString());
    if(!f.commit())return reject(error,"Cannot commit "+path+": "+f.errorString());
    return true;
}
inline QString digest(const QByteArray& data){return QString::fromLatin1(QCryptographicHash::hash(data,QCryptographicHash::Sha256).toHex());}
inline QString fileDigest(const QString& path,QString* error){
    if(!noLinks(path)||!QFileInfo(path).isFile()){reject(error,"Unsafe or missing file: "+path);return {};}
    QFile f(path);if(!f.open(QIODevice::ReadOnly)){reject(error,f.errorString());return {};}
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&f)||f.error()!=QFileDevice::NoError){reject(error,"Unable to hash "+path);return {};}
    return QString::fromLatin1(hash.result().toHex());
}
inline bool arrayShape(const QJsonArray& array,const QString& kind,QString* error){
    QSet<QString> keys;
    const QStringList states={"missing","complete","failed","interrupted","running","blocked_unavailable"};
    for(const auto& entry:array){
        if(!entry.isObject())return reject(error,kind+" contains a non-object record");
        const auto o=entry.toObject();const QString key=o.value(kind=="playlist"?"item_key":"key").toString();
        if(key.isEmpty())return reject(error,kind+" contains an empty identity");
        const QString unique=kind=="source"?key.toCaseFolded():key;
        // A playlist can contain repeated occurrences of the same video.
        if(kind!="playlist" && keys.contains(unique))return reject(error,kind+" contains a duplicate identity: "+key);
        keys.insert(unique);
        if(kind=="source"){
            if(!sourceKeySafe(key)||o.value("url").toString().isEmpty())return reject(error,"Invalid source identity or URL");
        }else{
            const QString id=o.value("provider_id").toString();
            if((!id.isEmpty()&&!videoIdSafe(id))||(!key.startsWith("placeholder:")&&key!="youtube:"+id))return reject(error,"Invalid canonical identity: "+key);
            if(kind=="canonical"){
                for(const auto& kindName:QStringList{"video","audio"}){
                    if(!o.value(kindName).isObject())return reject(error,"Missing representation: "+kindName);
                    const auto r=o.value(kindName).toObject();const auto path=r.value("path").toString();
                    if(!states.contains(r.value("state").toString())||(!path.isEmpty()&&!relativeSafe(path))||(r.value("state")=="complete"&&path.isEmpty()))return reject(error,"Invalid representation: "+kindName);
                    if(!path.isEmpty() && !path.startsWith(kindName=="video"?"Video/":"Audio/"))return reject(error,"Representation outside canonical media directory");
                }
            }else if(o.value("membership")!="active"&&o.value("membership")!="removed")return reject(error,"Invalid playlist membership");
        }
    }
    return true;
}
inline bool readArray(const QString& path,const QString& kind,QJsonArray* array,QString* error,bool missingAllowed=false){
    if(error)error->clear();
    if(missingAllowed&&!QFileInfo::exists(path)&&noLinks(path)){*array={};return true;}
    QByteArray bytes;if(!readBytes(path,&bytes,error))return false;
    QJsonParseError pe;const auto doc=QJsonDocument::fromJson(bytes,&pe);
    if(pe.error!=QJsonParseError::NoError||!doc.isArray())return reject(error,"Invalid "+kind+" state in "+path+": expected a JSON array ("+pe.errorString()+")");
    if(!arrayShape(doc.array(),kind,error))return false;
    *array=doc.array();return true;
}
inline bool historyValid(const QByteArray& bytes,QString* error){
    if(!bytes.isEmpty()&&!bytes.endsWith('\n'))return reject(error,"Incomplete history record");
    for(const auto& line:bytes.split('\n')){
        if(line.trimmed().isEmpty())continue;
        QJsonParseError pe;const auto doc=QJsonDocument::fromJson(line,&pe);
        if(pe.error!=QJsonParseError::NoError||!doc.isObject())return reject(error,"Corrupt history record; original history was preserved");
    }
    return true;
}
inline bool transactionPayload(const QString& path,const QByteArray& after,QString* error){
    if(path.endsWith("/history.jsonl"))return historyValid(after,error);
    QJsonParseError pe;const auto doc=QJsonDocument::fromJson(after,&pe);
    if(pe.error!=QJsonParseError::NoError)return reject(error,"Invalid JSON transaction payload");
    if(path=="State/ArchiveMode/items.json")return doc.isArray()&&arrayShape(doc.array(),"canonical",error);
    if(path=="State/ArchiveMode/sources.json")return doc.isArray()&&arrayShape(doc.array(),"source",error);
    if(path.startsWith("Playlists/")&&path.endsWith("/items.json"))return doc.isArray()&&arrayShape(doc.array(),"playlist",error);
    return doc.isObject();
}
inline QString journalPath(const QString& root){return QDir(root).filePath("State/ArchiveMode/transaction.json");}
inline bool journalTarget(const QString& path){
    if(path=="State/ArchiveMode/items.json"||path=="State/ArchiveMode/sources.json"||path=="State/ArchiveMode/projections-dirty.json")return true;
    if(QRegularExpression("^State/ArchiveMode/Imports/Pending/[A-Za-z0-9_-]{1,160}/receipt\\.json$").match(path).hasMatch())return true;
    const auto parts=path.split('/');return parts.size()==3&&parts[0]=="Playlists"&&sourceKeySafe(parts[1])&&QStringList{"items.json","playlist.json","history.jsonl"}.contains(parts[2]);
}
// Idempotent roll-forward. A complete intent is durable before media or state moves.
inline bool recoverTransaction(const QString& root,QString* error){
    const auto journal=journalPath(root);if(!QFileInfo::exists(journal)&&noLinks(journal))return true;
    QByteArray bytes;if(!readBytes(journal,&bytes,error))return false;
    QJsonParseError pe;const auto doc=QJsonDocument::fromJson(bytes,&pe);const auto o=doc.object();
    if(pe.error!=QJsonParseError::NoError||o.value("schema_version")!=1||!o.value("operations").isArray()||o.value("operations").toArray().isEmpty())return reject(error,"Invalid pending state transaction; preserve it for repair");
    const auto package=o.value("package_move").toObject();
    QString packageFrom,packageTo;bool packageAlreadyMoved=false;
    if(!package.isEmpty()){
        const auto id=package.value("package_id").toString();
        if(!sourceKeySafe(id))return reject(error,"Invalid transaction package identity");
        packageFrom="State/ArchiveMode/Imports/Pending/"+id;
        packageTo="State/ArchiveMode/Imports/Accepted/"+id;
        const auto from=QDir(root).filePath(packageFrom),to=QDir(root).filePath(packageTo);
        if(!noLinks(from)||!noLinks(to))return reject(error,"Linked package transaction target");
        if(QFileInfo::exists(from)&&QFileInfo::exists(to))return reject(error,"Accepted package collision; no evidence was removed");
        packageAlreadyMoved=!QFileInfo::exists(from)&&QFileInfo(to).isDir();
        const auto actual=packageAlreadyMoved?to:from;
        if(fileDigest(QDir(actual).filePath("manifest.json"),error)!=package.value("manifest_sha256").toString())return reject(error,"Package changed after transaction preparation");
        const auto hashes=package.value("file_sha256").toObject();
        for(auto it=hashes.begin();it!=hashes.end();++it)
            if(!relativeSafe(it.key())||fileDigest(QDir(actual).filePath(it.key()),error)!=it.value().toString())return reject(error,"Package evidence changed during interrupted promotion");
    }
    QMap<QString,QByteArray> writes;QSet<QString> seen;
    for(const auto& v:o.value("operations").toArray()){
        const auto e=v.toObject();auto rel=e.value("path").toString();
        if(!relativeSafe(rel)||!journalTarget(rel)||seen.contains(rel.toCaseFolded())||!e.value("before_exists").isBool())return reject(error,"Unsafe or duplicate transaction target");
        seen.insert(rel.toCaseFolded());
        if(packageAlreadyMoved&&rel.startsWith(packageFrom+"/"))rel=packageTo+rel.mid(packageFrom.size());
        const auto path=QDir(root).filePath(rel);const auto encoded=e.value("after_base64").toString().toLatin1();const auto after=QByteArray::fromBase64(encoded);
        if(after.toBase64()!=encoded||digest(after)!=e.value("after_sha256").toString()||!noLinks(path))return reject(error,"Transaction payload integrity failure");
        if(!transactionPayload(e.value("path").toString(),after,error))return reject(error,"Unsafe transaction payload: "+rel);
        QByteArray current;const bool exists=QFileInfo::exists(path);if(exists&&!readBytes(path,&current,error))return false;
        if(exists&&digest(current)==digest(after))continue;
        if(exists!=e.value("before_exists").toBool()||(exists&&digest(current)!=e.value("before_sha256").toString()))return reject(error,"Transaction preimage conflict: "+rel);
        writes[path]=after;
    }
    const auto moves=o.value("media_moves").toArray();QSet<QString> moveTargets;
    for(const auto& v:moves){
        const auto m=v.toObject();const auto from=m.value("from").toString(),to=m.value("to").toString(),hash=m.value("sha256").toString();
        if(!relativeSafe(from)||!relativeSafe(to)||!from.startsWith("Temp/import-")||!(to.startsWith("Video/")||to.startsWith("Audio/"))||moveTargets.contains(to.toCaseFolded())||!QRegularExpression("^[0-9a-f]{64}$").match(hash).hasMatch())return reject(error,"Unsafe media transaction target");
        moveTargets.insert(to.toCaseFolded());const auto source=QDir(root).filePath(from),dest=QDir(root).filePath(to);
        if(!noLinks(source)||!noLinks(dest))return reject(error,"Linked media transaction target");
        const auto present=QFileInfo::exists(dest)?dest:source;
        if(fileDigest(present,error)!=hash)return reject(error,"Media transaction conflict or incomplete staging: "+to);
    }
    // Destinations were absent at preparation. Never remove or overwrite an existing file.
    for(const auto& v:moves){const auto m=v.toObject();const auto from=QDir(root).filePath(m.value("from").toString()),to=QDir(root).filePath(m.value("to").toString());
        if(!QFileInfo::exists(to)&&!QFile::rename(from,to))return reject(error,"Media promotion interrupted; retry initialization: "+to);
    }
    for(auto i=writes.begin();i!=writes.end();++i)if(!writeBytes(i.key(),i.value(),error))return false;
    if(!package.isEmpty()&&!packageAlreadyMoved && !QDir().rename(QDir(root).filePath(packageFrom),QDir(root).filePath(packageTo)))return reject(error,"Canonical state committed; package move pending. Retry initialization");
    if(!QFile::remove(journal))return reject(error,"State committed but transaction receipt could not be retired; retry initialization");
    return true;
}
inline bool commitTransaction(const QString& root,QMap<QString,QByteArray> files,QString* error,const QJsonArray& mediaMoves={},const QJsonObject& packageMove={}){
    if(QFileInfo::exists(journalPath(root)))return reject(error,"An incomplete transaction must be recovered before new work");
    files["State/ArchiveMode/projections-dirty.json"]="{}\n";
    QJsonArray operations;
    for(auto i=files.begin();i!=files.end();++i){
        if(!relativeSafe(i.key())||!journalTarget(i.key())||!transactionPayload(i.key(),i.value(),error))return reject(error,"Invalid transaction target or payload");
        const auto path=QDir(root).filePath(i.key());QByteArray before;const bool exists=QFileInfo::exists(path);
        if(!noLinks(path)||(exists&&!readBytes(path,&before,error)))return reject(error,"Cannot stage transaction target: "+i.key());
        if(exists&&before==i.value())continue;
        operations.append(QJsonObject{{"path",i.key()},{"before_exists",exists},{"before_sha256",digest(before)},{"after_base64",QString::fromLatin1(i.value().toBase64())},{"after_sha256",digest(i.value())}});
    }
    if(operations.isEmpty()&&mediaMoves.isEmpty()&&packageMove.isEmpty())return true;
    if(!writeBytes(journalPath(root),QJsonDocument(QJsonObject{{"schema_version",1},{"operations",operations},{"media_moves",mediaMoves},{"package_move",packageMove}}).toJson(),error))return false;
    return recoverTransaction(root,error);
}
} }
#endif
