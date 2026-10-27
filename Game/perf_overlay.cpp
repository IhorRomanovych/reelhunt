// Game/perf_overlay.cpp - frame-time counter. See perf_overlay.h.
#include "Game/perf_overlay.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iostream>

namespace Game {

namespace {

constexpr float32 kPublishSeconds = 0.5f;
constexpr float32 kLogSeconds = 2.0f;

std::string format(const char* fmt, auto... args)
{
    char buf[160];
    std::snprintf(buf, sizeof(buf), fmt, args...);
    return buf;
}

} // namespace

void PerfOverlay::frame(float32 frameSeconds, float32 cpuMs, const Renderer::FrameStats& stats)
{
    window_ += frameSeconds;
    cpuSum_ += cpuMs;
    worstFrame_ = std::max(worstFrame_, frameSeconds);
    ++frames_;
    if (stats.gpu.valid)
    {
        gpuSum_ += stats.gpu.totalMs;
        for (std::size_t i = 0; i < kPasses; ++i)
            passSum_[i] += stats.gpu.passMs[i];
        ++gpuFrames_;
    }
    if (window_ < kPublishSeconds || frames_ == 0u)
        return;

    const float32 n = static_cast<float32>(frames_);
    lines_.clear();
    lines_.push_back(format("FPS %.0f  frame %.2f ms (worst %.1f)", n / window_, window_ * 1000.0f / n, worstFrame_ * 1000.0f));
    if (gpuFrames_ > 0u)
    {
        const float32 g = static_cast<float32>(gpuFrames_);
        lines_.push_back(format("CPU %.2f ms  GPU %.2f ms", cpuSum_ / n, gpuSum_ / g));
        for (std::size_t i = 0; i < kPasses; ++i)
            lines_.push_back(format("  %s %.2f", Renderer::gpuPassName(static_cast<Renderer::GpuPass>(i)),
                                    passSum_[i] / g));
    }
    else
        lines_.push_back(format("CPU %.2f ms  GPU n/a", cpuSum_ / n));
    lines_.push_back(format("draws %u  lights %u  shadowed %u", stats.draws, stats.lights, stats.shadowLights));

    sinceLog_ += window_;
    if (shown_ && sinceLog_ >= kLogSeconds)
    {
        sinceLog_ = 0.0f;
        std::cerr << "perf:";
        for (const std::string& l : lines_)
            std::cerr << " | " << l;
        std::cerr << "\n";
    }
    window_ = cpuSum_ = gpuSum_ = worstFrame_ = 0.0f;
    frames_ = gpuFrames_ = 0u;
    passSum_.fill(0.0f);
}

void PerfOverlay::draw(Renderer::UiBatch& ui, const UiText& text) const
{
    if (!shown_ || lines_.empty() || !text.valid())
        return;
    constexpr float32 kScale = 0.45f, kX = 0.015f, kY = 0.015f;
    const float32 line = text.lineHeight(kScale);
    // Upper case: the frontend's LCD font draws "dB" (the volume unit) for a lower-case d.
    std::vector<std::string> upper = lines_;
    float32 width = 0.0f;
    for (std::string& l : upper)
    {
        std::transform(l.begin(), l.end(), l.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        width = std::max(width, text.width(l, kScale));
    }
    ui.rect(kX - 0.006f, kY - 0.004f, kX + width + 0.006f, kY + line * static_cast<float32>(upper.size()) + 0.004f, 0, 0, 0, 170);
    float32 y = kY;
    for (const std::string& l : upper)
    {
        text.draw(ui, l, kX, y, kScale, UiColor{200, 230, 200, 255});
        y += line;
    }
}

} // namespace Game
