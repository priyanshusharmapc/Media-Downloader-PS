#ifndef MDPS_ARCHIVECORE_H
#define MDPS_ARCHIVECORE_H

#include <QDateTime>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QVector>

#include <functional>
#include <memory>

namespace archive
{
struct RuntimeConfig
{
    QString archiveRoot;
    QString appDir;
};

struct Representation
{
    QString state = "missing";
    QString path;
    QString origin;
    QString verifiedAt;
    QString error;
};

struct Source
{
    QString key;
    QString url;
    QString title;
    QString addedAt;
    QString lastScanAt;
    QString lastScanStatus = "never";
    QString lastError;
};

struct CanonicalItem
{
    QString key;
    QString provider = "youtube";
    QString providerId;
    QString title;
    QString uploader;
    QString originalUrl;
    QString availability = "unknown";
    QString firstSeen;
    QString lastSeen;
    QString recoveryStatus = "not_required";
    QString metadataPath;
    QStringList userTags;
    Representation video;
    Representation audio;
};

struct PlaylistItem
{
    QString itemKey;
    QString providerId;
    int position = -1;
    QString title;
    QString url;
    QString availability = "unknown";
    QString membership = "active";
    QString firstSeen;
    QString lastSeen;
    int lastPosition = -1;
};

struct Snapshot
{
    QString sourceKey;
    bool complete = false;
    int httpStatus = 0;
    QString scannedAt;
    QString error;
    QVector<PlaylistItem> items;
};

struct ReconcileSummary
{
    bool committed = false;
    bool completeSnapshot = false;
    int observed = 0;
    int active = 0;
    int removed = 0;
    int reappeared = 0;
    int unavailable = 0;
    int needsVideo = 0;
    int needsAudio = 0;
    QString error;
};

struct ValidationResult
{
    bool ok = false;
    QString packageId;
    QString itemKey;
    QStringList errors;
};

struct ProcessResult
{
    bool ok = false;
    int exitCode = -1;
    QString standardOutput;
    QString standardError;
    QString error;
};

class Paths
{
public:
    explicit Paths(QString root = {});
    QString root() const;
    QString video() const;
    QString audio() const;
    QString metadata() const;
    QString temp() const;
    QString playlists() const;
    QString state() const;
    QString archiveState() const;
    QString sourcesFile() const;
    QString itemsFile() const;
    QString schemas() const;
    QString imports() const;
    QString importsPending() const;
    QString importsAccepted() const;
    QString importsRejected() const;
    QString activityLogs() const;
    QString diagnosticLogs() const;
    QString sourceDir(const QString& sourceKey) const;
    QString playlistFile(const QString& sourceKey) const;
    QString playlistItemsFile(const QString& sourceKey) const;
    QString playlistHistoryFile(const QString& sourceKey) const;
    QString relativeToRoot(const QString& absolute) const;
    QString absoluteFromRelative(const QString& relative) const;
    bool isSafeRelative(const QString& path) const;
    bool ensureLayout(QString* error = nullptr) const;
    bool materializeAgentResources(QString* error = nullptr) const;
private:
    QString m_root;
};

class ActivityLogger
{
public:
    explicit ActivityLogger(Paths paths);
    void event(const QString& severity,const QString& category,const QString& name,
               const QJsonObject& details = {},const QString& sessionId = {});
    void diagnostic(const QString& line);
    QString redact(QString text) const;
    void prune();
private:
    Paths m_paths;
    QString m_sessionId;
};

class Store
{
public:
    explicit Store(Paths paths);
    const Paths& paths() const;
    bool initialize(QString* error = nullptr);
    QVector<Source> loadSources(QString* error = nullptr) const;
    bool saveSources(const QVector<Source>& sources,QString* error = nullptr) const;
    QVector<CanonicalItem> loadCanonicalItems(QString* error = nullptr) const;
    bool saveCanonicalItems(const QVector<CanonicalItem>& items,QString* error = nullptr) const;
    QVector<PlaylistItem> loadPlaylistItems(const QString& sourceKey,QString* error = nullptr) const;
    bool savePlaylistItems(const QString& sourceKey,const QVector<PlaylistItem>& items,QString* error = nullptr) const;
    bool savePlaylistMeta(const Source& source,QString* error = nullptr) const;
    bool appendHistory(const QString& sourceKey,const QJsonObject& event,QString* error = nullptr) const;
    bool writeProjections(const QString& sourceKey,QString* error = nullptr) const;
    bool updateRepresentation(const QString& itemKey,const QString& kind,const Representation& representation,QString* error = nullptr);
    bool updateCanonicalMetadata(const QString& itemKey,const QString& title,const QString& uploader,
                                 const QString& availability,const QString& originalUrl,QString* error = nullptr);
    ReconcileSummary reconcile(Source& source,const Snapshot& snapshot,ActivityLogger* logger = nullptr);
    bool writeReceipt(const QString& packageDir,const QJsonObject& receipt,QString* error = nullptr) const;
private:
    Paths m_paths;
};

class ToolResolver
{
public:
    explicit ToolResolver(RuntimeConfig config);
    QString ytDlp() const;
    QString ffmpeg() const;
    QString ffprobe() const;
    QString deno() const;
private:
    QString find(const QStringList& names,const QStringList& relativeCandidates) const;
    RuntimeConfig m_config;
};

class PlaylistDiscovery
{
public:
    PlaylistDiscovery(RuntimeConfig config,ActivityLogger& logger);
    Snapshot discover(const Source& source);
    static Snapshot parse(const Source& source,const QByteArray& json,const QString& stderrText,int exitCode);
private:
    RuntimeConfig m_config;
    ActivityLogger& m_logger;
};

class MediaVerifier
{
public:
    MediaVerifier(RuntimeConfig config,ActivityLogger& logger);
    ValidationResult verifyVideo(const QString& relativePath) const;
    ValidationResult verifyAudio(const QString& relativePath) const;
private:
    ValidationResult probe(const QString& relativePath,bool video) const;
    RuntimeConfig m_config;
    ActivityLogger& m_logger;
};

class MediaExecutor
{
public:
    MediaExecutor(RuntimeConfig config,Store& store,ActivityLogger& logger);
    bool syncItem(const CanonicalItem& item,bool wantVideo,bool wantAudio,QString* error = nullptr);
    bool syncItems(const QVector<CanonicalItem>& items,const std::function<bool()>& shouldStop,
                   QStringList* failures = nullptr);
private:
    ProcessResult run(const QString& program,const QStringList& args,const QString& purpose) const;
    QString findExistingById(const QString& relativeDir,const QString& id,const QStringList& extensions) const;
    bool downloadVideo(const CanonicalItem& item,QString* error);
    bool downloadAudio(const CanonicalItem& item,QString* error);
    RuntimeConfig m_config;
    Store& m_store;
    ActivityLogger& m_logger;
    ToolResolver m_tools;
    MediaVerifier m_verifier;
};

class RecoveryImporter
{
public:
    RecoveryImporter(RuntimeConfig config,Store& store,ActivityLogger& logger);
    ValidationResult validate(const QString& packageDir) const;
    bool ingest(const QString& packageDir,QString* error = nullptr);
    int ingestPending(QStringList* failures = nullptr);
private:
    bool normalizeVideo(const QString& input,const QString& output,QString* error) const;
    bool normalizeAudio(const QString& input,const QString& output,QString* error) const;
    RuntimeConfig m_config;
    Store& m_store;
    ActivityLogger& m_logger;
    ToolResolver m_tools;
    MediaVerifier m_verifier;
};

class SyncLock
{
public:
    explicit SyncLock(const Paths& paths);
    bool tryLock(int timeoutMs = 0);
    void unlock();
    QString errorString() const;
private:
    std::unique_ptr<QLockFile> m_lock;
    QString m_error;
};

QString sourceKeyFromUrl(const QString& url);
QString availabilityFromEntry(const QJsonObject& entry);
QString canonicalKey(const QString& providerId,const QString& sourceKey,int position,const QString& title);
QString derivedStatus(const PlaylistItem& playlistItem,const CanonicalItem* canonical);

} // namespace archive

#endif
