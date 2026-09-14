#include "archivetab.h"

#include "../settings.h"

#include <QApplication>
#include <QBoxLayout>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>
#include <QGroupBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QTableWidget>
#include <QTabWidget>
#include <QTextEdit>
#include <QToolButton>
#include <QtConcurrent/QtConcurrent>
#include <QUrl>

#include <algorithm>

namespace
{
QString appSettingsFile()
{
    const auto d=QDir(QCoreApplication::applicationDirPath()).filePath("local/settings");
    QDir().mkpath(d);
    return QDir(d).filePath("archive-mode.ini");
}
QString readText(const QString& path)
{
    QFile f(path); if(!f.open(QIODevice::ReadOnly|QIODevice::Text)) return {}; return QString::fromUtf8(f.readAll());
}
QString pretty(const QString& s){return s.isEmpty()?QStringLiteral("—"):s;}
}

ArchiveTab::ArchiveTab(const Context& ctx):QObject(&ctx.mainWidget()),m_ctx(ctx)
{
    buildUi(); wireUi();
}

void ArchiveTab::buildUi()
{
    m_page=new QWidget(m_ctx.Ui().tabWidget);
    m_page->setObjectName("archiveModeTab");
    auto* rootLayout=new QVBoxLayout(m_page);
    rootLayout->setContentsMargins(8,8,8,8); rootLayout->setSpacing(7);

    auto* rootRow=new QHBoxLayout;
    auto* rootTitle=new QLabel(tr("Archive root:"),m_page); m_rootLabel=new QLabel(m_page); m_rootLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    auto* browse=new QPushButton(tr("Browse…"),m_page); browse->setObjectName("archiveBrowseRoot");
    rootRow->addWidget(rootTitle); rootRow->addWidget(m_rootLabel,1); rootRow->addWidget(browse); rootLayout->addLayout(rootRow);
    QObject::connect(browse,&QPushButton::clicked,this,&ArchiveTab::browseRoot);

    auto* actions=new QHBoxLayout;
    m_add=new QPushButton(tr("Add Playlist"),m_page); m_remove=new QPushButton(tr("Remove"),m_page); m_scan=new QPushButton(tr("Scan"),m_page);
    m_syncSelected=new QPushButton(tr("Sync Selected"),m_page); m_syncAll=new QPushButton(tr("Sync All"),m_page);
    m_stop=new QPushButton(tr("Stop After Current"),m_page); m_retry=new QPushButton(tr("Retry Failed"),m_page);
    m_more=new QToolButton(m_page); m_more->setText(tr("More")); m_more->setPopupMode(QToolButton::InstantPopup);
    for(auto* b:{m_add,m_remove,m_scan,m_syncSelected,m_syncAll,m_stop,m_retry}) actions->addWidget(b);
    actions->addStretch(); actions->addWidget(m_more); rootLayout->addLayout(actions);

    auto* splitter=new QSplitter(Qt::Horizontal,m_page);
    auto* left=new QWidget(splitter); auto* leftLayout=new QVBoxLayout(left); leftLayout->setContentsMargins(0,0,0,0);
    auto* playlistLabel=new QLabel(tr("PLAYLISTS"),left); m_sources=new QListWidget(left); m_sources->setMinimumWidth(210);
    leftLayout->addWidget(playlistLabel); leftLayout->addWidget(m_sources,1);

    auto* right=new QWidget(splitter); auto* rightLayout=new QVBoxLayout(right); rightLayout->setContentsMargins(0,0,0,0);
    m_healthLabel=new QLabel(tr("No playlist selected"),right); m_healthLabel->setWordWrap(true); rightLayout->addWidget(m_healthLabel);
    auto* filterRow=new QHBoxLayout; m_search=new QLineEdit(right); m_search->setPlaceholderText(tr("Search archive items…"));
    m_filter=new QComboBox(right); m_filter->addItems({tr("All"),tr("Protected"),tr("Needs Sync"),tr("Missing"),tr("Unavailable"),tr("Removed"),tr("Failed"),tr("Interrupted")});
    filterRow->addWidget(m_search,1); filterRow->addWidget(m_filter); rightLayout->addLayout(filterRow);
    m_table=new QTableWidget(right); m_table->setColumnCount(6); m_table->setHorizontalHeaderLabels({tr("#"),tr("Title"),tr("Availability"),tr("Video"),tr("Audio"),tr("Status")});
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows); m_table->setSelectionMode(QAbstractItemView::SingleSelection); m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->verticalHeader()->setVisible(false); m_table->horizontalHeader()->setStretchLastSection(false); m_table->horizontalHeader()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(1,QHeaderView::Stretch); for(int c=2;c<6;++c)m_table->horizontalHeader()->setSectionResizeMode(c,QHeaderView::ResizeToContents);
    rightLayout->addWidget(m_table,3);

    m_detailsTabs=new QTabWidget(right); m_sourceDetails=new QTextEdit; m_archiveDetails=new QTextEdit; m_historyDetails=new QTextEdit; m_recoveryDetails=new QTextEdit;
    for(auto* e:{m_sourceDetails,m_archiveDetails,m_historyDetails,m_recoveryDetails}) e->setReadOnly(true);
    m_detailsTabs->addTab(m_sourceDetails,tr("Source")); m_detailsTabs->addTab(m_archiveDetails,tr("Archive")); m_detailsTabs->addTab(m_historyDetails,tr("History")); m_detailsTabs->addTab(m_recoveryDetails,tr("Recovery"));
    m_detailsTabs->setMaximumHeight(210); rightLayout->addWidget(m_detailsTabs,1);
    splitter->addWidget(left); splitter->addWidget(right); splitter->setStretchFactor(0,0); splitter->setStretchFactor(1,1); splitter->setSizes({230,850}); rootLayout->addWidget(splitter,1);

    auto* statusRow=new QHBoxLayout; m_statusLabel=new QLabel(tr("Idle"),m_page); m_activityToggle=new QPushButton(tr("▸ Activity"),m_page); m_activityToggle->setCheckable(true);
    statusRow->addWidget(m_statusLabel,1); statusRow->addWidget(m_activityToggle); rootLayout->addLayout(statusRow);
    m_activity=new QPlainTextEdit(m_page); m_activity->setReadOnly(true); m_activity->setMaximumBlockCount(1000); m_activity->setMaximumHeight(180); m_activity->hide(); rootLayout->addWidget(m_activity);

    auto* menu=new QMenu(m_more);
    auto* openRoot=menu->addAction(tr("Open Archive Folder")); auto* openPlaylist=menu->addAction(tr("Open Playlist Folder"));
    menu->addSeparator(); auto* openCatalog=menu->addAction(tr("Open Catalog")); auto* openMissing=menu->addAction(tr("Open Missing Report"));
    menu->addSeparator(); auto* imports=menu->addAction(tr("Process External Imports")); auto* openLogs=menu->addAction(tr("Open Logs"));
    m_more->setMenu(menu);
    QObject::connect(openRoot,&QAction::triggered,[this]{openPath(m_root);});
    QObject::connect(openPlaylist,&QAction::triggered,this,&ArchiveTab::openSelectedPlaylistFolder);
    QObject::connect(openCatalog,&QAction::triggered,[this]{openProjection("catalog.csv");});
    QObject::connect(openMissing,&QAction::triggered,[this]{openProjection("missing.csv");});
    QObject::connect(imports,&QAction::triggered,this,&ArchiveTab::processImports);
    QObject::connect(openLogs,&QAction::triggered,[this]{if(!m_root.isEmpty())openPath(archive::Paths(m_root).activityLogs());});

    m_ctx.Ui().tabWidget->addTab(m_page,tr("Archive"));
}

