#pragma once
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
#include <libavutil/time.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
}
#include <memory>
#include <stdexcept>
#include <string>

namespace flux {
inline std::string avError(int code) { char buffer[AV_ERROR_MAX_STRING_SIZE]{}; av_strerror(code, buffer, sizeof(buffer)); return buffer; }
inline void avCheck(int code, const char* operation) { if (code < 0) throw std::runtime_error(std::string(operation) + ": " + avError(code)); }
struct PacketDeleter { void operator()(AVPacket* p) const { av_packet_free(&p); } };
struct FrameDeleter { void operator()(AVFrame* p) const { av_frame_free(&p); } };
struct CodecDeleter { void operator()(AVCodecContext* p) const { avcodec_free_context(&p); } };
struct InputDeleter { void operator()(AVFormatContext* p) const { avformat_close_input(&p); } };
struct SwrDeleter { void operator()(SwrContext* p) const { swr_free(&p); } };
struct SwsDeleter { void operator()(SwsContext* p) const { sws_freeContext(p); } };
struct GraphDeleter { void operator()(AVFilterGraph* p) const { avfilter_graph_free(&p); } };
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;
using InputPtr = std::unique_ptr<AVFormatContext, InputDeleter>;
using SwrPtr = std::unique_ptr<SwrContext, SwrDeleter>;
using SwsPtr = std::unique_ptr<SwsContext, SwsDeleter>;
using GraphPtr = std::unique_ptr<AVFilterGraph, GraphDeleter>;
inline PacketPtr makePacket() { PacketPtr p(av_packet_alloc()); if (!p) throw std::bad_alloc(); return p; }
inline FramePtr makeFrame() { FramePtr p(av_frame_alloc()); if (!p) throw std::bad_alloc(); return p; }
struct Dictionary {
    AVDictionary* value = nullptr;
    ~Dictionary() { av_dict_free(&value); }
    Dictionary() = default;
    Dictionary(const Dictionary&) = delete;
    Dictionary& operator=(const Dictionary&) = delete;
    void set(const char* key, const char* val) { avCheck(av_dict_set(&value, key, val, 0), "av_dict_set"); }
};
inline CodecPtr openDecoder(AVStream* stream) {
    const AVCodec* codec = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!codec) throw std::runtime_error("Decoder is not available in this FFmpeg build");
    CodecPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) throw std::bad_alloc();
    avCheck(avcodec_parameters_to_context(ctx.get(), stream->codecpar), "Copy codec parameters");
    ctx->pkt_timebase = stream->time_base;
    ctx->thread_count = 2;
    avCheck(avcodec_open2(ctx.get(), codec, nullptr), "Open decoder");
    return ctx;
}
}
