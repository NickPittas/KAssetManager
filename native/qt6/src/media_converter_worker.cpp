#include "media_converter_worker.h"
#include "utils.h"

#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QSet>


namespace {
constexpr qint64 kSeqUpperSearchStart = 10000000; // 10M
constexpr int kSeqUpperSearchMaxDoublings = 32;
constexpr qint64 kSeqUpperSearchHardCap = 100000000; // 100M
}

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
    emit queueStarted(m_tasks.size());
    startNext();
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

    QString err, program, outPath; QStringList args; qint64 durMs = 0;
    if (!buildCommand(t, program, outPath, args, durMs, err)) {
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

    // Estimate total frames for frame-based progress when possible
    m_estTotalFrames = 0;
    if (t.target == TargetKind::VideoMP4 || t.target == TargetKind::VideoMOV) {
        QFileInfo inFi(t.sourcePath);
        static QRegularExpression rxDigits("(\\d+)(?!.*\\d)");
        QRegularExpressionMatch mm = rxDigits.match(inFi.fileName());
        const QString ext = inFi.suffix().toLower();
        static const QSet<QString> imgExts = {"png","jpg","jpeg","tif","tiff","exr","iff","psd","bmp","tga","dds","webp"};
        if (mm.hasMatch() && imgExts.contains(ext)) {
            const int pad = mm.captured(1).length();
            m_estTotalFrames = countSequenceFrames(inFi, mm, pad);
        } else if (durMs > 0) {
            const double fps = probeAvgFps(m_ffmpegPath, t.sourcePath);
            if (fps > 0.0) m_estTotalFrames = qMax<qint64>(1, qint64((durMs/1000.0) * fps + 0.5));
        }
    } else if (t.target == TargetKind::JpgSequence || t.target == TargetKind::PngSequence || t.target == TargetKind::TifSequence) {
        if (durMs > 0) {
            const double fps = probeAvgFps(m_ffmpegPath, t.sourcePath);
            if (fps > 0.0) m_estTotalFrames = qMax<qint64>(1, qint64((durMs/1000.0) * fps + 0.5));
        }
    }

    m_curDurationMs = durMs;

    emit fileStarted(m_index, t.sourcePath, outPath, durMs);

    m_proc = new QProcess(this);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &MediaConverterWorker::onReadyStdOut);
    connect(m_proc, &QProcess::readyReadStandardError, this, &MediaConverterWorker::onReadyStdErr);
    connect(m_proc, qOverload<int,QProcess::ExitStatus>(&QProcess::finished), this, &MediaConverterWorker::onFinished);

    m_proc->setProgram(program);
    m_proc->setArguments(args);
    emit logLine(QString("%1 %2").arg(QFileInfo(program).fileName(), args.join(' ')));
    m_timer.restart();
    m_proc->start();
}

void MediaConverterWorker::onReadyStdOut()
{
    const QByteArray data = m_proc->readAllStandardOutput();
    const QString s = QString::fromUtf8(data);
    emit logLine(s);

    // Parse -progress output
    // Prefer frame-based progress when total frames are known; otherwise fall back to time-based.
    static QRegularExpression rxFrame("frame=([0-9]+)");
    static QRegularExpression rxTime("out_time_ms=([0-9]+)");

    int latestFrame = -1;
    auto it = rxFrame.globalMatch(s);
    while (it.hasNext()) {
        const QRegularExpressionMatch mm = it.next();
        latestFrame = mm.captured(1).toInt();
    }

    if (latestFrame >= 0 && m_estTotalFrames > 0) {
        int percent = int(std::min<qint64>(qint64(latestFrame) * 100 / m_estTotalFrames, 100));
        emit currentFileProgress(m_index, percent, latestFrame, m_estTotalFrames);
        int overall = int(((double(m_index) + (percent/100.0)) / double(m_tasks.size())) * 100.0);
        emit overallProgress(std::clamp(overall, 0, 100));
        return;
    }

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

double MediaConverterWorker::probeAvgFps(const QString& ffmpeg, const QString& input)
{
    // Use ffprobe to fetch avg_frame_rate as a rational (e.g., 30000/1001)
    QString ffprobe = QFileInfo(ffmpeg).dir().filePath("ffprobe.exe");
    if (!QFileInfo::exists(ffprobe)) ffprobe = "ffprobe";
    QProcess p; QStringList a;
    a << "-v" << "error" << "-select_streams" << "v:0" << "-show_entries" << "stream=avg_frame_rate" << "-of" << "default=nw=1:nk=1" << input;
    p.start(ffprobe, a); p.waitForFinished(5000);
    if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
        const QString line = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        const QStringList parts = line.split('/');
        bool ok1=false, ok2=false;
        if (parts.size() == 2) {
            const double num = parts[0].toDouble(&ok1);
            const double den = parts[1].toDouble(&ok2);
            if (ok1 && ok2 && den > 0.0) return num / den;
        }
        double val = line.toDouble(&ok1);
        if (ok1 && val > 0.0) return val;
    }
    return 0.0;
}