void ArchiveTab::wireUi()
{
    QObject::connect(m_add,&QPushButton::clicked,this,&ArchiveTab::addPlaylist);
    QObject::connect(m_remove,&QPushButton::clicked,this,&ArchiveTab::removePlaylist);
    QObject::connect(m_scan,&QPushButton::clicked,this,&ArchiveTab::scanSelected);
    QObject::connect(m_syncSelected,&QPushButton::clicked,this,&ArchiveTab::syncSelected);
    QObject::connect(m_syncAll,&QPushButton::clicked,this,&ArchiveTab::syncAll);
    QObject::connect(m_retry,&QPushButton::clicked,this,&ArchiveTab::retryFailed);
    QObject::connect(m_stop,&QPushButton::clicked,this,&ArchiveTab::stopAfterCurrent);
    QObject::connect(m_sources,&QListWidget::currentRowChanged,[this]{refreshTable();refreshDetails();});
    QObject::connect(m_table,&QTableWidget::itemSelectionChanged,this,&ArchiveTab::refreshDetails);
    QObject::connect(m_search,&QLineEdit::textChanged,this,&ArchiveTab::refreshTable);
    QObject::connect(m_filter,QOverload<int>::of(&QComboBox::currentIndexChanged),[this](int){refreshTable();});
    QObject::connect(m_activityToggle,&QPushButton::toggled,[this](bool checked){m_activity->setVisible(checked);m_activityToggle->setText(checked?tr("▾ Activity"):tr("▸ Activity"));if(checked)refreshActivity();});
}

