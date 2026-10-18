#include "VideoFilter.h"
#include <sstream>

namespace flux {
void VideoFilter::process(AVFrame* input, AVRational tb, FilterPreset preset,
                          const std::function<void(AVFrame*)>& receive) {
    if (preset == FilterPreset::None) { graph_.reset(); receive(input); return; }
    if (!graph_ || width_ != input->width || height_ != input->height || format_ != input->format || preset_ != preset) {
        graph_.reset(avfilter_graph_alloc());
        if (!graph_) throw std::bad_alloc();
        graph_->nb_threads = 2;
        width_ = input->width; height_ = input->height; format_ = input->format; preset_ = preset;
        AVRational sar = input->sample_aspect_ratio;
        if (sar.num <= 0 || sar.den <= 0) sar = {1, 1};
        std::ostringstream args;
        args << "video_size=" << width_ << 'x' << height_ << ":pix_fmt=" << format_
             << ":time_base=" << tb.num << '/' << tb.den << ":pixel_aspect=" << sar.num << '/' << sar.den;
        avCheck(avfilter_graph_create_filter(&source_, avfilter_get_by_name("buffer"), "in", args.str().c_str(), nullptr, graph_.get()), "Create filter source");
        avCheck(avfilter_graph_create_filter(&sink_, avfilter_get_by_name("buffersink"), "out", nullptr, nullptr, graph_.get()), "Create filter sink");
        const char* description = "null";
        switch (preset) {
        case FilterPreset::Grayscale: description = "hue=s=0"; break;
        case FilterPreset::Mirror: description = "hflip"; break;
        case FilterPreset::Vivid: description = "eq=contrast=1.12:saturation=1.35:brightness=0.02"; break;
        case FilterPreset::Edges: description = "edgedetect=low=0.08:high=0.25"; break;
        default: break;
        }
        AVFilterInOut* inputs = avfilter_inout_alloc();
        AVFilterInOut* outputs = avfilter_inout_alloc();
        if (!inputs || !outputs) { avfilter_inout_free(&inputs); avfilter_inout_free(&outputs); throw std::bad_alloc(); }
        outputs->name = av_strdup("in"); outputs->filter_ctx = source_; outputs->pad_idx = 0;
        inputs->name = av_strdup("out"); inputs->filter_ctx = sink_; inputs->pad_idx = 0;
        const int result = avfilter_graph_parse_ptr(graph_.get(), description, &inputs, &outputs, nullptr);
        avfilter_inout_free(&inputs); avfilter_inout_free(&outputs);
        avCheck(result, "Parse video filter");
        avCheck(avfilter_graph_config(graph_.get(), nullptr), "Configure video filter");
    }
    avCheck(av_buffersrc_add_frame_flags(source_, input, AV_BUFFERSRC_FLAG_KEEP_REF), "Feed video filter");
    drain(receive);
}
void VideoFilter::drain(const std::function<void(AVFrame*)>& receive) {
    auto frame = makeFrame();
    for (;;) {
        const int result = av_buffersink_get_frame(sink_, frame.get());
        if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) break;
        avCheck(result, "Read video filter"); receive(frame.get()); av_frame_unref(frame.get());
    }
}
void VideoFilter::flush(const std::function<void(AVFrame*)>& receive) {
    if (!graph_) return;
    avCheck(av_buffersrc_add_frame_flags(source_, nullptr, 0), "Flush video filter"); drain(receive);
}
}
