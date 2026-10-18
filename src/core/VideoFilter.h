#pragma once
#include "FFmpegRAII.h"
#include <functional>

namespace flux {
enum class FilterPreset { None, Grayscale, Mirror, Vivid, Edges };
class VideoFilter {
public:
    void process(AVFrame* input, AVRational timeBase, FilterPreset preset,
                 const std::function<void(AVFrame*)>& receive);
    void flush(const std::function<void(AVFrame*)>& receive);
private:
    void drain(const std::function<void(AVFrame*)>& receive);
    GraphPtr graph_;
    AVFilterContext* source_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    int width_ = 0, height_ = 0, format_ = -1;
    FilterPreset preset_ = FilterPreset::None;
};
}
