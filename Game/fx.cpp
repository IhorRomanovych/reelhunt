#include "Game/fx.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <sstream>

#include "Assets/ini_records.h"
#include "Game/texture_set.h"
#include "Math/matrix.h"
#include "Renderer/material.h"
#include "Renderer/mesh.h"
#include "Renderer/scene.h"

namespace Game {
namespace {

constexpr std::size_t kMaxParticles = 512; // ours: the pool of quads
constexpr float32 kPi = 3.14159265f;

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

std::string trim(const std::string& s)
{
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a])))
        ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1])))
        --b;
    return s.substr(a, b - a);
}

FxCurve constant(float32 v)
{
    FxCurve c;
    c.keys = {{0.0f, v}};
    return c;
}

FxCurve linear(float32 a, float32 b)
{
    FxCurve c;
    c.keys = {{0.0f, a}, {1.0f, b}};
    return c;
}

// A unit vector within `degrees` of `dir`.
Math::Vector3 spread(const Math::Vector3& dir, float32 degrees, std::mt19937& rng)
{
    const float32 len = dir.length();
    Math::Vector3 d = len > 1e-5f ? dir * (1.0f / len) : Math::Vector3{0.0f, 1.0f, 0.0f};
    if (degrees <= 0.0f)
        return d;
    std::uniform_real_distribution<float32> unit(0.0f, 1.0f);
    const float32 a = degrees * kPi / 180.0f * unit(rng), phi = 2.0f * kPi * unit(rng);
    const Math::Vector3 helper = std::fabs(d.y) < 0.9f ? Math::Vector3{0, 1, 0} : Math::Vector3{1, 0, 0};
    const Math::Vector3 u = d.cross(helper).normalized(), v = d.cross(u);
    return (d * std::cos(a) + (u * std::cos(phi) + v * std::sin(phi)) * std::sin(a)).normalized();
}

} // namespace

float32 FxCurve::at(float32 t) const
{
    if (keys.empty())
        return 0.0f;
    if (keys.size() == 1)
        return keys.front().second;
    if (looped && keys.back().first > 1e-5f)
        t = std::fmod(std::max(0.0f, t), keys.back().first);
    if (t <= keys.front().first)
        return keys.front().second;
    for (std::size_t i = 1; i < keys.size(); ++i)
        if (t <= keys[i].first)
        {
            const auto& [t0, v0] = keys[i - 1];
            const auto& [t1, v1] = keys[i];
            return t1 > t0 ? v0 + (v1 - v0) * (t - t0) / (t1 - t0) : v1;
        }
    return keys.back().second;
}

const FxCurve* FxEmitterDef::curve(const char* key) const
{
    const auto it = curves.find(key);
    return it == curves.end() ? nullptr : &it->second;
}

float32 FxEmitterDef::value(const char* key, float32 t, float32 otherwise) const
{
    const FxCurve* c = curve(key);
    return c ? c->at(t) : otherwise;
}

