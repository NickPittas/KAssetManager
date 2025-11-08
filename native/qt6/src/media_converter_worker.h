#pragma once

#include <QObject>
#include <QProcess>
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>

class QFileInfo;
class QRegularExpressionMatch;

class MediaConverterWorker : public QObject {
    Q_OBJECT
public:
    enum class TargetKind { VideoMP4, VideoMOV, JpgSequence, PngSequence, TifSequence, ImageJpg, ImagePng, ImageTif };
    enum class RateMode { CBR, VBR };
    enum class ConflictAction { AutoRename, Overwrite, Skip };

    struct OptionsMP4 {
        QString codec;            // "h264" or "hevc"
        RateMode rateMode = RateMode::VBR;
        int bitrateMbps = 8;      // for CBR/VBR target average
        int fps = 24;             // input frame rate for image sequences
    };
    struct OptionsMOV {
        QString codec;            // "h264", "prores_ks", "qtrle" (Animation)
        int proresProfile = 2;    // 0 proxy,1 lt,2 422,3 hq,4 4444
        int fps = 24;             // input frame rate for image sequences
    };
    struct OptionsJPGSeq { int qscale = 5; int padDigits = 4; int startNumber = 1; };
    struct OptionsPNGSeq { bool includeAlpha = true; int padDigits = 4; int startNumber = 1; };
    struct OptionsTIFSeq { QString compression; bool includeAlpha = true; int padDigits = 4; int startNumber = 1; };
    struct OptionsJPG { int quality = 90; };
    struct OptionsPNG { bool includeAlpha = true; };
    struct OptionsTIF { QString compression; bool includeAlpha = true; };

    struct Task {
        QString sourcePath;
        QString outputDir;
        TargetKind target;
        // Optional scaling; if 0 -> not applied for that dimension
        int scaleWidth = 0;
        int scaleHeight = 0;
        bool forceEven = true; // make dims divisible by 2 for video
        ConflictAction conflict = ConflictAction::AutoRename;
        // Per-target options
        OptionsMP4 mp4;
        OptionsMOV mov;
        OptionsJPGSeq jpgSeq;
        OptionsPNGSeq pngSeq;
        OptionsTIFSeq tifSeq;
        OptionsJPG jpg;
        OptionsPNG png;
        OptionsTIF tif;
    };

    explicit MediaConverterWorker(QObject* parent=nullptr);

    void setFfmpegPath(const QString& path) { m_ffmpegPath = path; }
    void setMagickPath(const QString& path) { m_magickPath = path; }


signals:
    void queueStarted(int total);
    void fileStarted(int index, const QString& srcPath, const QString& outPath, qint64 durationMs);
    void logLine(const QString& line);
    void currentFileProgress(int index, int percent, qint64 outTimeMs, qint64 totalMs);
    void overallProgress(int percent);
    void fileFinished(int index, bool success, const QString& errorMsg);
    void queueFinished(bool allSuccess);

public slots:
    void start(const QVector<Task>& tasks);
    void cancelAll();
    void retryCurrent();
    void continueAfterFailure();

private slots:
    void onReadyStdOut();
    void onReadyStdErr();
    void onFinished(int exitCode, QProcess::ExitStatus status);

private:
    // Build external command (ffmpeg or ImageMagick) and compute output path for given task
    bool buildCommand(const Task& t, QString& program, QString& outPath, QStringList& args, qint64& estDurationMs, QString& err) const;
    static bool probeDurationMs(const QString& ffmpeg, const QString& input, qint64& durMs, int& width, int& height, QString& err);
    static QString uniqueOutPath(const QString& basePath);
    static QString scaleFilterFor(const Task& t, bool isVideo);
    static double probeAvgFps(const QString& ffmpeg, const QString& input);
    static qint64 countSequenceFrames(const QFileInfo& inFi, const QRegularExpressionMatch& mm, int pad);


    void startNext();

    QString m_magickPath;

private:
    QString m_ffmpegPath;
    QVector<Task> m_tasks;
    int m_index = -1;
    QProcess* m_proc = nullptr;
    bool m_cancelling = false;
    bool m_waitingOnError = false;
    qint64 m_curDurationMs = 0;
    qint64 m_estTotalFrames = 0;
    QElapsedTimer m_timer;
};

