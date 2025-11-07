#include "media_converter_worker.h"
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QRegularExpression>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#endif

#include <algorithm>

static QString quote(const QString& p){
    if (p.startsWith('"') && p.endsWith('"')) return p;
    QString s = p; s.replace('"', "\"\"");
    return '"' + s + '"';
}

MediaConverterWorker::MediaConverterWorker(QObject* parent) : QObject(parent) {}

void MediaConverterWorker::start(const QVector<Task>& tasks)
{
    if (tasks.isEmpty()) { emit queueFinished(true); return; }
    m_tasks = tasks;
    m_index = -1;
    m_cancelling = false;
    m_paused = false;
    emit queueStarted(m_tasks.size());
    startNext();
}

void MediaConverterWorker::pause()
{
#ifdef Q_OS_WIN
    if (suspendProcess()) {
        m_paused = true;
        emit logLine("[Paused]");
    }
#else
    m_paused = true;
#endif
}

void MediaConverterWorker::resume()
{
#ifdef Q_OS_WIN
    if (resumeProcess()) {
        m_paused = false;
        emit logLine("[Resumed]");
    }
#else
    m_paused = false;
#endif
}

void MediaConverterWorker::cancelAll()
{
    m_cancelling = true;
    if (m_proc) {
        m_proc->kill();
    } else if (m_waitingOnError) {
        m_waitingOnError = false;
        emit queueFinished(false);
    }
}

void MediaConverterWorker::retryCurrent()
{
    if (m_index >= 0) {
        // Restart current by decrementing index to re-run in startNext
        m_waitingOnError = false;
        m_index -= 1;
        startNext();
    }
}

void MediaConverterWorker::continueAfterFailure()
{
    // Skip the failed item and continue with next
    if (m_waitingOnError) {
        m_waitingOnError = false;
        startNext();
    }
}

void MediaConverterWorker::startNext()
{
    if (m_cancelling) { emit queueFinished(false); return; }
    if (m_proc) { m_proc->deleteLater(); m_proc = nullptr; }

    m_index += 1;
    if (m_index >= m_tasks.size()) { emit queueFinished(true); return; }

    const Task& t = m_tasks[m_index];

    QString err, outPath; QStringList args; qint64 durMs = 0;
    if (!buildCommand(t, outPath, args, durMs, err)) {
        emit logLine(QString("[ERROR] %1").arg(err));
        emit fileFinished(m_index, false, err);
        startNext();
        return;
    }

    if (t.conflict == ConflictAction::Skip && QFileInfo::exists(outPath)) {
        emit logLine(QString("[Skip] %1 exists").arg(outPath));
        emit fileFinished(m_index, true, QString());
        overallProgress(int(double(m_index+1) / double(m_tasks.size()) * 100.0));
        startNext();
        return;
    }

    m_curDurationMs = durMs;

    emit fileStarted(m_index, t.sourcePath, outPath, durMs);

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &MediaConverterWorker::onReadyStdOut);
    connect(m_proc, &QProcess::readyReadStandardError, this, &MediaConverterWorker::onReadyStdErr);
    connect(m_proc, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, &MediaConverterWorker::onFinished);

#if defined(Q_OS_WIN)
    // Ensure paths with spaces work
    m_proc->setProgram(m_ffmpegPath);
    m_proc->setArguments(args);
#else
    m_proc->setProgram(m_ffmpegPath);
    m_proc->setArguments(args);
#endif
    emit logLine(QString("ffmpeg %1").arg(args.join(' ')));
    m_timer.restart();
    m_proc->start();
}

void MediaConverterWorker::onReadyStdOut()
{
    const QByteArray data = m_proc->readAllStandardOutput();
    const QString s = QString::fromUtf8(data);
    emit logLine(s);

    // Parse -progress output
    // Expect lines like: out_time_ms=123456, progress=continue
    static QRegularExpression rxTime("out_time_ms=([0-9]+)");
    QRegularExpressionMatch m = rxTime.match(s);
    if (m.hasMatch() && m_curDurationMs > 0) {
        qint64 outMs = m.captured(1).toLongLong();
        int percent = int(std::min<qint64>(outMs * 100 / m_curDurationMs, 100));
        emit currentFileProgress(m_index, percent, outMs, m_curDurationMs);
        int overall = int(((double(m_index) + (percent/100.0)) / double(m_tasks.size())) * 100.0);
        emit overallProgress(std::clamp(overall, 0, 100));
    }
}

void MediaConverterWorker::onReadyStdErr()
{
    const QByteArray data = m_proc->readAllStandardError();
    const QString s = QString::fromUtf8(data);
    emit logLine(s);
}

void MediaConverterWorker::onFinished(int exitCode, QProcess::ExitStatus status)
{
    const bool ok = (status == QProcess::NormalExit && exitCode == 0);
    if (!ok && !m_cancelling) {
        emit fileFinished(m_index, false, QString::fromUtf8(m_proc->readAllStandardError()));
        // Pause queue and wait for UI decision (retry/skip/cancel)
        m_waitingOnError = true;
        return;
    }
    emit fileFinished(m_index, ok, QString());
    overallProgress(int(double(m_index+1) / double(m_tasks.size()) * 100.0));
    startNext();
}