bool FxLibrary::loadProject(const std::string& text)
{
    std::istringstream in(text);
    std::string raw;
    FxSystemDef* system = nullptr;
    FxEmitterDef* emitter = nullptr;
    std::string kind, channel;
    enum class Expect { None, SystemName, PrimName } expect = Expect::None;
    std::size_t before = systems_.size();
    std::vector<std::string> lines;
    while (std::getline(in, raw))
        lines.push_back(trim(raw));
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& l = lines[i];
        if (l.empty())
            continue;
        const std::size_t colon = l.find(':');
        const std::string key = colon == std::string::npos ? l : l.substr(0, colon);
        const std::string val = colon == std::string::npos ? std::string() : trim(l.substr(colon + 1));
        if (key == "FX_SYSTEM_DATA")
        {
            system = nullptr;
            emitter = nullptr;
            kind.clear();
            expect = Expect::SystemName;
            continue;
        }
        if (key == "FX_PRIM_BASE_DATA" && system)
        {
            emitter = &system->emitters.emplace_back();
            kind.clear();
            expect = Expect::PrimName;
            continue;
        }
        if (key == "NAME" && expect == Expect::SystemName)
        {
            FxSystemDef& s = systems_[lower(val)];
            s = FxSystemDef{};
            s.name = val;
            system = &s;
            expect = Expect::None;
            continue;
        }
        if (key == "NAME" && expect == Expect::PrimName && emitter)
        {
            emitter->name = val;
            expect = Expect::None;
            continue;
        }
        if (!system)
            continue;
        if (!emitter)
        {
            if (key == "LENGTH")
                system->length = std::max(0.01f, std::stof(val));
            else if (key == "PLAYMODE")
                system->loops = std::stoi(val) == 2;
            else if (key == "CULLDIST")
                system->cullDistance = std::stof(val);
            continue;
        }
        if (key == "MATRIX")
        {
            std::istringstream m(val);
            float32 f[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
            for (float32& x : f)
                m >> x;
            emitter->axisX = {f[0], f[1], f[2]};
            emitter->axisY = {f[3], f[4], f[5]};
            emitter->axisZ = {f[6], f[7], f[8]};
            emitter->offset = {f[9], f[10], f[11]};
        }
        else if (key == "TEXTURE")
            emitter->texture = val;
        else if (key == "DSTBLENDID")
            emitter->additive = val == "1" || val == "2";
        else if (key.rfind("FX_INFO_", 0) == 0)
            kind = key.substr(8, key.size() - 8 - 5); // FX_INFO_<kind>_DATA
        else if (key == "FX_INTERP_DATA" && !kind.empty() && i > 0)
        {
            // The channel's name is the line before ("RED:"); then LOOPED, NUM_KEYS and the keys.
            channel = lines[i - 1].substr(0, lines[i - 1].find(':'));
            FxCurve c;
            int32 n = 0;
            for (std::size_t j = i + 1; j < lines.size() && j < i + 3; ++j)
            {
                if (lines[j].rfind("LOOPED:", 0) == 0)
                    c.looped = std::stoi(lines[j].substr(7)) != 0;
                else if (lines[j].rfind("NUM_KEYS:", 0) == 0)
                    n = std::stoi(lines[j].substr(9));
            }
            std::size_t j = i + 3;
            for (int32 k = 0; k < n && j + 2 < lines.size(); ++k, j += 3)
            {
                const float32 t = std::stof(lines[j + 1].substr(lines[j + 1].find(':') + 1));
                const float32 v = std::stof(lines[j + 2].substr(lines[j + 2].find(':') + 1));
                c.keys.emplace_back(t, v);
            }
            emitter->curves[kind + "." + channel] = std::move(c);
            i = j - 1;
        }
    }
    return systems_.size() > before;
}

