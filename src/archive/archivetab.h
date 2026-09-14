#ifndef MDPS_ARCHIVETAB_H
#define MDPS_ARCHIVETAB_H

#include "archivecore.h"
#include "../context.hpp"
#include "../utility.h"

#include <QObject>
#include <QFutureWatcher>
#include <QJsonObject>

#include <atomic>

class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QTableWidget;
class QTabWidget;
class QTextEdit;
class QPlainTextEdit;
class QComboBox;
class QToolButton;
class QWidget;

class ArchiveTab : public QObject
{
    Q_OBJECT
public:
    explicit ArchiveTab(const Context& ctx);
    void keyPressed(utility::mainWindowKeyCombo);
    void init_done();
    void enableAll();
    void disableAll();
    void resetMenu();
    void exiting();
    void retranslateUi();
    void tabEntered();
    void tabExited();
    void textAlignmentChanged(Qt::LayoutDirection);
private:
    QString configuredRoot() const;
    void persistRoot(const QString& root);
    archive::RuntimeConfig runtimeConfig() const;
    bool ensureReady(QString* error=nullptr);
    void buildUi();
    void wireUi();
    void refreshAll();
    void refreshSources();
    void refreshTable();
    void refreshDetails();
    void refreshActivity();
    void setBusy(bool busy,const QString& text={});
    archive::Source selectedSource() const;
    QString selectedSourceKey() const;
    QString selectedItemKey() const;
    QVector<archive::CanonicalItem> itemsForSource(const archive::Source& source) const;
    void addPlaylist();
    void removePlaylist();
    void browseRoot();
    void scanSelected();
    void syncSelected();
    void syncAll();
    void retryFailed();
    void processImports();
    void stopAfterCurrent();
    void openPath(const QString& path);
    void openSelectedPlaylistFolder();
    void openProjection(const QString& name);
    void runSources(const QVector<archive::Source>& sources,bool doDownloads,const QString& operationName);
    void runAsync(const QString& operationName,const std::function<QString()>& fn);
    QString operationScanOrSync(QVector<archive::Source> sources,bool doDownloads);

    const Context& m_ctx;
    QWidget* m_page=nullptr;
    QLabel* m_rootLabel=nullptr;
    QLabel* m_healthLabel=nullptr;
    QLabel* m_statusLabel=nullptr;
    QListWidget* m_sources=nullptr;
    QLineEdit* m_search=nullptr;
    QComboBox* m_filter=nullptr;
    QTableWidget* m_table=nullptr;
    QTabWidget* m_detailsTabs=nullptr;
    QTextEdit* m_sourceDetails=nullptr;
    QTextEdit* m_archiveDetails=nullptr;
    QTextEdit* m_historyDetails=nullptr;
    QTextEdit* m_recoveryDetails=nullptr;
    QPlainTextEdit* m_activity=nullptr;
    QPushButton* m_add=nullptr;
    QPushButton* m_remove=nullptr;
    QPushButton* m_scan=nullptr;
    QPushButton* m_syncSelected=nullptr;
    QPushButton* m_syncAll=nullptr;
    QPushButton* m_stop=nullptr;
    QPushButton* m_retry=nullptr;
    QToolButton* m_more=nullptr;
    QPushButton* m_activityToggle=nullptr;
    QFutureWatcher<QString>* m_watcher=nullptr;
    QString m_root;
    bool m_busy=false;
    std::atomic_bool m_stopRequested{false};
};

#endif