QString ArchiveTab::configuredRoot() const
{
    QSettings s(appSettingsFile(),QSettings::IniFormat);
    const auto relative=s.value("ArchiveRootRelativeToApp").toString();
    if(!relative.isEmpty()){
        const auto candidate=QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath(relative));
        if(QFileInfo(candidate).isDir()) return candidate;
    }
    const auto parent=QFileInfo(QCoreApplication::applicationDirPath()).absoluteDir().absolutePath();
    if(QFileInfo(QDir(parent).filePath("State")).exists()||QFileInfo(QDir(parent).filePath("Video")).exists()||QFileInfo(QDir(parent).filePath("Audio")).exists()) return parent;
    const auto download=m_ctx.Settings().downloadFolder();
    if(!download.isEmpty()&&QFileInfo(download).isDir()) return download;
    return parent;
}

void ArchiveTab::persistRoot(const QString& root)
{
    const auto app=QCoreApplication::applicationDirPath(); const auto rel=QDir::fromNativeSeparators(QDir(app).relativeFilePath(root));
    QSettings s(appSettingsFile(),QSettings::IniFormat); s.setValue("ArchiveRootRelativeToApp",rel); s.sync();
}

archive::RuntimeConfig ArchiveTab::runtimeConfig() const{return {m_root,QCoreApplication::applicationDirPath()};}

bool ArchiveTab::ensureReady(QString* error)
{
    if(m_root.isEmpty()) m_root=configuredRoot();
    archive::Paths p(m_root);
    archive::Store store(p);
    if(!store.initialize(error)) return false;
    m_rootLabel->setText(QDir::toNativeSeparators(m_root));
    return true;
}

void ArchiveTab::init_done(){m_root=configuredRoot();QString e;if(!ensureReady(&e))m_statusLabel->setText(tr("Archive initialization failed: %1").arg(e));refreshAll();}
void ArchiveTab::enableAll(){if(!m_busy)setBusy(false);}
void ArchiveTab::disableAll(){const QList<QWidget*> controls={m_add,m_remove,m_scan,m_syncSelected,m_syncAll,m_retry,m_more};
    for(auto* w:controls) w->setEnabled(false);}
void ArchiveTab::resetMenu(){}
void ArchiveTab::exiting(){m_stopRequested=true;if(m_watcher&&m_watcher->isRunning())m_watcher->waitForFinished();}
void ArchiveTab::retranslateUi(){}
void ArchiveTab::tabEntered(){refreshAll();}
void ArchiveTab::tabExited(){}
void ArchiveTab::keyPressed(utility::mainWindowKeyCombo){}
void ArchiveTab::textAlignmentChanged(Qt::LayoutDirection d){m_page->setLayoutDirection(d);}

void ArchiveTab::refreshAll(){QString e;if(!ensureReady(&e)){m_statusLabel->setText(e);return;}refreshSources();refreshTable();refreshDetails();if(m_activityToggle->isChecked())refreshActivity();}

