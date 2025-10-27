#include "video_metadata.h"
#include <QFile>


#ifdef HAVE_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/log.h>
}
#endif

namespace MediaInfo {

bool probeVideoFile(const QString& filePath, VideoMetadata& out, QString* errorMessage)
{
#ifdef HAVE_FFMPEG
    // Reduce FFmpeg logging noise
    static bool logLevelSet = false;
    if (!logLevelSet) {
        av_log_set_level(AV_LOG_ERROR);
        logLevelSet = true;
    }

    out.videoCodec.clear();
    out.videoProfile.clear();
    out.audioCodec.clear();
    out.width = 0;
    out.height = 0;
    out.fps = 0.0;
    out.bitrate = 0;

    AVFormatContext* fmtCtx = nullptr;
    QByteArray localPath = QFile::encodeName(filePath);
    int ret = avformat_open_input(&fmtCtx, localPath.constData(), nullptr, nullptr);
    if (ret < 0) {
        if (errorMessage) *errorMessage = QString("avformat_open_input failed (%1)").arg(ret);
        return false;
    }

    ret = avformat_find_stream_info(fmtCtx, nullptr);
    if (ret < 0) {
        if (errorMessage) *errorMessage = QString("avformat_find_stream_info failed (%1)").arg(ret);
        avformat_close_input(&fmtCtx);
        return false;
    }

    // Bitrate at container level if available
    if (fmtCtx->bit_rate > 0) {
        out.bitrate = fmtCtx->bit_rate;
    }

    // Find best streams
    int vIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    int aIdx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

    if (vIdx >= 0) {
        AVStream* vs = fmtCtx->streams[vIdx];
        AVCodecParameters* vp = vs ? vs->codecpar : nullptr;
        if (vp) {
            const AVCodec* vcodec = avcodec_find_decoder(vp->codec_id);
            const char* vname = vcodec && vcodec->name ? vcodec->name : avcodec_get_name(vp->codec_id);
            if (vname) out.videoCodec = QString::fromUtf8(vname);

            // Try to derive profile name for common professional codecs
            if (vp->profile != FF_PROFILE_UNKNOWN) {
                switch (vp->codec_id) {
                    case AV_CODEC_ID_H264:
                        switch (vp->profile) {
                            case FF_PROFILE_H264_BASELINE: out.videoProfile = "Baseline"; break;
                            case FF_PROFILE_H264_MAIN:     out.videoProfile = "Main"; break;
                            case FF_PROFILE_H264_HIGH:     out.videoProfile = "High"; break;
                            case FF_PROFILE_H264_HIGH_10:  out.videoProfile = "High10"; break;
                            case FF_PROFILE_H264_HIGH_422: out.videoProfile = "High 4:2:2"; break;
                            case FF_PROFILE_H264_HIGH_444: out.videoProfile = "High 4:4:4"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_HEVC:
                        switch (vp->profile) {
                            case FF_PROFILE_HEVC_MAIN:    out.videoProfile = "Main"; break;
                            case FF_PROFILE_HEVC_MAIN_10: out.videoProfile = "Main 10"; break;
                            case FF_PROFILE_HEVC_REXT:    out.videoProfile = "RExt"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_PRORES:
                        switch (vp->profile) {
                            case FF_PROFILE_PRORES_PROXY:    out.videoProfile = "Proxy"; break;
                            case FF_PROFILE_PRORES_LT:       out.videoProfile = "LT"; break;
                            case FF_PROFILE_PRORES_STANDARD: out.videoProfile = "422"; break;
                            case FF_PROFILE_PRORES_HQ:       out.videoProfile = "422 HQ"; break;
                            case FF_PROFILE_PRORES_4444:     out.videoProfile = "4444"; break;
                            case FF_PROFILE_PRORES_XQ:       out.videoProfile = "4444 XQ"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_DNXHD:
                        switch (vp->profile) {
                            case FF_PROFILE_DNXHD:       out.videoProfile = "DNxHD"; break;
                            case FF_PROFILE_DNXHR_LB:    out.videoProfile = "DNxHR LB"; break;
                            case FF_PROFILE_DNXHR_SQ:    out.videoProfile = "DNxHR SQ"; break;
                            case FF_PROFILE_DNXHR_HQ:    out.videoProfile = "DNxHR HQ"; break;
                            case FF_PROFILE_DNXHR_HQX:   out.videoProfile = "DNxHR HQX"; break;
                            case FF_PROFILE_DNXHR_444:   out.videoProfile = "DNxHR 444"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_MPEG2VIDEO:
                        switch (vp->profile) {
                            case FF_PROFILE_MPEG2_SIMPLE: out.videoProfile = "Simple"; break;
                            case FF_PROFILE_MPEG2_MAIN:   out.videoProfile = "Main"; break;
                            case FF_PROFILE_MPEG2_HIGH:   out.videoProfile = "High"; break;
                            case FF_PROFILE_MPEG2_422:    out.videoProfile = "4:2:2"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_MPEG4:
                        switch (vp->profile) {
                            case FF_PROFILE_MPEG4_SIMPLE:          out.videoProfile = "Simple"; break;
                            case FF_PROFILE_MPEG4_MAIN:            out.videoProfile = "Main"; break;
                            case FF_PROFILE_MPEG4_ADVANCED_SIMPLE: out.videoProfile = "Advanced Simple"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_VP9:
                        switch (vp->profile) {
                            case FF_PROFILE_VP9_0: out.videoProfile = "Profile 0"; break;
                            case FF_PROFILE_VP9_1: out.videoProfile = "Profile 1"; break;
                            case FF_PROFILE_VP9_2: out.videoProfile = "Profile 2"; break;
                            case FF_PROFILE_VP9_3: out.videoProfile = "Profile 3"; break;
                            default: break;
                        }
                        break;
                    case AV_CODEC_ID_AV1:
                        switch (vp->profile) {
                            case FF_PROFILE_AV1_MAIN:         out.videoProfile = "Main"; break;
                            case FF_PROFILE_AV1_HIGH:         out.videoProfile = "High"; break;
                            case FF_PROFILE_AV1_PROFESSIONAL: out.videoProfile = "Professional"; break;
                            default: break;
                        }
                        break;
                    default:
                        break;
                }
            }

            out.width = vp->width;
            out.height = vp->height;

            AVRational fr = vs->avg_frame_rate.num && vs->avg_frame_rate.den ? vs->avg_frame_rate
                                                                            : vs->r_frame_rate;
            if (fr.num > 0 && fr.den > 0) {
                out.fps = static_cast<double>(fr.num) / static_cast<double>(fr.den);
            }

            if (out.bitrate <= 0 && vp->bit_rate > 0) {
                out.bitrate = vp->bit_rate;
            }
        }
    }

    if (aIdx >= 0) {
        AVStream* as = fmtCtx->streams[aIdx];
        AVCodecParameters* ap = as ? as->codecpar : nullptr;
        if (ap) {
            const AVCodec* acodec = avcodec_find_decoder(ap->codec_id);
            const char* aname = acodec && acodec->name ? acodec->name : avcodec_get_name(ap->codec_id);
            if (aname) out.audioCodec = QString::fromUtf8(aname);
            if (out.bitrate <= 0 && ap->bit_rate > 0) {
                out.bitrate = ap->bit_rate; // fallback to audio bitrate if only that is present
            }
        }
    }

    avformat_close_input(&fmtCtx);
    return true;
#else
    Q_UNUSED(filePath);
    out.videoCodec.clear();
    out.videoProfile.clear();
    out.audioCodec.clear();
    out.width = 0;
    out.height = 0;
    out.fps = 0.0;
    out.bitrate = 0;
    if (errorMessage) *errorMessage = "FFmpeg support not available at build time";
    return false;
#endif
}

} // namespace MediaInfo

