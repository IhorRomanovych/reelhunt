// Game/perf_overlay.h - frame-time counter: FPS, frame and CPU time, GPU time per render pass.
//
// Fed once per frame with the wall time since the previous frame, the CPU time spent before
// present (update + draw-list build + recording) and the renderer's FrameStats of the last
// presented frame. Averages over half a second; draws in the top-left corner while shown and
// logs a line every two seconds. The window presents with vsync (FIFO), so the frame time is
// capped at the refresh rate; the CPU and GPU times are what a frame actually costs.
#pragma once
#include "engine_types.h"

#include <array>
#include <string>
#include <vector>

#include "Game/ui_text.h"
#include "Renderer/frame_stats.h"
#include "Renderer/ui.h"

namespace Game {

class PerfOverlay {
public:
    void setShown(bool on) noexcept { shown_ = on; }
    void toggle() noexcept { shown_ = !shown_; }
    [[nodiscard]] bool shown() const noexcept { return shown_; }

    void frame(float32 frameSeconds, float32 cpuMs, const Renderer::FrameStats& stats);
    void draw(Renderer::UiBatch& ui, const UiText& text) const;

    // The text lines currently shown (also what is logged).
    [[nodiscard]] const std::vector<std::string>& lines() const noexcept { return lines_; }

private:
    static constexpr std::size_t kPasses = static_cast<std::size_t>(Renderer::GpuPass::Count);

    bool shown_ = false;
    // Running sums since the last publish.
    float32 window_ = 0.0f, cpuSum_ = 0.0f, gpuSum_ = 0.0f, worstFrame_ = 0.0f;
    uint32 frames_ = 0u, gpuFrames_ = 0u;
    std::array<float32, kPasses> passSum_{};
    float32 sinceLog_ = 0.0f;
    std::vector<std::string> lines_;
};

} // namespace Game