void ArchiveTab::refreshSources()
{
    const auto current=selectedSourceKey(); archive::Store store{archive::Paths(m_root)}; const auto sources=store.loadSources(); m_sources->blockSignals(true);m_sources->clear();int selected=-1;
    for(int i=0;i<sources.size();++i){auto* item=new QListWidgetItem(sources[i].title.isEmpty()?sources[i].key:sources[i].title,m_sources);item->setData(Qt::UserRole,sources[i].key);item->setToolTip(sources[i].url);if(sources[i].key==current)selected=i;}
    if(selected<0&&m_sources->count()>0)selected=0;if(selected>=0)m_sources->setCurrentRow(selected);m_sources->blockSignals(false);
}

archive::Source ArchiveTab::selectedSource() const
{
    const auto key=selectedSourceKey(); archive::Store store{archive::Paths(m_root)};for(const auto& s:store.loadSources())if(s.key==key)return s;return {};
}
QString ArchiveTab::selectedSourceKey() const{auto* i=m_sources?m_sources->currentItem():nullptr;return i?i->data(Qt::UserRole).toString():QString();}
QString ArchiveTab::selectedItemKey() const{const auto rows=m_table?m_table->selectionModel()->selectedRows():QModelIndexList{};if(rows.isEmpty())return {};auto* i=m_table->item(rows.first().row(),0);return i?i->data(Qt::UserRole).toString():QString();}

QVector<archive::CanonicalItem> ArchiveTab::itemsForSource(const archive::Source& source) const
{
    archive::Store store{archive::Paths(m_root)};const auto playlist=store.loadPlaylistItems(source.key);const auto all=store.loadCanonicalItems();QHash<QString,archive::CanonicalItem> map;for(const auto& c:all)map[c.key]=c;QVector<archive::CanonicalItem> out;for(const auto& p:playlist)if(p.membership=="active"&&map.contains(p.itemKey))out.append(map[p.itemKey]);return out;
}

void ArchiveTab::refreshTable()
{
    if(m_root.isEmpty()||!m_table)return;const auto source=selectedSource();archive::Store store{archive::Paths(m_root)};const auto playlist=store.loadPlaylistItems(source.key);const auto all=store.loadCanonicalItems();QHash<QString,archive::CanonicalItem> map;for(const auto& c:all)map[c.key]=c;
    const auto search=m_search->text().trimmed();const auto filter=m_filter->currentText();int protectedCount=0,needs=0,unavailable=0,removed=0;
    m_table->setRowCount(0);
    for(const auto& p:playlist){const bool has=map.contains(p.itemKey);const auto c=has?map[p.itemKey]:archive::CanonicalItem{};const auto status=archive::derivedStatus(p,has?&c:nullptr);if(status=="Protected")++protectedCount;if(status.contains("Needs")||status=="Missing"||status=="Failed"||status=="Interrupted")++needs;if(p.availability!="public")++unavailable;if(p.membership=="removed")++removed;
        if(!search.isEmpty()&&!p.title.contains(search,Qt::CaseInsensitive)&&!p.providerId.contains(search,Qt::CaseInsensitive))continue;
        if(filter!="All"){
            bool match=false;if(filter=="Protected")match=status=="Protected";else if(filter=="Needs Sync")match=status=="Needs Sync";else if(filter=="Missing")match=status=="Missing";else if(filter=="Unavailable")match=p.availability!="public";else if(filter=="Removed")match=p.membership=="removed";else match=status==filter;if(!match)continue;
        }
        const int r=m_table->rowCount();m_table->insertRow(r);auto* pos=new QTableWidgetItem(p.position<0?QStringLiteral("—"):QString::number(p.position));pos->setData(Qt::UserRole,p.itemKey);m_table->setItem(r,0,pos);m_table->setItem(r,1,new QTableWidgetItem(p.title));m_table->setItem(r,2,new QTableWidgetItem(p.availability));m_table->setItem(r,3,new QTableWidgetItem(has?c.video.state:"missing"));m_table->setItem(r,4,new QTableWidgetItem(has?c.audio.state:"missing"));m_table->setItem(r,5,new QTableWidgetItem(status));
    }
    m_healthLabel->setText(source.key.isEmpty()?tr("No playlist selected"):tr("%1  |  All %2  |  Protected %3  |  Needs Work %4  |  Unavailable %5  |  Removed %6  |  Last scan %7").arg(source.title.isEmpty()?source.key:source.title).arg(playlist.size()).arg(protectedCount).arg(needs).arg(unavailable).arg(removed).arg(pretty(source.lastScanAt)));
}

