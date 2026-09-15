#include "../src/archive/archivecore.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTextStream>

#include <cmath>

using namespace archive;

namespace
{
void fail(const QString& message)
{
    QTextStream(stderr)<<"FAIL: "<<message<<"\n";
    std::exit(1);
}
void require(bool condition,const QString& message){if(!condition)fail(message);}

QString tool(const char* environmentName,const QString& fallback)
{
    const auto configured=qEnvironmentVariable(environmentName);
    return configured.isEmpty()?QStandardPaths::findExecutable(fallback):configured;
}

ProcessResult runTool(const QString& program,const QStringList& args,const QString& cwd)
{
    ProcessResult result;
    QProcess process;process.setWorkingDirectory(cwd);process.setProcessChannelMode(QProcess::SeparateChannels);process.start(program,args);
    require(process.waitForStarted(10000),"cannot start "+program+": "+process.errorString());
    process.closeWriteChannel();require(process.waitForFinished(180000),"tool timed out: "+program);
    result.exitCode=process.exitCode();result.standardOutput=QString::fromUtf8(process.readAllStandardOutput());result.standardError=QString::fromUtf8(process.readAllStandardError());
    result.ok=process.exitStatus()==QProcess::NormalExit&&result.exitCode==0;
    if(!result.ok)result.error=QString("exit %1").arg(result.exitCode);
    return result;
}

void copyTruncated(const QString& source,const QString& destination,double fraction)
{
    QFile input(source);require(input.open(QIODevice::ReadOnly),"open source fixture");
    const auto bytes=input.readAll();require(bytes.size()>4096,"fixture is unexpectedly small");
    const auto keep=qMax<qint64>(1024,static_cast<qint64>(std::floor(bytes.size()*fraction)));
    QFile output(destination);require(output.open(QIODevice::WriteOnly),"open truncated fixture");
    require(output.write(bytes.constData(),keep)==keep,"write truncated fixture");
}

void prependPath(const QString& directory)
{
    const auto current=qEnvironmentVariable("PATH");
    const auto separator=QDir::listSeparator();
    qputenv("PATH",(directory+separator+current).toUtf8());
}
}

int main(int argc,char** argv)
{
    QCoreApplication app(argc,argv);
    const auto ffmpeg=tool("ARCHIVE_TEST_FFMPEG","ffmpeg");
    const auto ffprobe=tool("ARCHIVE_TEST_FFPROBE","ffprobe");
    require(!ffmpeg.isEmpty()&&!ffprobe.isEmpty(),"FFmpeg and FFprobe are required for media integrity qualification");
    prependPath(QFileInfo(ffmpeg).absolutePath());

    const auto testTemp=QDir(qEnvironmentVariable("ARCHIVE_TEST_TMP",QDir::tempPath())).filePath("archive-media-XXXXXX");
    QTemporaryDir temp(testTemp);require(temp.isValid(),"temporary test root");
    const auto video=QDir(temp.path()).filePath("valid.mp4");
    const auto audio=QDir(temp.path()).filePath("valid.m4a");
    const auto wrong=QDir(temp.path()).filePath("wrong.mp4");
    require(runTool(ffmpeg,{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=320x180:rate=24","-f","lavfi","-i","sine=frequency=1000:sample_rate=48000","-t","8","-c:v","libx264","-pix_fmt","yuv420p","-c:a","aac","-b:a","128k","-movflags","+faststart",video},temp.path()).ok,"generate valid video fixture");
    require(runTool(ffmpeg,{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","sine=frequency=1000:sample_rate=48000","-t","8","-c:a","aac","-b:a","128k","-movflags","+faststart",audio},temp.path()).ok,"generate valid audio fixture");
    require(runTool(ffmpeg,{"-hide_banner","-loglevel","error","-y","-f","lavfi","-i","testsrc2=size=320x180:rate=24","-t","3","-c:v","mpeg4","-an","-movflags","+faststart",wrong},temp.path()).ok,"generate wrong-codec fixture");
    const auto truncatedVideo=QDir(temp.path()).filePath("truncated.mp4");
    const auto truncatedAudio=QDir(temp.path()).filePath("truncated.m4a");
    copyTruncated(video,truncatedVideo,0.40);copyTruncated(audio,truncatedAudio,0.40);
    const auto archiveVideo=QDir(temp.path()).filePath("Video/truncated.mp4");
    QDir().mkpath(QFileInfo(archiveVideo).absolutePath());
    require(QFile::copy(truncatedVideo,archiveVideo),"copy corrupt canonical fixture");

    require(runTool(ffprobe,{"-v","error","-show_streams","-show_format","-of","json",truncatedVideo},temp.path()).ok,"truncated video retains readable metadata fixture");
    require(runTool(ffprobe,{"-v","error","-show_streams","-show_format","-of","json",truncatedAudio},temp.path()).ok,"truncated audio retains readable metadata fixture");

    RuntimeConfig config{temp.path(),QFileInfo(ffmpeg).absolutePath()};
    ActivityLogger logger(Paths(temp.path()));MediaVerifier verifier(config,logger);
    require(verifier.verifyVideo(Paths(temp.path()).relativeToRoot(video)).ok,"valid video rejected");
    require(verifier.verifyAudio(Paths(temp.path()).relativeToRoot(audio)).ok,"valid audio rejected");
    require(!verifier.verifyVideo(Paths(temp.path()).relativeToRoot(truncatedVideo)).ok,"truncated video accepted as complete");
    require(!verifier.verifyAudio(Paths(temp.path()).relativeToRoot(truncatedAudio)).ok,"truncated audio accepted as complete");
    require(!verifier.verifyVideo(Paths(temp.path()).relativeToRoot(wrong)).ok,"wrong video codec accepted");

    Store store(Paths(temp.path()));QString error;require(store.initialize(&error),"initialize state: "+error);
    CanonicalItem item;item.key="youtube:abc123DEF45";item.providerId="abc123DEF45";item.title="integrity";item.video.state="complete";item.video.path="Video/truncated.mp4";
    require(store.saveCanonicalItems({item},&error),"save complete corrupt state: "+error);
    require(!verifier.verifyVideo(item.video.path).ok,"corrupt complete representation reverified as valid");
    require(QDir(temp.path()).entryList(QDir::Dirs|QDir::NoDotAndDotDot).filter("normalize-*").isEmpty(),"verification created normalization residue");

    QTextStream(stdout)<<"archive-media-integrity-tests: PASS\n";
    return 0;
}
