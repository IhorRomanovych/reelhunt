// Game/ui_text.h - draws text with a game bitmap font into a 2D overlay batch.
#pragma once
#include "engine_types.h"

#include <string>

#include "Assets/font.h"
#include "Renderer/ui.h"

namespace Renderer { class Texture; }

namespace Game {

struct UiColor {
    uint8 r = 180, g = 180, b = 180, a = 255;
};

// A font (glyph metrics) bound to its atlas texture. Glyph sizes follow the original frontend:
// atlas pixels / 512 in screen units on both axes, multiplied by `scale`. The frontend's text
// (0x5e59c0) also multiplies the widths by the screen's height / width, `aspect`, so its letters
// keep their shape on any screen.
class UiText {
public:
    UiText() = default;
    UiText(const Assets::Font* font, const Renderer::Texture* atlas, float32 aspect = 1.0f)
        : font_(font), atlas_(atlas), aspect_(aspect) {}

    [[nodiscard]] bool valid() const noexcept { return font_ && atlas_; }
    [[nodiscard]] float32 lineHeight(float32 scale) const noexcept { return font_ ? font_->lineHeight * scale : 0.0f; }
    [[nodiscard]] float32 width(const std::string& text, float32 scale) const noexcept;

    // Draws `text` with its top-left corner at (x, y). Characters without a glyph advance by a
    // space. Lower-case letters fall back to upper case when the font has no lower case.
    void draw(Renderer::UiBatch& batch, const std::string& text, float32 x, float32 y, float32 scale, UiColor color) const
    {
        draw(batch, text, x, y, scale * aspect_, scale, color);
    }
    // ... `scaleX` on the widths and advances (width(text, scaleX)), `scaleY` on the heights.
    void draw(Renderer::UiBatch& batch, const std::string& text, float32 x, float32 y, float32 scaleX, float32 scaleY,
              UiColor color) const;

private:
    [[nodiscard]] const Assets::FontGlyph* glyphFor(char c) const noexcept;

    const Assets::Font* font_ = nullptr;
    const Renderer::Texture* atlas_ = nullptr;
    float32 aspect_ = 1.0f;
};

} // namespace Game