void FxLibrary::loadTemplates(const std::vector<Assets::IniRecord>& records)
{
    for (const Assets::IniRecord& r : records)
    {
        const bool trail = !r.all("TRAIL").empty(), streak = !r.all("STREAK").empty();
        if (r.all("PARTICLE").empty() && !trail && !streak)
            continue;
        auto num = [&](const char* key, std::size_t index, float32 otherwise) { return r.number(key, index).value_or(otherwise); };
        auto vec = [&](const char* key, const Math::Vector3& otherwise) {
            return r.line(key) ? Math::Vector3{num(key, 1, otherwise.x), num(key, 2, otherwise.y), num(key, 3, otherwise.z)} : otherwise;
        };
        FxSystemDef s;
        s.name = r.name;
        const float32 duration = num("EFFECT_DURATION", 1, -1.0f);
        s.loops = duration < 0.0f;
        s.length = duration > 0.0f ? duration : 1.0f;
        FxEmitterDef e;
        e.name = r.name;
        e.sceneAxes = true;
        e.texture = r.text("TEXTURE_FILE").value_or("fx_spark");
        e.additive = Assets::iequals(r.text("DESTBLEND").value_or(""), "BLENDONE");
        const int32 count = std::max(1, static_cast<int32>(num("NUM_PARTICLES", 1, 1.0f)));
        if (trail)
        {
            e.trails = std::max(1, static_cast<int32>(num("NUM_TRAILS", 1, 1.0f)));
            e.trailStep = num("STEP_TIME", 1, 0.0f);
        }
        e.streak = streak;
        const float32 delay = num("MIN_CREATION_DELAY", 1, 0.0f);
        if (delay > 0.0f)
        {
            e.curves["EMRATE.RATE"] = constant(1.0f / delay);
            e.maxAlive = count * std::max(1, e.trails);
        }
        else
            e.burst = count;
        const float32 lifeMin = num("MIN_TIME", 1, 1.0f), lifeMax = num("MAX_TIME", 1, lifeMin);
        e.curves["EMLIFE.LIFE"] = constant(0.5f * (lifeMin + lifeMax));
        e.curves["EMLIFE.BIAS"] = constant(0.5f * (lifeMax - lifeMin));
        e.curves["EMSPEED.SPEED"] = constant(num("SPEED", 1, 0.0f));
        const Math::Vector3 dir = vec("DIRECTION", {0, 1, 0});
        e.curves["EMDIR.DIRX"] = constant(dir.x);
        e.curves["EMDIR.DIRY"] = constant(dir.y);
        e.curves["EMDIR.DIRZ"] = constant(dir.z);
        // RANDOM_CONE (and the blood / chips with no method): a spread about the direction (ours).
        const bool cone = Assets::iequals(r.text("METHOD_VELOCITY").value_or("RANDOM_CONE"), "RANDOM_CONE");
        e.curves["EMANGLE.MAX"] = constant(cone ? (num("RADIUS", 1, 0.0f) > 0.0f ? 30.0f : 15.0f) : 0.0f);
        e.spawnBox = vec("DIMENSIONS", {0, 0, 0}) * 0.5f;
        const Math::Vector3 motion = vec("MOTION", {0, 0, 0});
        e.curves["FORCE.FORCEX"] = constant(motion.x);
        e.curves["FORCE.FORCEY"] = constant(motion.y);
        e.curves["FORCE.FORCEZ"] = constant(motion.z);
        e.curves["FRICTION.FRICTION"] = constant(num("RESISTANCE", 1, 0.0f));
        const Math::Vector3 a{num("RGBA_START", 1, 255), num("RGBA_START", 2, 255), num("RGBA_START", 3, 255)};
        const Math::Vector3 b{num("RGBA_END", 1, a.x), num("RGBA_END", 2, a.y), num("RGBA_END", 3, a.z)};
        e.curves["COLOUR.RED"] = linear(a.x, b.x);
        e.curves["COLOUR.GREEN"] = linear(a.y, b.y);
        e.curves["COLOUR.BLUE"] = linear(a.z, b.z);
        e.curves["COLOUR.ALPHA"] = linear(num("RGBA_START", 4, 255), num("RGBA_END", 4, 0));
        // WIDTH / HEIGHT, grown by EXPANSION times its size a second over its (mean) life (ours: a
        // gunshot's 0.07 m puff to 0.28 m, the smoke grenade's 0.15 m to 3 m).
        const float32 life = 0.5f * (lifeMin + lifeMax);
        // Drawn across: twice WIDTH / HEIGHT; a streak WIDTH long and twice HEIGHT across.
        const float32 width = num("WIDTH", 1, 0.2f), height = num("HEIGHT", 1, width);
        const float32 w = width * (streak ? 1.0f : 2.0f), h = height * 2.0f;
        e.curves["SIZE.SIZEX"] = linear(w, w * (1.0f + num("EXPANSION", 1, 0.0f) * life));
        e.curves["SIZE.SIZEY"] = linear(h, h * (1.0f + num("EXPANSION", 2, 0.0f) * life));
        s.emitters.push_back(std::move(e));
        systems_[lower(r.name)] = std::move(s);
    }
}

const FxSystemDef* FxLibrary::find(const std::string& name) const
{
    const auto it = systems_.find(lower(name));
    return it == systems_.end() ? nullptr : &it->second;
}

// --- FxManager ---------------------------------------------------------------------------------

FxManager::Instance* FxManager::find(int32 id)
{
    for (Instance& in : instances_)
        if (in.id == id)
            return &in;
    return nullptr;
}

bool FxManager::exists(int32 id) const
{
    return std::any_of(instances_.begin(), instances_.end(), [id](const Instance& in) { return in.id == id; });
}

const FxSystemDef* FxManager::defOf(int32 id) const
{
    for (const Instance& in : instances_)
        if (in.id == id)
            return in.def;
    return nullptr;
}

std::optional<Math::Vector3> FxManager::positionOf(int32 id) const
{
    for (const Instance& in : instances_)
        if (in.id == id)
            return in.position;
    return std::nullopt;
}

