// Game/ui_text.cpp - bitmap-font text for the 2D overlay.
#include "Game/ui_text.h"

#include "Renderer/texture.h"

#include <cctype>

namespace Game {

const Assets::FontGlyph* UiText::glyphFor(char c) const noexcept
{
    if (!font_)
        return nullptr;
    const auto uc = static_cast<unsigned char>(c);
    if (const Assets::FontGlyph* g = font_->glyph(uc))
        return g;
    if (std::islower(uc))
        return font_->glyph(static_cast<unsigned char>(std::toupper(uc)));
    return nullptr;
}

float32 UiText::width(const std::string& text, float32 scale) const noexcept
{
    scale *= aspect_;
    float32 w = 0.0f;
    for (char c : text)
        if (const Assets::FontGlyph* g = glyphFor(c))
            w += g->advance * scale; // (a letter the font lacks takes no room, as in the exe)
    return w;
}

void UiText::draw(Renderer::UiBatch& batch, const std::string& text, float32 x, float32 y, float32 scaleX, float32 scaleY,
                  UiColor color) const
{
    if (!valid())
        return;
    const auto aw = static_cast<float32>(atlas_->width());
    const auto ah = static_cast<float32>(atlas_->height());
    for (char c : text)
    {
        // A letter the font lacks is skipped without moving on (font 1's lower-case accents:
        // "Zurück" draws "Zurck").
        const Assets::FontGlyph* g = glyphFor(c);
        if (!g)
            continue;
        if (c != ' ')
        {
            Renderer::UiQuad q;
            q.x0 = x;
            q.y0 = y;
            q.x1 = x + (g->u1 - g->u0) * aw / 512.0f * scaleX;
            q.y1 = y + (g->v1 - g->v0) * ah / 512.0f * scaleY;
            q.u0 = g->u0; q.v0 = g->v0; q.u1 = g->u1; q.v1 = g->v1;
            q.r = color.r; q.g = color.g; q.b = color.b; q.a = color.a;
            q.texture = atlas_;
            batch.add(q);
        }
        x += g->advance * scaleX;
    }
}

} // namespace Game
