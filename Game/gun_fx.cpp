// Game/gun_fx.cpp - shot effects. See gun_fx.h.
#include "Game/gun_fx.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <optional>

#include "Assets/txd.h"
#include "Game/material_types.h"
#include "Game/player_field.h"
#include "Math/math.h"
#include "Math/quaternion.h"
#include "Renderer/light.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"

namespace Game {

namespace {

// gunshot_streak (particleeffects.ini): WIDTH 2.5, HEIGHT 0.02, MIN_TIME 0.1, MAX_TIME 0.3.
constexpr float32 kStreakLength = 2.5f, kStreakThickness = 0.02f, kStreakMin = 0.1f, kStreakMax = 0.3f;
// Ours: the flash, shells and nails.
constexpr float32 kFlashTime = 0.05f, kFlashSize = 0.3f, kFlashRadius = 5.0f, kFlashIntensity = 15.0f;
constexpr float32 kGravity = 9.81f;
constexpr float32 kShellStays = 6.0f, kNailStays = 8.0f, kNailSpeed = 60.0f;
constexpr std::size_t kMaxShells = 32;

// Axes and origin -> a model-to-scene matrix (column j = axis j).
Math::Matrix4x4 frame(const Math::Vector3& x, const Math::Vector3& y, const Math::Vector3& z, const Math::Vector3& o)
{
    Math::Matrix4x4 m;
    m.m00 = x.x; m.m10 = x.y; m.m20 = x.z;
    m.m01 = y.x; m.m11 = y.y; m.m21 = y.z;
    m.m02 = z.x; m.m12 = z.y; m.m22 = z.z;
    m.m03 = o.x; m.m13 = o.y; m.m23 = o.z;
    return m;
}

Math::Matrix4x4 rotationOf(Math::Matrix4x4 m)
{
    m.m03 = m.m13 = m.m23 = 0.0f;
    return m;
}

Math::Vector3 turn(const Math::Matrix4x4& m, const Math::Vector3& v)
{
    return m.multiplyPoint(v) - m.multiplyPoint(Math::Vector3{0.0f, 0.0f, 0.0f});
}

// A unit quad facing +Z: x 0..1 (along) or -0.5..0.5 (centred), y -0.5..0.5.
std::unique_ptr<Renderer::Mesh> quad(const Renderer::Texture* tex, bool centred)
{
    auto mesh = std::make_unique<Renderer::Mesh>();
    const float32 x0 = centred ? -0.5f : 0.0f, x1 = centred ? 0.5f : 1.0f;
    const Math::Vector3 n{0.0f, 0.0f, 1.0f};
    mesh->addVertex({x0, -0.5f, 0.0f}, n, {0.0f, 1.0f});
    mesh->addVertex({x1, -0.5f, 0.0f}, n, {1.0f, 1.0f});
    mesh->addVertex({x1, 0.5f, 0.0f}, n, {1.0f, 0.0f});
    mesh->addVertex({x0, 0.5f, 0.0f}, n, {0.0f, 0.0f});
    mesh->addTriangle(0, 1, 2);
    mesh->addTriangle(0, 2, 3);
    mesh->computeBoundingBox();
    Renderer::Material m = Renderer::Material::defaultMaterial();
    m.setFlag(Engine::RenderMode::BackCull, false);
    m.setFlag(Engine::RenderMode::BlendOn, true); // the forward translucent pass, by the texture's alpha
    if (tex)
        m.setTexture(*tex);
    mesh->setMaterial(m);
    mesh->setLighting(Renderer::MeshLighting::Baked); // white vertex light: it glows
    mesh->setCastsShadow(false);
    mesh->setVisible(false);
    return mesh;
}

} // namespace

void GunEffects::load(Renderer::Scene& scene, const std::string& gameRoot, const DffLibrary* models, const TextureSet* textures)
{
    scene_ = &scene;
    models_ = models;
    textures_ = textures;
    if (auto txd = Assets::Txd::load(gameRoot + "levels/GLOBAL/SFX/sfx_pc.txd"))
        fx_.add(*txd);
    streakTex_ = fx_.find("fx_streak");
    fireTex_ = fx_.find("fx_fire1");
    if (!streakTex_ || !fireTex_)
        std::cerr << "gun fx: sfx_pc.txd lacks fx_streak / fx_fire1\n";
}

GunEffects::Sprite& GunEffects::sprite(bool streak)
{
    for (Sprite& s : sprites_)
        if (s.streak == streak && s.age >= s.life)
            return s;
    Sprite s;
    s.streak = streak;
    auto mesh = quad(streak ? streakTex_ : fireTex_, !streak);
    s.mesh = mesh.get();
    scene_->addEntity(std::move(mesh));
    sprites_.push_back(s);
    return sprites_.back();
}

GunEffects::Shell* GunEffects::shell(const std::string& model)
{
    if (!models_ || !textures_ || model.empty())
        return nullptr;
    // A free one of that model, else a new one, else the oldest of that model.
    Shell* oldest = nullptr;
    for (Shell& s : shells_)
        if (s.model == model)
        {
            if (!s.active)
                return &s;
            if (!oldest || s.born < oldest->born)
                oldest = &s;
        }
    if (shells_.size() >= kMaxShells && oldest)
        return oldest;
    const Assets::DffClump* clump = models_->find(model);
    if (!clump)
        return oldest;
    Shell s;
    s.model = model;
    s.meshes = addClumpMeshes(*clump, *scene_, *textures_);
    for (Renderer::Mesh* m : s.meshes)
    {
        m->setCastsShadow(false);
        m->setVisible(false);
    }
    shells_.push_back(std::move(s));
    return &shells_.back();
}

void GunEffects::throwShells(const GunLook& look, const Math::Matrix4x4& hand, float32 delay, float32 feetY)
{
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    for (int32 i = 0; i < std::max(1, look.shellNumber); ++i)
    {
        Shell* s = shell(look.shellModel);
        if (!s)
            return;
        s->active = true;
        s->nail = false;
        s->resting = false;
        s->born = ++born_;
        s->delay = delay + look.randDelay * unit(rng_);
        s->pending = true;
        s->hand = hand;
        s->localPos = look.ejectPos.empty() ? Math::Vector3{0.0f, 0.0f, 0.0f}
                                            : look.ejectPos[static_cast<std::size_t>(i) % look.ejectPos.size()];
        s->localVel = look.velocity * (1.0f + look.velRand * (2.0f * unit(rng_) - 1.0f));
        s->spin = look.spin;
        s->angle = {0.0f, 0.0f, 0.0f};
        s->radius = std::max(0.003f, look.radius);
        s->feetY = feetY;
        s->left = kShellStays;
        s->ct = look.ct;
        s->bounces = 0;
        for (Renderer::Mesh* m : s->meshes)
            m->setVisible(false);
    }
}

void GunEffects::fired(const GunLook& look, const Math::Matrix4x4& hand, const std::vector<ShotLine>& lines, float32 feetY,
                       bool flashSprite)
{
    if (!scene_)
        return;
    const Math::Vector3 muzzle = hand.multiplyPoint(look.flashPos);
    // The flash: a light and a sprite at the muzzle.
    Flash* f = nullptr;
    for (Flash& x : flashes_)
        if (x.left <= 0.0f)
            f = &x;
    if (!f && flashes_.size() < 4)
    {
        auto light = std::make_unique<Renderer::Light>();
        light->setLightType(Renderer::LightType::Point);
        light->setColor(Math::Vector4{1.0f, 0.75f, 0.45f, 1.0f});
        light->setRadius(kFlashRadius);
        light->setCastingShadow(false);
        light->setIntensity(0.0f);
        flashes_.push_back({static_cast<Renderer::Light*>(scene_->addEntity(std::move(light))), 0.0f});
        f = &flashes_.back();
    }
    if (f)
    {
        f->light->setPosition(muzzle);
        f->light->setIntensity(kFlashIntensity);
        f->left = kFlashTime;
    }
    if (fireTex_ && flashSprite)
    {
        Sprite& s = sprite(false);
        s.from = s.to = muzzle;
        s.age = 0.0f;
        s.life = kFlashTime;
        s.size = kFlashSize;
    }
    // A streak down each line.
    std::uniform_real_distribution<float32> life(kStreakMin, kStreakMax);
    for (const ShotLine& l : lines)
    {
        if (streakTex_ && (l.to - muzzle).length() > 0.5f)
        {
            Sprite& s = sprite(true);
            s.from = muzzle;
            s.to = l.to;
            s.age = 0.0f;
            s.life = life(rng_);
        }
        // The nail gun's nails fly the line and stay where it ends.
        if (look.shellsFired)
            if (Shell* n = shell(look.shellModel))
            {
                n->active = true;
                n->nail = true;
                n->resting = false;
                n->born = ++born_;
                n->delay = 0.0f;
                n->pending = false;
                n->hand = hand;
                n->pos = muzzle;
                n->target = l.to;
                n->dropAtTarget = l.body;
                n->left = kNailStays;
                place(*n);
            }
    }
    if (!look.shellsFired && look.ejectDelayFire >= 0.0f)
        throwShells(look, hand, look.ejectDelayFire, feetY);
}

void GunEffects::reloaded(const GunLook& look, const Math::Matrix4x4& hand, float32 feetY)
{
    if (scene_ && !look.shellsFired && look.ejectDelayReload >= 0.0f)
        throwShells(look, hand, look.ejectDelayReload, feetY);
}

void GunEffects::place(Shell& s)
{
    // The model as the gun holds it, turned by its spin.
    Math::Matrix4x4 m = Math::Matrix4x4::translation(s.pos) * rotationOf(s.hand);
    if (!s.nail)
        m = m * Math::Matrix4x4::rotationX(s.angle.x) * Math::Matrix4x4::rotationY(s.angle.y) *
            Math::Matrix4x4::rotationZ(s.angle.z);
    for (Renderer::Mesh* mesh : s.meshes)
    {
        mesh->setTransform(m);
        mesh->setVisible(true);
    }
}

void GunEffects::update(float32 dt, const Math::Vector3& camera, const PlayerField& field)
{
    for (Flash& f : flashes_)
        if (f.left > 0.0f && (f.left -= dt) <= 0.0f)
            f.light->setIntensity(0.0f);

    for (Sprite& s : sprites_)
    {
        if (s.age >= s.life)
            continue;
        s.age += dt;
        if (s.age >= s.life)
        {
            s.mesh->setVisible(false);
            continue;
        }
        if (s.streak)
        {
            // It runs from the muzzle to the end of the line over its life.
            const Math::Vector3 line = s.to - s.from;
            const float32 total = line.length();
            const Math::Vector3 dir = line * (1.0f / total);
            const float32 len = std::min(kStreakLength, total);
            const float32 head = std::min(total, (s.age / s.life) * (total + len));
            const float32 tail = std::max(0.0f, head - len);
            const Math::Vector3 start = s.from + dir * tail;
            const Math::Vector3 mid = s.from + dir * (0.5f * (head + tail));
            Math::Vector3 side = dir.cross(camera - mid);
            if (side.length() < 1e-5f)
                side = dir.cross(Math::Vector3{0.0f, 1.0f, 0.0f});
            side = side.normalized();
            s.mesh->setTransform(frame(dir * std::max(0.01f, head - tail), side * kStreakThickness, dir.cross(side), start));
        }
        else
        {
            // The flash faces the camera.
            Math::Vector3 toCam = (camera - s.from).normalized();
            Math::Vector3 right = Math::Vector3{0.0f, 1.0f, 0.0f}.cross(toCam);
            right = right.length() < 1e-5f ? Math::Vector3{1.0f, 0.0f, 0.0f} : right.normalized();
            const Math::Vector3 up = toCam.cross(right);
            s.mesh->setTransform(frame(right * s.size, up * s.size, toCam, s.from));
        }
        s.mesh->setVisible(true);
    }

    for (Shell& s : shells_)
    {
        if (!s.active)
            continue;
        if (s.pending)
        {
            if ((s.delay -= dt) > 0.0f)
                continue;
            s.pending = false;
            // Thrown out of the gun now.
            s.pos = s.hand.multiplyPoint(s.localPos);
            s.vel = turn(s.hand, s.localVel);
            s.floorY = s.feetY;
        }
        if (s.nail && !s.resting)
        {
            const Math::Vector3 to = s.target - s.pos;
            const float32 d = to.length();
            const float32 step = kNailSpeed * dt;
            if (d <= step)
            {
                s.pos = s.target;
                s.resting = true; // stuck where the line ended
                if (s.dropAtTarget)
                    s.left = 0.0f;
            }
            else
                s.pos += to * (step / d);
        }
        else if (!s.resting)
        {
            // Thrown: it falls, spins, and bounces off the floor until it lies still.
            s.vel.y -= kGravity * dt;
            Math::Vector3 next = s.pos + s.vel * dt;
            // The floor: the world's, or (where he stands on a prop, which the triangle field does
            // not hold) no lower than his feet (ours).
            const auto shot = field.shotHit(s.pos, next);
            std::optional<float32> hit;
            uint8 material = 0;
            if (shot)
            {
                hit = shot->fraction;
                material = shot->material;
            }
            if (!hit && next.y < s.floorY && s.pos.y >= s.floorY)
                hit = (s.pos.y - s.floorY) / std::max(1e-6f, s.pos.y - next.y);
            if (hit)
            {
                s.pos = s.pos + (next - s.pos) * *hit + Math::Vector3{0.0f, s.radius, 0.0f};
                // Its sound (0x59b2f0: while it has bounced at most twice).
                if (s.bounces <= 2 && landings_.size() < 32)
                    landings_.push_back({s.ct, s.pos, (1.0f - static_cast<float32>(s.bounces) * 0.5f) * MaterialTypes::shellLevel(material)});
                ++s.bounces;
                s.vel = Math::Vector3{s.vel.x * 0.5f, -s.vel.y * 0.35f, s.vel.z * 0.5f};
                s.spin = s.spin * 0.5f;
                if (s.vel.length() < 0.4f)
                    s.resting = true;
            }
            else
                s.pos = next;
            s.angle += s.spin * dt;
        }
        if (s.resting && (s.left -= dt) <= 0.0f)
        {
            s.active = false;
            for (Renderer::Mesh* m : s.meshes)
                m->setVisible(false);
            continue;
        }
        place(s);
    }
}

} // namespace Game
