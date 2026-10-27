// Renderer/ui.h - 2D overlay quads (menus, HUD), drawn on top of the 3D frame.
//
// Coordinates are normalised to the window: (0,0) top-left, (1,1) bottom-right - the same
// "frontend units" the original game's 2D code uses. Quads are drawn in submission order.
#pragma once
#include "engine_types.h"

#include <vector>

namespace Renderer {

class Texture;

enum class UiBlend : uint8 {
    Alpha,    // src * a + dst * (1 - a)
    Additive, // src + dst (black is transparent)
};

struct UiQuad {
    float32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;   // screen rectangle
    float32 u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;   // texture rectangle
    uint8 r = 255, g = 255, b = 255, a = 255;             // multiplies the texel
    const Texture* texture = nullptr;                     // nullptr = solid colour
    UiBlend blend = UiBlend::Alpha;
    // A free quad (rotated sprites, polygon slices): these four corners, in order around it,
    // with their own texture coordinates, instead of the rectangle above.
    bool freeForm = false;
    float32 px[4]{}, py[4]{}, pu[4]{}, pv[4]{};
};

class UiBatch {
public:
    void clear() noexcept { quads_.clear(); }
    void add(const UiQuad& q) { quads_.push_back(q); }
    // Solid rectangle.
    void rect(float32 x0, float32 y0, float32 x1, float32 y1, uint8 r, uint8 g, uint8 b, uint8 a) {
        UiQuad q;
        q.x0 = x0; q.y0 = y0; q.x1 = x1; q.y1 = y1;
        q.r = r; q.g = g; q.b = b; q.a = a;
        quads_.push_back(q);
    }
    // A triangle (as a free quad with its last corner doubled).
    void triangle(float32 ax, float32 ay, float32 bx, float32 by, float32 cx, float32 cy, uint8 r, uint8 g, uint8 b, uint8 a,
                  const Texture* texture = nullptr, float32 au = 0.0f, float32 av = 0.0f, float32 bu = 0.0f, float32 bv = 0.0f,
                  float32 cu = 0.0f, float32 cv = 0.0f, UiBlend blend = UiBlend::Alpha) {
        UiQuad q;
        q.freeForm = true;
        q.px[0] = ax; q.py[0] = ay; q.pu[0] = au; q.pv[0] = av;
        q.px[1] = bx; q.py[1] = by; q.pu[1] = bu; q.pv[1] = bv;
        q.px[2] = cx; q.py[2] = cy; q.pu[2] = cu; q.pv[2] = cv;
        q.px[3] = cx; q.py[3] = cy; q.pu[3] = cu; q.pv[3] = cv;
        q.r = r; q.g = g; q.b = b; q.a = a;
        q.texture = texture;
        q.blend = blend;
        quads_.push_back(q);
    }
    [[nodiscard]] const std::vector<UiQuad>& quads() const noexcept { return quads_; }

private:
    std::vector<UiQuad> quads_;
};

} // namespace Renderer