int32 FxManager::spawn(const FxSystemDef* def, const Math::Vector3& position, const Math::Vector3& forward, bool startNow)
{
    if (!def || def->emitters.empty())
        return 0;
    Instance in;
    in.id = nextId_++;
    in.def = def;
    in.emitters = def->emitters;
    in.position = position;
    in.forward = forward.length() > 1e-5f ? forward.normalized() : Math::Vector3{1, 0, 0};
    in.started = startNow;
    in.carry.assign(in.emitters.size(), 0.0f);
    in.alive.assign(in.emitters.size(), 0);
    in.burst.assign(in.emitters.size(), false);
    instances_.push_back(std::move(in));
    return instances_.back().id;
}

void FxManager::start(int32 id)
{
    if (Instance* in = find(id))
    {
        in->started = true;
        in->stopped = false;
        in->time = 0.0f;
    }
}

void FxManager::kill(int32 id)
{
    if (Instance* in = find(id))
        in->stopped = true;
}

void FxManager::setPosition(int32 id, const Math::Vector3& position)
{
    if (Instance* in = find(id))
        in->position = position;
}

void FxManager::setUp(int32 id, const Math::Vector3& up)
{
    if (Instance* in = find(id); in && up.length() > 1e-5f)
        in->up = up.normalized();
}

void FxManager::setForward(int32 id, const Math::Vector3& forward)
{
    if (Instance* in = find(id); in && forward.length() > 1e-5f)
    {
        in->forward = forward.normalized();
        in->aimed = true;
    }
}

void FxManager::setColours(int32 id, std::optional<Math::Vector4> start, std::optional<Math::Vector4> end)
{
    Instance* in = find(id);
    if (!in)
        return;
    for (FxEmitterDef& e : in->emitters)
    {
        static const char* const kKeys[] = {"COLOUR.RED", "COLOUR.GREEN", "COLOUR.BLUE", "COLOUR.ALPHA"};
        for (int32 c = 0; c < 4; ++c)
        {
            FxCurve& curve = e.curves[kKeys[c]];
            if (curve.keys.empty())
                curve = linear(255.0f, 255.0f);
            const float32 s = start ? (&start->x)[c] : curve.keys.front().second;
            const float32 f = end ? (&end->x)[c] : curve.keys.back().second;
            curve = linear(s, f);
        }
    }
}

Math::Vector3 FxManager::toWorld(const Instance& in, const FxEmitterDef& e, const Math::Vector3& v, bool point) const
{
    if (e.sceneAxes)
        return v; // the INI's effects are given in the scene's axes
    // The prim's matrix, then the tool's frame (Z up) to the scene's, then the effect's facing:
    // its X along `forward`, Y up.
    Math::Vector3 l = e.axisX * v.x + e.axisY * v.y + e.axisZ * v.z + (point ? e.offset : Math::Vector3{});
    l = Math::Vector3{l.x, l.z, -l.y};
    if (in.up.lengthSquared() > 0.5f)
    {
        // Its tool Z (scene-local Y) along `up`.
        const Math::Vector3 Y = in.up;
        const Math::Vector3 helper = std::fabs(Y.x) < 0.95f ? Math::Vector3{1, 0, 0} : Math::Vector3{0, 0, 1};
        const Math::Vector3 Z = helper.cross(Y).normalized(), X = Y.cross(Z);
        return X * l.x + Y * l.y + Z * l.z;
    }
    const Math::Vector3 X = in.forward;
    const Math::Vector3 helper = std::fabs(X.y) < 0.95f ? Math::Vector3{0, 1, 0} : Math::Vector3{1, 0, 0};
    const Math::Vector3 Z = X.cross(helper).normalized(), Y = Z.cross(X);
    return X * l.x + Y * l.y + Z * l.z;
}