void ArchiveTab::refreshDetails()
{
    const auto source=selectedSource();const auto key=selectedItemKey();archive::Store store{archive::Paths(m_root)};archive::PlaylistItem p;archive::CanonicalItem c;bool hp=false,hc=false;for(const auto& x:store.loadPlaylistItems(source.key))if(x.itemKey==key){p=x;hp=true;break;}for(const auto& x:store.loadCanonicalItems())if(x.key==key){c=x;hc=true;break;}
    if(!hp){m_sourceDetails->clear();m_archiveDetails->clear();m_historyDetails->clear();m_recoveryDetails->clear();return;}
    m_sourceDetails->setPlainText(tr("Title: %1\nYouTube ID: %2\nOriginal URL: %3\nPlaylist position: %4\nMembership: %5\nAvailability: %6\nFirst seen: %7\nLast seen: %8").arg(p.title,pretty(p.providerId),pretty(p.url)).arg(p.position).arg(p.membership,p.availability,pretty(p.firstSeen),pretty(p.lastSeen)));
    if(hc)m_archiveDetails->setPlainText(tr("Canonical key: %1\nVideo: %2\nVideo path: %3\nVideo origin: %4\nAudio: %5\nAudio path: %6\nAudio origin: %7\nMetadata: %8").arg(c.key,c.video.state,pretty(c.video.path),pretty(c.video.origin),c.audio.state,pretty(c.audio.path),pretty(c.audio.origin),pretty(c.metadataPath)));
    const auto history=readText(archive::Paths(m_root).playlistHistoryFile(source.key));QStringList matching;for(const auto& line:history.split('\n'))if(line.contains(key))matching<<line;m_historyDetails->setPlainText(matching.join("\n"));
    if(hc)m_recoveryDetails->setPlainText(tr("Recovery status: %1\nCurrent availability: %2\nVideo present: %3\nAudio present: %4\nExternal recovery is submitted through State/ArchiveMode/Imports/Pending according to ARCHIVE_AGENT.md.").arg(c.recoveryStatus,c.availability,c.video.state=="complete"?tr("Yes"):tr("No"),c.audio.state=="complete"?tr("Yes"):tr("No")));
}

void ArchiveTab::refreshActivity()
{
    if(m_root.isEmpty())return;const auto base=archive::Paths(m_root).activityLogs();QDir d(base);const auto days=d.entryList(QDir::Dirs|QDir::NoDotAndDotDot,QDir::Name|QDir::Reversed);QStringList lines;for(const auto& day:days){QDir dd(d.filePath(day));const auto files=dd.entryList(QDir::Files,QDir::Time);for(const auto& f:files){for(const auto& line:readText(dd.filePath(f)).split('\n')){if(line.trimmed().isEmpty())continue;QJsonParseError pe;const auto doc=QJsonDocument::fromJson(line.toUtf8(),&pe);if(pe.error==QJsonParseError::NoError&&doc.isObject()){const auto o=doc.object();lines<<QString("%1  %2  %3").arg(o.value("timestamp").toString(),o.value("event").toString(),QString::fromUtf8(QJsonDocument(o.value("details").toObject()).toJson(QJsonDocument::Compact)));}else lines<<line;if(lines.size()>=200)break;}if(lines.size()>=200)break;}if(lines.size()>=200)break;}std::reverse(lines.begin(),lines.end());m_activity->setPlainText(lines.join("\n"));
}