QString MediaConverterWorker::uniqueOutPath(const QString& basePath)
{
    if (!QFileInfo::exists(basePath)) return basePath;
    QFileInfo fi(basePath);
    QString base = fi.completeBaseName();
    QString ext = fi.suffix();
    QString dir = fi.dir().absolutePath();
    for (int i=1;i<10000;++i) {
        QString cand = QDir(dir).filePath(QString("%1_%2.%3").arg(base).arg(i,3,10,QChar('0')).arg(ext));
        if (!QFileInfo::exists(cand)) return cand;
    }
    return basePath;
}

bool MediaConverterWorker::probeDurationMs(const QString& ffmpeg, const QString& input, qint64& durMs, int& w, int& h, QString& err)
{
    durMs = 0; w = 0; h = 0;
    // Use ffprobe next to ffmpeg if possible
    QString ffprobe = QFileInfo(ffmpeg).dir().filePath("ffprobe.exe");
    if (!QFileInfo::exists(ffprobe)) ffprobe = "ffprobe"; // try PATH

    QProcess p; QStringList a;
    a << "-v" << "error" << "-select_streams" << "v:0" << "-show_entries" << "stream=width,height" << "-of" << "default=nw=1:nk=1" << input;
    p.start(ffprobe, a); p.waitForFinished(5000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        QList<QByteArray> lines = p.readAllStandardOutput().split('\n');
        if (lines.size() >= 2) {
            bool ok1=false, ok2=false; w = QString::fromUtf8(lines[0]).trimmed().toInt(&ok1); h = QString::fromUtf8(lines[1]).trimmed().toInt(&ok2);
            Q_UNUSED(ok1); Q_UNUSED(ok2);
        }
    }

    QProcess p2; QStringList a2; a2 << "-v" << "error" << "-show_entries" << "format=duration" << "-of" << "default=nw=1:nk=1" << input;
    p2.start(ffprobe, a2); p2.waitForFinished(5000);
    if (p2.exitStatus() == QProcess::NormalExit && p2.exitCode() == 0) {
        bool ok=false; double sec = QString::fromUtf8(p2.readAllStandardOutput()).trimmed().toDouble(&ok);
        if (ok && sec > 0.0) durMs = qint64(sec * 1000.0);
    }
    return true;
}

QString MediaConverterWorker::scaleFilterFor(const Task& t, bool isVideo)
{
    if (t.scaleWidth <= 0 && t.scaleHeight <= 0) return QString();
    int w = t.scaleWidth > 0 ? t.scaleWidth : -2;
    int h = t.scaleHeight > 0 ? t.scaleHeight : -2;
    if (!isVideo) {
        // Images don't require even constraints typically
        if (t.scaleWidth <= 0) w = -1; if (t.scaleHeight <= 0) h = -1;
    }
    QString f = QString("scale=%1:%2:flags=lanczos").arg(w).arg(h);
    if (isVideo && t.forceEven) f += ",pad=ceil(iw/2)*2:ceil(ih/2)*2"; // ensure even
    return f;
}