void FxManager::emit(Instance& in, std::size_t index, std::mt19937& rng)
{
    if (particles_.size() >= kMaxParticles)
        return;
    const FxEmitterDef& e = in.emitters[index];
    std::uniform_real_distribution<float32> sym(-1.0f, 1.0f), unit(0.0f, 1.0f);
    const float32 t = in.time;
    Particle p;
    p.instance = in.id;
    p.emitter = static_cast<uint8>(index);
    const float32 life = e.value("EMLIFE.LIFE", t, 1.0f) + e.value("EMLIFE.BIAS", t, 0.0f) * sym(rng);
    p.life = std::max(0.05f, life);
    // Where: the prim's origin, a sphere of EMSIZE.RADIUS, the INI's DIMENSIONS box.
    Math::Vector3 at{0, 0, 0};
    if (const float32 r = e.value("EMSIZE.RADIUS", t, 0.0f); r > 0.0f)
        at = spread({0, 1, 0}, 180.0f, rng) * (r * std::cbrt(unit(rng)));
    at += Math::Vector3{e.spawnBox.x * sym(rng), e.spawnBox.y * sym(rng), e.spawnBox.z * sym(rng)};
    p.position = in.position + toWorld(in, e, at, true);
    // Which way and how fast: EMDIR within EMANGLE (min..max degrees), EMSPEED +- its bias.
    // (An INI effect the scripts aimed, SetEffectDirection, goes that way instead.)
    const Math::Vector3 dir = e.sceneAxes && in.aimed ? in.forward
                                                      : Math::Vector3{e.value("EMDIR.DIRX", t, 0.0f), e.value("EMDIR.DIRY", t, 0.0f),
                                                                      e.value("EMDIR.DIRZ", t, 1.0f)};
    const float32 lo = e.value("EMANGLE.MIN", t, 0.0f), hi = e.value("EMANGLE.MAX", t, lo);
    const Math::Vector3 d = spread(dir.length() > 1e-5f ? dir : Math::Vector3{0, 0, 1}, lo + (hi - lo) * unit(rng), rng);
    const float32 speed = e.value("EMSPEED.SPEED", t, 0.0f) + e.value("EMSPEED.BIAS", t, 0.0f) * sym(rng);
    p.velocity = toWorld(in, e, d, false) * speed;
    if (e.trails > 0)
    {
        // A trail's copies along the particle's path, STEP_TIME apart (0x489290).
        for (int32 k = 0; k < e.trails && particles_.size() < kMaxParticles; ++k)
        {
            Particle q = p;
            q.position = p.position + p.velocity * (e.trailStep * static_cast<float32>(k));
            particles_.push_back(q);
            ++in.alive[index];
        }
        return;
    }
    particles_.push_back(p);
    ++in.alive[index];
}

void FxManager::update(float32 dt, std::mt19937& rng)
{
    // Emission.
    for (Instance& in : instances_)
    {
        if (!in.started || in.stopped)
            continue;
        for (std::size_t i = 0; i < in.emitters.size(); ++i)
        {
            const FxEmitterDef& e = in.emitters[i];
            if (e.burst > 0 && !in.burst[i])
            {
                in.burst[i] = true;
                for (int32 k = 0; k < e.burst; ++k)
                    emit(in, i, rng);
            }
            const float32 rate = e.value("EMRATE.RATE", in.time, 0.0f);
            in.carry[i] += rate * dt;
            while (in.carry[i] >= 1.0f)
            {
                in.carry[i] -= 1.0f;
                if (e.maxAlive <= 0 || in.alive[i] < e.maxAlive)
                    emit(in, i, rng);
            }
        }
        in.time += dt;
        if (in.time >= in.def->length)
        {
            if (in.def->loops)
            {
                in.time = std::fmod(in.time, in.def->length);
                std::fill(in.burst.begin(), in.burst.end(), false);
            }
            else
                in.stopped = true;
        }
    }
    // The particles: force, friction, noise; gone at the end of their life.
    std::uniform_real_distribution<float32> sym(-1.0f, 1.0f);
    for (Particle& p : particles_)
    {
        Instance* in = find(p.instance);
        p.age += dt;
        if (!in)
            continue;
        const FxEmitterDef& e = in->emitters[p.emitter];
        const float32 f = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const Math::Vector3 force{e.value("FORCE.FORCEX", f, 0.0f), e.value("FORCE.FORCEY", f, 0.0f), e.value("FORCE.FORCEZ", f, 0.0f)};
        p.velocity += toWorld(*in, e, force, false) * dt;
        if (const float32 n = e.value("NOISE.NOISE", f, 0.0f); n > 0.0f)
            p.velocity += Math::Vector3{sym(rng), sym(rng), sym(rng)} * (n * dt);
        if (const float32 fr = e.value("FRICTION.FRICTION", f, 0.0f); fr > 0.0f)
            p.velocity = p.velocity * std::max(0.0f, 1.0f - fr * dt);
        p.position += p.velocity * dt;
    }
    for (std::size_t i = 0; i < particles_.size();)
    {
        if (particles_[i].age < particles_[i].life)
        {
            ++i;
            continue;
        }
        if (Instance* in = find(particles_[i].instance))
            --in->alive[particles_[i].emitter];
        particles_[i] = particles_.back();
        particles_.pop_back();
    }
    // An effect that has stopped goes once its particles have.
    std::erase_if(instances_, [](const Instance& in) {
        return in.stopped && std::all_of(in.alive.begin(), in.alive.end(), [](int32 n) { return n <= 0; });
    });
}