void ArchiveTab::setBusy(bool busy,const QString& text)
{
    m_busy=busy;const QList<QWidget*> controls={m_add,m_remove,m_scan,m_syncSelected,m_syncAll,m_retry,m_more};
    for(auto* w:controls) w->setEnabled(!busy);m_stop->setEnabled(busy);if(!text.isEmpty())m_statusLabel->setText(text);else if(!busy)m_statusLabel->setText(tr("Idle"));
}

void ArchiveTab::browseRoot()
{
    if(m_busy)return;const auto p=QFileDialog::getExistingDirectory(m_page,tr("Select Archive Root"),m_root,QFileDialog::ShowDirsOnly);if(p.isEmpty())return;m_root=QDir::cleanPath(p);persistRoot(m_root);QString e;if(!ensureReady(&e)){QMessageBox::critical(m_page,tr("Archive Root"),e);return;}refreshAll();
}

void ArchiveTab::addPlaylist()
{
    QString e;if(!ensureReady(&e)){QMessageBox::critical(m_page,tr("Archive"),e);return;}bool ok=false;const auto url=QInputDialog::getText(m_page,tr("Add Playlist"),tr("Playlist URL:"),QLineEdit::Normal,{},&ok).trimmed();if(!ok||url.isEmpty())return;const auto key=archive::sourceKeyFromUrl(url);auto sources=archive::Store(archive::Paths(m_root)).loadSources();for(const auto& s:sources)if(s.key==key){QMessageBox::information(m_page,tr("Add Playlist"),tr("This playlist is already registered."));return;}auto title=QInputDialog::getText(m_page,tr("Add Playlist"),tr("Display name:"),QLineEdit::Normal,key,&ok).trimmed();if(!ok)return;archive::Source s;s.key=key;s.url=url;s.title=title.isEmpty()?key:title;s.addedAt=QDateTime::currentDateTime().toString(Qt::ISODateWithMs);sources.append(s);archive::Store store{archive::Paths(m_root)};if(!store.saveSources(sources,&e)){QMessageBox::critical(m_page,tr("Add Playlist"),e);return;}archive::ActivityLogger logger{archive::Paths(m_root)};logger.event("INFO","source","playlist_added",{{"source_key",key},{"url",url},{"title",s.title}});refreshAll();for(int i=0;i<m_sources->count();++i)if(m_sources->item(i)->data(Qt::UserRole).toString()==key){m_sources->setCurrentRow(i);break;}
}

void ArchiveTab::removePlaylist()
{
    const auto s=selectedSource();if(s.key.isEmpty()||m_busy)return;if(QMessageBox::question(m_page,tr("Remove Playlist"),tr("Stop managing '%1'? Archived media and historical playlist files will not be deleted.").arg(s.title))!=QMessageBox::Yes)return;archive::Store store{archive::Paths(m_root)};auto sources=store.loadSources();for(int i=sources.size()-1;i>=0;--i)if(sources[i].key==s.key)sources.remove(i);QString e;if(!store.saveSources(sources,&e)){QMessageBox::critical(m_page,tr("Remove Playlist"),e);return;}archive::ActivityLogger logger{archive::Paths(m_root)};logger.event("INFO","source","playlist_unregistered",{{"source_key",s.key}});refreshAll();
}

void ArchiveTab::scanSelected(){const auto s=selectedSource();if(!s.key.isEmpty())runSources({s},false,tr("Scanning %1").arg(s.title));}
void ArchiveTab::syncSelected(){const auto s=selectedSource();if(!s.key.isEmpty())runSources({s},true,tr("Syncing %1").arg(s.title));}
void ArchiveTab::retryFailed(){syncSelected();}
void ArchiveTab::syncAll(){archive::Store store{archive::Paths(m_root)};const auto sources=store.loadSources();if(!sources.isEmpty())runSources(sources,true,tr("Syncing all playlists"));}
void ArchiveTab::stopAfterCurrent(){m_stopRequested=true;m_statusLabel->setText(tr("Stop requested. The current item will finish safely."));}

