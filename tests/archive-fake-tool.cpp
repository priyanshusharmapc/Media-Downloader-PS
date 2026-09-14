// Deterministic subprocess fixture. This executable is built only with BUILD_TESTING.
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 if(args.contains("--version")){QTextStream(stdout)<<"archive-test-fixture-1\n";return 0;}
 QFile planFile(qEnvironmentVariable("ARCHIVE_TEST_PLAN"));if(!planFile.open(QIODevice::ReadOnly))return 90;
 const auto plan=QJsonDocument::fromJson(planFile.readAll()).object();
 QFile calls(plan.value("calls_path").toString());if(!calls.open(QIODevice::WriteOnly|QIODevice::Append))return 91;
 calls.write(QJsonDocument(QJsonArray::fromStringList(args.mid(1))).toJson(QJsonDocument::Compact)+"\n");calls.close();
 const auto delay=plan.value("delay_ms").toInt();if(delay>0)QThread::msleep(static_cast<unsigned long>(delay));
 if(args.contains("--flat-playlist")){QTextStream(stdout)<<QString::fromUtf8(QJsonDocument(plan.value("discovery").toObject()).toJson(QJsonDocument::Compact))<<"\n";QTextStream(stderr)<<plan.value("discovery_stderr").toString();return plan.value("discovery_exit").toInt();}
 if(plan.value("download_exit").toInt()!=0){QTextStream(stderr)<<"fixture download failed\n";return plan.value("download_exit").toInt();}
 if(plan.value("archive_skip").toBool()&&args.contains("--download-archive"))return 0;
 if(!args.contains("--no-overwrites")||!args.contains("--"))return 92;
 const bool audio=args.contains("-x");const auto source=plan.value(audio?"audio_file":"video_file").toString();
 const auto id=plan.value("video_id").toString("abc123DEF45");
 QString suffix;const int oi=args.indexOf("-o");if(oi>=0){const auto match=QRegularExpression("\\[repair-[^\\]]+\\]").match(args.value(oi+1));if(match.hasMatch())suffix=" "+match.captured();}
 const auto folder=audio?"Audio":"Video";QDir().mkpath(folder);
 const auto dest=QString(folder)+"/fixture ["+id+"]"+suffix+"."+QFileInfo(source).suffix();
 if(!QFileInfo::exists(dest)&&!QFile::copy(source,dest))return 93;
 return 0;
}