void FxManager::draw(Renderer::Scene& scene, const TextureSet& textures, const Math::Vector3& eye, const Math::Vector3& right,
                     const Math::Vector3& up)
{
    const std::size_t want = std::min(particles_.size(), kMaxParticles);
    while (pool_.size() < want)
    {
        auto mesh = std::make_unique<Renderer::Mesh>();
        const Math::Vector3 n{0.0f, 0.0f, 1.0f};
        mesh->addVertex({-0.5f, -0.5f, 0.0f}, n, {0.0f, 1.0f});
        mesh->addVertex({0.5f, -0.5f, 0.0f}, n, {1.0f, 1.0f});
        mesh->addVertex({0.5f, 0.5f, 0.0f}, n, {1.0f, 0.0f});
        mesh->addVertex({-0.5f, 0.5f, 0.0f}, n, {0.0f, 0.0f});
        mesh->addTriangle(0, 1, 2);
        mesh->addTriangle(0, 2, 3);
        mesh->computeBoundingBox();
        mesh->setLighting(Renderer::MeshLighting::Unlit);
        mesh->setCastsShadow(false);
        mesh->setVisible(false);
        pool_.push_back(mesh.get());
        scene.addEntity(std::move(mesh));
    }
    std::size_t used = 0;
    for (const Particle& p : particles_)
    {
        if (used >= pool_.size())
            break;
        Instance* in = find(p.instance);
        if (!in || (p.position - eye).length() > in->def->cullDistance)
            continue;
        const FxEmitterDef& e = in->emitters[p.emitter];
        const float32 f = std::clamp(p.age / p.life, 0.0f, 1.0f);
        const float32 sx = e.value("SIZE.SIZEX", f, 0.1f), sy = e.value("SIZE.SIZEY", f, sx);
        const float32 a = e.value("COLOUR.ALPHA", f, 255.0f) / 255.0f;
        if (sx <= 1e-4f || sy <= 1e-4f || a <= 0.004f)
            continue;
        Renderer::Mesh& mesh = *pool_[used++];
        Renderer::Material m = Renderer::Material::defaultMaterial();
        m.setFlag(Engine::RenderMode::BackCull, false);
        m.setFlag(e.additive ? Engine::RenderMode::Additive : Engine::RenderMode::BlendOn, true);
        if (const Renderer::Texture* t = textures.find(e.texture))
            m.setTexture(*t);
        m.setDiffuse(Math::Vector4{e.value("COLOUR.RED", f, 255.0f) / 255.0f, e.value("COLOUR.GREEN", f, 255.0f) / 255.0f,
                                   e.value("COLOUR.BLUE", f, 255.0f) / 255.0f, std::clamp(a, 0.0f, 1.0f)});
        mesh.setMaterial(m);
        Math::Vector3 x = right * sx, y = up * sy, centre = p.position;
        if (e.streak)
        {
            // From the particle along its velocity, turned about it to face the camera (0x488a10).
            const Math::Vector3 dir = p.velocity.length() > 1e-6f ? p.velocity.normalized() : in->forward;
            const Math::Vector3 side = dir.cross(p.position - eye);
            if (side.length() < 1e-5f)
                continue;
            x = dir * sx;
            y = side.normalized() * sy;
            centre = p.position + dir * (0.5f * sx);
        }
        const Math::Vector3 z = x.cross(y).normalized();
        Math::Matrix4x4 tf = Math::Matrix4x4::identity();
        tf.m00 = x.x; tf.m10 = x.y; tf.m20 = x.z;
        tf.m01 = y.x; tf.m11 = y.y; tf.m21 = y.z;
        tf.m02 = z.x; tf.m12 = z.y; tf.m22 = z.z;
        tf.m03 = centre.x; tf.m13 = centre.y; tf.m23 = centre.z;
        mesh.setTransform(tf);
        mesh.setVisible(true);
    }
    for (std::size_t i = used; i < pool_.size(); ++i)
        pool_[i]->setVisible(false);
}

} // namespace Game