bool MediaConverterWorker::buildCommand(const Task& t, QString& outPath, QStringList& args, qint64& estDurationMs, QString& err) const
{
    args.clear(); err.clear(); estDurationMs = 0;
    if (m_ffmpegPath.isEmpty()) { err = "FFmpeg path not set"; return false; }

    const QFileInfo inFi(t.sourcePath);
    if (!inFi.exists()) { err = QString("Source not found: %1").arg(t.sourcePath); return false; }

    // Derive base output name
    const QString baseName = inFi.completeBaseName();
    const QString outDir = t.outputDir.isEmpty() ? inFi.dir().absolutePath() : t.outputDir;
    QDir().mkpath(outDir);

    QString ext;
    bool isVideo = false;
    switch (t.target) {
        case TargetKind::VideoMP4: ext = "mp4"; isVideo = true; break;
        case TargetKind::VideoMOV: ext = "mov"; isVideo = true; break;
        case TargetKind::JpgSequence: ext = "jpg"; break;
        case TargetKind::PngSequence: ext = "png"; break;
        case TargetKind::TifSequence: ext = "tif"; break;
        case TargetKind::ImageJpg: ext = "jpg"; break;
        case TargetKind::ImagePng: ext = "png"; break;
        case TargetKind::ImageTif: ext = "tif"; break;
    }

    // Probe duration for progress (videos)
    int inW=0, inH=0; probeDurationMs(m_ffmpegPath, t.sourcePath, estDurationMs, inW, inH, err);

    // Input
    args << "-hide_banner" << "-nostdin" << "-y"; // allow overwrite handling below
    args << "-progress" << "pipe:1"; // progress to stdout
    args << "-i" << inFi.absoluteFilePath();

    // Scaling
    const QString scaleF = scaleFilterFor(t, isVideo);
    if (!scaleF.isEmpty()) args << "-vf" << scaleF;

    // Per target settings
    if (t.target == TargetKind::VideoMP4) {
        QString vcodec = t.mp4.codec.isEmpty() ? QStringLiteral("libx264") : t.mp4.codec;
        if (vcodec == "h264") vcodec = "libx264";
        if (vcodec == "hevc" || vcodec == "h265") vcodec = "libx265";
        args << "-c:v" << vcodec;
        if (t.mp4.rateMode == RateMode::CBR) {
            args << "-b:v" << QString::number(t.mp4.bitrateMbps) + "M"
                 << "-minrate" << QString::number(t.mp4.bitrateMbps) + "M"
                 << "-maxrate" << QString::number(t.mp4.bitrateMbps) + "M"
                 << "-bufsize" << QString::number(t.mp4.bitrateMbps*2) + "M";
        } else {
            args << "-b:v" << QString::number(t.mp4.bitrateMbps) + "M";
        }
        args << "-movflags" << "+faststart";
        outPath = QDir(outDir).filePath(baseName + ".mp4");
    } else if (t.target == TargetKind::VideoMOV) {
        QString vcodec = t.mov.codec;
        if (vcodec == "h264") vcodec = "libx264";
        if (vcodec == "Animation") vcodec = "qtrle"; // QuickTime RLE (Animation)
        if (vcodec.startsWith("prores")) vcodec = "prores_ks";
        args << "-c:v" << (vcodec.isEmpty() ? "prores_ks" : vcodec);
        if (vcodec == "prores_ks") args << "-profile:v" << QString::number(t.mov.proresProfile);
        outPath = QDir(outDir).filePath(baseName + ".mov");
    } else if (t.target == TargetKind::JpgSequence) {
        // Create subfolder
        QString seqDir = QDir(outDir).filePath(baseName + "_jpg_seq"); QDir().mkpath(seqDir);
        outPath = QDir(seqDir).filePath(baseName + "_%04d.jpg");
        args << "-qscale:v" << QString::number(std::clamp(t.jpgSeq.qscale, 2, 31));
    } else if (t.target == TargetKind::PngSequence) {
        QString seqDir = QDir(outDir).filePath(baseName + "_png_seq"); QDir().mkpath(seqDir);
        outPath = QDir(seqDir).filePath(baseName + "_%04d.png");
        if (t.pngSeq.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
        args << "-compression_level" << "9";
    } else if (t.target == TargetKind::TifSequence) {
        QString seqDir = QDir(outDir).filePath(baseName + "_tif_seq"); QDir().mkpath(seqDir);
        outPath = QDir(seqDir).filePath(baseName + "_%04d.tif");
        args << "-c:v" << "tiff";
        if (!t.tifSeq.compression.isEmpty()) args << "-compression_algo" << t.tifSeq.compression.toLower();
        if (t.tifSeq.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
    } else if (t.target == TargetKind::ImageJpg) {
        outPath = QDir(outDir).filePath(baseName + ".jpg");
        args << "-frames:v" << "1" << "-qscale:v" << QString::number(std::clamp(31 - (t.jpg.quality/4), 2, 31));
    } else if (t.target == TargetKind::ImagePng) {
        outPath = QDir(outDir).filePath(baseName + ".png");
        if (t.png.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
        args << "-frames:v" << "1";
    } else if (t.target == TargetKind::ImageTif) {
        outPath = QDir(outDir).filePath(baseName + ".tif");
        args << "-frames:v" << "1" << "-c:v" << "tiff";
        if (!t.tif.compression.isEmpty()) args << "-compression_algo" << t.tif.compression.toLower();
        if (t.tif.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
    }

    if (t.conflict == ConflictAction::AutoRename) outPath = uniqueOutPath(outPath);
    // For Overwrite we rely on -y above; for Skip handled earlier

    args << outPath;
    return true;
}

#ifdef Q_OS_WIN
bool MediaConverterWorker::suspendProcess()
{
    if (!m_proc) return false; DWORD pid = m_proc->processId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    if (!Thread32First(snap, &te)) { CloseHandle(snap); return false; }
    do {
        if (te.th32OwnerProcessID == pid) {
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (hThread) { SuspendThread(hThread); CloseHandle(hThread); }
        }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return true;
}

bool MediaConverterWorker::resumeProcess()
{
    if (!m_proc) return false; DWORD pid = m_proc->processId();
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    THREADENTRY32 te; te.dwSize = sizeof(te);
    if (!Thread32First(snap, &te)) { CloseHandle(snap); return false; }
    do {
        if (te.th32OwnerProcessID == pid) {
            HANDLE hThread = OpenThread(THREAD_SUSPEND_RESUME, FALSE, te.th32ThreadID);
            if (hThread) { ResumeThread(hThread); CloseHandle(hThread); }
        }
    } while (Thread32Next(snap, &te));
    CloseHandle(snap);
    return true;
}
#endif