void ArchiveTab::processImports()
{
    if(m_busy)return;runAsync(tr("Processing external imports"),[this]{archive::Paths p(m_root);archive::Store store(p);archive::ActivityLogger logger(p);archive::RecoveryImporter importer(runtimeConfig(),store,logger);QStringList failures;const auto accepted=importer.ingestPending(&failures);QJsonObject o{{"accepted",accepted},{"failures",QJsonArray::fromStringList(failures)}};return QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact));});
}

void ArchiveTab::runSources(const QVector<archive::Source>& sources,bool downloads,const QString& name)
{
    if(m_busy||sources.isEmpty())return;m_stopRequested=false;runAsync(name,[this,sources,downloads]{return operationScanOrSync(sources,downloads);});
}

QString ArchiveTab::operationScanOrSync(QVector<archive::Source> sources,bool doDownloads)
{
    archive::Paths paths(m_root);archive::Store store(paths);QString error;if(!store.initialize(&error))return QString("ERROR:")+error;archive::ActivityLogger logger(paths);archive::SyncLock lock(paths);if(!lock.tryLock())return "ERROR:"+lock.errorString();QJsonObject result;int totalObserved=0,totalFailures=0,totalDownloaded=0;QStringList failures;
    logger.event("INFO","application",doDownloads?"sync_session_started":"scan_session_started",{{"source_count",sources.size()}});
    for(auto& source:sources){if(m_stopRequested.load())break;archive::PlaylistDiscovery discovery(runtimeConfig(),logger);auto snapshot=discovery.discover(source);auto sum=store.reconcile(source,snapshot,&logger);if(!sum.committed){failures<<source.key+": "+sum.error;++totalFailures;continue;}totalObserved+=sum.observed;if(!doDownloads)continue;
        auto playlist=store.loadPlaylistItems(source.key);auto canonical=store.loadCanonicalItems();QHash<QString,archive::CanonicalItem> map;for(const auto& c:canonical)map[c.key]=c;
        for(const auto& p:playlist){if(m_stopRequested.load())break;if(p.membership!="active"||p.availability!="public"||!map.contains(p.itemKey))continue;auto c=map[p.itemKey];if(c.video.state=="complete"&&c.audio.state=="complete")continue;archive::MediaExecutor executor(runtimeConfig(),store,logger);QString e;if(executor.syncItem(c,true,true,&e))++totalDownloaded;else{++totalFailures;failures<<p.itemKey+": "+e;}}
        store.writeProjections(source.key,nullptr);
    }
    lock.unlock();result["observed"]=totalObserved;result["completed_items"]=totalDownloaded;result["failure_count"]=totalFailures;result["stopped"]=m_stopRequested.load();result["failures"]=QJsonArray::fromStringList(failures);logger.event(totalFailures?"WARNING":"INFO","application",doDownloads?"sync_session_completed":"scan_session_completed",result);return QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact));
}

void ArchiveTab::runAsync(const QString& operationName,const std::function<QString()>& fn)
{
    if(m_busy)return;setBusy(true,operationName);m_watcher=new QFutureWatcher<QString>(this);QObject::connect(m_watcher,&QFutureWatcher<QString>::finished,[this,operationName]{const auto result=m_watcher->result();m_watcher->deleteLater();m_watcher=nullptr;setBusy(false);if(result.startsWith("ERROR:")){m_statusLabel->setText(result);QMessageBox::warning(m_page,tr("Archive Operation"),result.mid(6));}else{m_statusLabel->setText(operationName+tr(" completed"));}refreshAll();});m_watcher->setFuture(QtConcurrent::run(fn));
}

void ArchiveTab::openPath(const QString& path){if(!path.isEmpty())QDesktopServices::openUrl(QUrl::fromLocalFile(path));}
void ArchiveTab::openSelectedPlaylistFolder(){const auto key=selectedSourceKey();if(!key.isEmpty())openPath(archive::Paths(m_root).sourceDir(key));}
void ArchiveTab::openProjection(const QString& name){const auto key=selectedSourceKey();if(key.isEmpty())return;const auto p=QDir(archive::Paths(m_root).sourceDir(key)).filePath(name);if(QFileInfo::exists(p))openPath(p);else QMessageBox::information(m_page,tr("Archive"),tr("The projection has not been generated yet."));}
