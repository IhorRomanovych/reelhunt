// Renderer/light.h - a local light (point or spot) as a scene entity.
#pragma once
#include "engine_types.h"
#include "renderware_api_stub.h"
#include "Math/vector.h"
#include "Renderer/smartref.h"

namespace Renderer {

enum class LightType : uint8 {
    Point, // all directions
    Spot,  // a cone along direction(); full inside the inner angle, fading to the outer one
};

class Light : public Engine::Rw::Entity {
public:
    Light() : Engine::Rw::Entity(static_cast<uint32>(ObjectType::Light)) {}

    void setLightType(LightType t) noexcept { type_ = t; }
    [[nodiscard]] LightType lightType() const noexcept { return type_; }

    void setPosition(const Math::Vector3& p) noexcept { pos_ = p; }
    [[nodiscard]] Math::Vector3 position() const noexcept { return pos_; }
    // The way the light travels (spot lights).
    void setDirection(const Math::Vector3& d) noexcept { dir_ = d; }
    [[nodiscard]] Math::Vector3 direction() const noexcept { return dir_; }
    // Spot cone half-angles in radians: full light inside `inner`, none beyond `outer`.
    void setCone(float32 inner, float32 outer) noexcept
    {
        coneInner_ = inner;
        coneOuter_ = outer;
    }
    [[nodiscard]] float32 coneInner() const noexcept { return coneInner_; }
    [[nodiscard]] float32 coneOuter() const noexcept { return coneOuter_; }

    void setColor(const Math::Vector4& c) noexcept { col_ = c; }
    [[nodiscard]] Math::Vector4 color() const noexcept { return col_; }
    void setAttenuation(float32 a) noexcept { attenuation_ = a; }
    [[nodiscard]] float32 attenuation() const noexcept { return attenuation_; }
    // Range in metres: the light fades to nothing here (windowed inverse-square falloff).
    void setRadius(float32 r) noexcept { radius_ = r; }
    [[nodiscard]] float32 radius() const noexcept { return radius_; }
    // Intensity; 0 (the default) keeps the light out of the lighting pass.
    void setIntensity(float32 i) noexcept { intensity_ = i; }
    [[nodiscard]] float32 intensity() const noexcept { return intensity_; }

    // Casts shadows (spot lights get a shadow map pair: static scene + characters).
    void setCastingShadow(bool v) noexcept { castingShadow_ = v; }
    [[nodiscard]] bool castingShadow() const noexcept { return castingShadow_; }

    // A RenderWare light of the level (an RpLight a Light_Inst switches on): its flags, 1 it lights
    // the atomics (characters, props), 2 the world. Such a light shines in every lighting mode,
    // RenderWare's way (falling off linearly to its radius, no shadow); 0 is one of our lamps.
    void setRwFlags(uint8 f) noexcept { rwFlags_ = f; }
    [[nodiscard]] uint8 rwFlags() const noexcept { return rwFlags_; }

private:
    LightType type_ = LightType::Point;
    Math::Vector3 pos_{};
    Math::Vector3 dir_{0.0f, -1.0f, 0.0f};
    float32 coneInner_ = 0.6f, coneOuter_ = 1.0f;
    Math::Vector4 col_{1.0f, 1.0f, 1.0f, 1.0f};
    float32 attenuation_ = 1.0f;
    float32 radius_ = 1.0f;
    float32 intensity_ = 0.0f;
    bool castingShadow_ = false;
    uint8 rwFlags_ = 0;
};

} // namespace Renderer