qint64 MediaConverterWorker::countSequenceFrames(const QFileInfo& inFi, const QRegularExpressionMatch& mm, int pad)
{
    // Optimized: find first and last existing frame using existence checks only
    // Assumes gaps are acceptable for progress estimation. Prioritizes speed over perfect accuracy.
    const QString name = inFi.fileName();
    const QString pre = name.left(mm.capturedStart(1));
    const QString post = name.mid(mm.capturedEnd(1));
    QDir dir = inFi.dir();

    auto existsFrame = [&](qint64 n) -> bool {
        if (n < 0) return false;
        const QString digits = QString::number(n).rightJustified(pad, QLatin1Char('0'));
        const QString fileName = pre + digits + post;
        return QFileInfo(dir.filePath(fileName)).exists();
    };

    // Current file's frame number is a known existing anchor
    const qint64 curN = mm.captured(1).toLongLong();

    // 1) Find first frame: binary search in [0, curN]
    const qint64 first = Utils::binarySearchFirstTrue(-1, curN, existsFrame);

    // 2) Find last frame using high-start halving then binary search
    const qint64 START_HUGE = kSeqUpperSearchStart;
    qint64 lastKnownExist = curN;
    qint64 lastKnownNonExist = -1;

    // Halve down from a huge number until an existing frame is found (or we drop below current)
    qint64 probe = START_HUGE;
    while (probe > lastKnownExist) {
        if (existsFrame(probe)) { lastKnownExist = probe; break; }
        lastKnownNonExist = probe;
        probe /= 2;
    }
    // If we never found an existing above curN, use curN as existing and set a non-existing above it
    if (lastKnownExist == curN) {
        // Find a non-existing number just above current by doubling until not found
        qint64 up = std::max<qint64>(curN + 1, 2 * curN);
        // Cap to avoid extremely large paths; we just need one non-existing
        for (int i = 0; i < kSeqUpperSearchMaxDoublings; ++i) {
            if (!existsFrame(up)) { lastKnownNonExist = up; break; }
            if (up > kSeqUpperSearchHardCap) { lastKnownNonExist = up + 1; break; } // force a non-existing cap
            up *= 2;
        }
        if (lastKnownNonExist < 0) lastKnownNonExist = curN + 1; // fallback
    } else {
        // Ensure lastKnownNonExist is set (should be from the halving loop)
        if (lastKnownNonExist < 0) lastKnownNonExist = lastKnownExist + 1;
    }

    // Now binary search between [lastKnownExist, lastKnownNonExist] to find the maximum existing frame
    // Guard against degenerate cases
    if (lastKnownNonExist <= lastKnownExist) lastKnownNonExist = lastKnownExist + 1;
    const qint64 last = Utils::binarySearchLastTrue(lastKnownExist, lastKnownNonExist, existsFrame);

    const qint64 total = (last >= first) ? (last - first + 1) : 1;
    return total;
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

bool MediaConverterWorker::buildCommand(const Task& t, QString& program, QString& outPath, QStringList& args, qint64& estDurationMs, QString& err) const
{
    args.clear(); err.clear(); estDurationMs = 0;

    const QFileInfo inFi(t.sourcePath);
    if (!inFi.exists()) { err = QString("Source not found: %1").arg(t.sourcePath); return false; }

    // Helper: prevent paths starting with '-' from being interpreted as flags by tools when used without a preceding option
    auto safePath = [](const QString& p) -> QString {
        QFileInfo fi(p);
        if (!fi.isAbsolute() && p.startsWith('-')) {
            return QStringLiteral("./") + p;
        }
        return p;
    };

    // Derive base output name
    const QString baseName = inFi.completeBaseName();
    const QString outDir = t.outputDir.isEmpty() ? inFi.dir().absolutePath() : t.outputDir;
    QDir().mkpath(outDir);

    // Decide by target which external tool to use
    const bool isSingleImage = (t.target == TargetKind::ImageJpg || t.target == TargetKind::ImagePng || t.target == TargetKind::ImageTif);

    if (isSingleImage) {
        // Use ImageMagick (magick.exe)
        if (m_magickPath.isEmpty()) { err = "ImageMagick (magick) path not set"; return false; }
        program = m_magickPath;

        // Input first for ImageMagick
        args << safePath(inFi.absoluteFilePath());

        // Scaling (preserve aspect by default): WxH, Wx, or xH
        const int W = t.scaleWidth, H = t.scaleHeight;
        QString resizeSpec;
        if (W > 0 && H > 0) resizeSpec = QString("%1x%2").arg(W).arg(H);
        else if (W > 0) resizeSpec = QString("%1x").arg(W);
        else if (H > 0) resizeSpec = QString("x%1").arg(H);
        if (!resizeSpec.isEmpty()) args << "-resize" << resizeSpec;

        // Per-target settings
        if (t.target == TargetKind::ImageJpg) {
            outPath = QDir(outDir).filePath(baseName + ".jpg");
            args << "-quality" << QString::number(t.jpg.quality);
        } else if (t.target == TargetKind::ImagePng) {
            outPath = QDir(outDir).filePath(baseName + ".png");
            if (!t.png.includeAlpha) args << "-alpha" << "off";
        } else if (t.target == TargetKind::ImageTif) {
            outPath = QDir(outDir).filePath(baseName + ".tif");
            QString comp = t.tif.compression.toUpper();
            if (!comp.isEmpty()) args << "-compress" << comp;
            if (!t.tif.includeAlpha) args << "-alpha" << "off";
        }

        if (t.conflict == ConflictAction::AutoRename) outPath = uniqueOutPath(outPath);
        args << safePath(outPath);
        return true;
    }

    // Otherwise, use FFmpeg for video and image sequences
    if (m_ffmpegPath.isEmpty()) { err = "FFmpeg path not set"; return false; }
    program = m_ffmpegPath;

    bool isVideo = (t.target == TargetKind::VideoMP4 || t.target == TargetKind::VideoMOV);

    // Probe duration for progress (videos)
    int inW = 0, inH = 0; probeDurationMs(m_ffmpegPath, t.sourcePath, estDurationMs, inW, inH, err);

    // Input
    args << "-hide_banner" << "-nostdin" << "-y"; // allow overwrite handling below
    args << "-progress" << "pipe:1"; // progress to stdout

    // If converting to video and the input path looks like an image sequence (e.g., contains a trailing number),
    // construct a printf-style pattern and supply -start_number and -framerate before -i.
    bool usedSequenceInput = false;
    if (isVideo) {
        const QString name = inFi.fileName();
        static QRegularExpression rxDigits("(\\d+)(?!.*\\d)"); // last run of digits
        QRegularExpressionMatch mm = rxDigits.match(name);
        // Consider common image extensions
        const QString ext = inFi.suffix().toLower();
        static const QSet<QString> imgExts = {"png","jpg","jpeg","tif","tiff","exr","iff","psd","bmp","tga","dds","webp"};
        if (mm.hasMatch() && imgExts.contains(ext)) {
            const QString digits = mm.captured(1);
            const int pad = digits.length();
            const int startNum = digits.toInt();
            QString pattFile = name;
            pattFile.replace(mm.capturedStart(1), pad, QString("%") + QString("0%1d").arg(pad));
            const QString pattPath = QDir(inFi.dir().absolutePath()).filePath(pattFile);
            // -framerate must appear before -i
            int fps = (t.target == TargetKind::VideoMP4) ? t.mp4.fps : t.mov.fps;
            if (fps <= 0) fps = 24;
            args << "-framerate" << QString::number(fps);
            args << "-start_number" << QString::number(std::max(0, startNum));
            args << "-i" << pattPath;
            usedSequenceInput = true;
        }
    }
    if (!usedSequenceInput) {
        args << "-i" << inFi.absoluteFilePath();
    }

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

        // Determine if we should preserve alpha and set appropriate pixel format
        auto ffprobeHasAlpha = [&](const QString& inputPath)->bool {
            QString ffprobe = QFileInfo(m_ffmpegPath).dir().filePath("ffprobe.exe");
            if (!QFileInfo::exists(ffprobe)) ffprobe = "ffprobe";
            QProcess p; QStringList a;
            a << "-v" << "error" << "-select_streams" << "v:0" << "-show_entries" << "stream=pix_fmt" << "-of" << "default=nw=1:nk=1" << inputPath;
            p.start(ffprobe, a); p.waitForFinished(4000);
            if (p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0) {
                const QString pf = QString::fromUtf8(p.readAllStandardOutput()).trimmed().toLower();
                if (pf.contains("rgba") || pf.contains("bgra") || pf.contains("argb") || pf.contains("abgr") || pf.contains("yuva") || pf.startsWith("ya")) return true;
            }
            return false;
        };
        bool inputHasAlpha = false;
        if (usedSequenceInput) {
            const QString ext = inFi.suffix().toLower();
            static const QSet<QString> alphaImgs = {"png","tif","tiff","exr","psd"};
            inputHasAlpha = alphaImgs.contains(ext);
        } else {
            inputHasAlpha = ffprobeHasAlpha(inFi.absoluteFilePath());
        }
        const bool alphaCapable = ((vcodec == "prores_ks" && t.mov.proresProfile == 4) || (vcodec == "qtrle"));
        if (alphaCapable && inputHasAlpha) {
            if (vcodec == "prores_ks") args << "-pix_fmt" << "yuva444p10le"; // ProRes 4444 with alpha
            else if (vcodec == "qtrle") args << "-pix_fmt" << "argb";        // Animation (QuickTime RLE) with alpha
        }

        outPath = QDir(outDir).filePath(baseName + ".mov");
    } else if (t.target == TargetKind::JpgSequence) {
        // Create subfolder
        QString seqDir = QDir(outDir).filePath(baseName + "_jpg_seq"); QDir().mkpath(seqDir);
        const QString pat = QString("%") + QString("0%1d").arg(std::clamp(t.jpgSeq.padDigits,1,8));
        outPath = QDir(seqDir).filePath(baseName + "_" + pat + ".jpg");
        args << "-start_number" << QString::number(std::max(0, t.jpgSeq.startNumber));
        args << "-qscale:v" << QString::number(std::clamp(t.jpgSeq.qscale, 2, 31));
    } else if (t.target == TargetKind::PngSequence) {
        QString seqDir = QDir(outDir).filePath(baseName + "_png_seq"); QDir().mkpath(seqDir);
        const QString pat = QString("%") + QString("0%1d").arg(std::clamp(t.pngSeq.padDigits,1,8));
        outPath = QDir(seqDir).filePath(baseName + "_" + pat + ".png");
        args << "-start_number" << QString::number(std::max(0, t.pngSeq.startNumber));
        if (t.pngSeq.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
        args << "-compression_level" << "9";
    } else if (t.target == TargetKind::TifSequence) {
        QString seqDir = QDir(outDir).filePath(baseName + "_tif_seq"); QDir().mkpath(seqDir);
        const QString pat = QString("%") + QString("0%1d").arg(std::clamp(t.tifSeq.padDigits,1,8));
        outPath = QDir(seqDir).filePath(baseName + "_" + pat + ".tif");
        args << "-start_number" << QString::number(std::max(0, t.tifSeq.startNumber));
        args << "-c:v" << "tiff";
        if (!t.tifSeq.compression.isEmpty()) args << "-compression_algo" << t.tifSeq.compression.toLower();
        if (t.tifSeq.includeAlpha) args << "-pix_fmt" << "rgba"; else args << "-pix_fmt" << "rgb24";
    }

    // Only auto-rename non-sequence outputs. For image sequences, FFmpeg requires
    // an exact printf-style pattern like filename_%05d.ext; adding suffixes breaks it.
    if (t.conflict == ConflictAction::AutoRename) {
        const bool isSeq = (t.target == TargetKind::JpgSequence || t.target == TargetKind::PngSequence || t.target == TargetKind::TifSequence);
        if (!isSeq) outPath = uniqueOutPath(outPath);
    }
    // For Overwrite we rely on -y above; for Skip handled earlier

    args << safePath(outPath);
    return true;
}


