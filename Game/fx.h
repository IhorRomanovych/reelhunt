// Game/fx.h - particle effects: the FX project (levels/GLOBAL/SFX/mh_fx_pc.fxp) and the scripts'
// particle templates (levels/global/data/ParticleEffects.INI).
//
// mh_fx_pc.fxp is RenderWare's FX project text (FX_PROJECT_DATA), the format GTA: San Andreas
// uses: 46 systems (the guns' muzzle flashes <gun>_muzFlash, the bullet hits by surface FXB*,
// Drum_Explode, fire FLM001, smoke FXSMOK*, sparks FXSPARK, sirens, rain ...). A system has a
// LENGTH, a PLAYMODE (0 once, 2 looping) and emitter prims (a texture from mh_fx_pc.txd, a blend:
// DSTBLENDID 1 = added, 5 = alpha-blended, a matrix) with keyed curves (FX_INFO_<kind>_DATA,
// channels of FX_KEYFLOAT_DATA): EMRATE / EMSIZE / EMSPEED / EMDIR / EMANGLE / EMLIFE drive the
// emission over the system's time, FORCE / FRICTION / NOISE / COLOUR / SIZE each particle over its
// life. The tool's frame has Z up (smoke rises along FORCEZ): (x, y, z) -> scene (x, z, -y).
// Ours: the emitter curves are read in seconds, the particle curves over the fraction of life
// (FXSMOK3's colour keys run to 1.0 with a 4 s life); ROTATE / DIR / FLAT / GROUNDCOLLIDE are not
// used; the exe's own FX code is not traced.
//
// ParticleEffects.INI holds 30 templates (PARTICLE, TRAIL, STREAK) the scripts start with
// NewParticleEffect / CreateEffect (slowburn, slowsmoke, AlarmBox, faulty_spark, smokegrenade,
// rain) and the guns' gunshot_* (blood, sparks, chips, smoke). They are turned into the same
// emitters (Y up already): one particle each MIN_CREATION_DELAY up to NUM_PARTICLES alive, or a
// burst of NUM_PARTICLES with no delay; RGBA_START -> RGBA_END, WIDTH / HEIGHT the half sizes
// (CParticleEffect 0x4865e0 draws +-WIDTH x +-HEIGHT) growing by EXPANSION times their size a
// second (ours), MOTION, RESISTANCE, DIRECTION (RANDOM_CONE: spread, ours 30 deg), DIMENSIONS
// (the box they start in).
// A TRAIL (CTrailEffect 0x489290) draws NUM_TRAILS copies of each particle instead of it, the
// copies spread along its path STEP_TIME apart (0x489840 draws them as the particles are). A
// STREAK (CStreakEffect 0x488a10) is a beam from the particle along its velocity, WIDTH long and
// +-HEIGHT across, turned to the camera.
#pragma once
#include "engine_types.h"

#include <map>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <vector>

#include "Math/vector.h"

namespace Assets { struct IniRecord; }
namespace Renderer { class Mesh; class Scene; class Texture; }

namespace Game {

class TextureSet;

// A keyed curve (FX_INTERP_DATA): linear between keys, held past the ends (or wrapped when looped).
struct FxCurve {
    std::vector<std::pair<float32, float32>> keys;
    bool looped = false;
    [[nodiscard]] float32 at(float32 t) const;
};

struct FxEmitterDef {
    std::string name;
    std::string texture;
    bool additive = false;
    bool sceneAxes = false;                      // the INI's (Y up) rather than the tool's (Z up)
    Math::Vector3 axisX{1, 0, 0}, axisY{0, 1, 0}, axisZ{0, 0, 1}, offset{0, 0, 0}; // MATRIX (tool frame)
    std::map<std::string, FxCurve> curves;       // "EMRATE.RATE", "COLOUR.RED", ...
    // The INI's: a burst at the start (no creation delay), at most this many alive.
    int32 burst = 0;
    int32 maxAlive = 0;
    Math::Vector3 spawnBox{0, 0, 0};             // DIMENSIONS (half extents)
    int32 trails = 0;                            // TRAIL: copies per particle (NUM_TRAILS)
    float32 trailStep = 0.0f;                    // TRAIL: STEP_TIME between them
    bool streak = false;                         // STREAK: a beam along the velocity
    [[nodiscard]] const FxCurve* curve(const char* key) const;
    [[nodiscard]] float32 value(const char* key, float32 t, float32 otherwise) const;
};

struct FxSystemDef {
    std::string name;
    float32 length = 1.0f;
    bool loops = false;
    float32 cullDistance = 50.0f;
    std::vector<FxEmitterDef> emitters;
};

class FxLibrary {
public:
    // The FX project text; false when nothing was read.
    bool loadProject(const std::string& text);
    // ParticleEffects.INI's records.
    void loadTemplates(const std::vector<Assets::IniRecord>& records);
    [[nodiscard]] const FxSystemDef* find(const std::string& name) const;
    [[nodiscard]] std::size_t size() const noexcept { return systems_.size(); }

private:
    std::map<std::string, FxSystemDef> systems_; // lower-case name
};

// The effects playing in the level, and their particles drawn as camera-facing quads.
class FxManager {
public:
    // An effect of `def` at `position` (scene frame) facing `forward` (its X axis; ours: the
    // tool's +X is where a muzzle flash points). Started at once unless `start` is false.
    // Returns its id (0 none).
    int32 spawn(const FxSystemDef* def, const Math::Vector3& position, const Math::Vector3& forward = {1, 0, 0},
                bool start = true);
    void start(int32 id);
    void kill(int32 id);                          // it stops emitting; its particles live out
    void setPosition(int32 id, const Math::Vector3& position);
    void setForward(int32 id, const Math::Vector3& forward);
    // Its tool Z (up) along `up` instead (a surface hit: the matrix's at-axis is the normal).
    void setUp(int32 id, const Math::Vector3& up);
    // The scripts' settings of a template's copy: colours (0..255), the emission spread.
    void setColours(int32 id, std::optional<Math::Vector4> start, std::optional<Math::Vector4> end);
    [[nodiscard]] bool exists(int32 id) const;
    // An effect's system (template) and place; nullptr / nullopt when it is gone.
    [[nodiscard]] const FxSystemDef* defOf(int32 id) const;
    [[nodiscard]] std::optional<Math::Vector3> positionOf(int32 id) const;

    void update(float32 dt, std::mt19937& rng);
    // Draws into `scene` (a pool of quads made on first use) facing a camera at `eye` with world
    // axes `right` / `up`.
    void draw(Renderer::Scene& scene, const TextureSet& textures, const Math::Vector3& eye, const Math::Vector3& right,
              const Math::Vector3& up);
    [[nodiscard]] std::size_t particleCount() const noexcept { return particles_.size(); }

private:
    struct Instance {
        int32 id = 0;
        const FxSystemDef* def = nullptr;
        // The effect's own copy of a template's emitters (the scripts' SetEffect* change them).
        std::vector<FxEmitterDef> emitters;
        Math::Vector3 position{0, 0, 0}, forward{1, 0, 0};
        Math::Vector3 up{0, 0, 0};    // setUp (zero: from forward)
        float32 time = 0.0f;
        bool started = false, stopped = false;
        bool aimed = false;           // setForward was called (an INI effect's DIRECTION replaced)
        std::vector<float32> carry;   // emission owed, per emitter
        std::vector<int32> alive;     // particles alive, per emitter
        std::vector<bool> burst;      // the INI burst done
    };
    struct Particle {
        int32 instance = 0;
        uint8 emitter = 0;
        Math::Vector3 position{0, 0, 0}, velocity{0, 0, 0};
        float32 age = 0.0f, life = 1.0f;
    };
    Instance* find(int32 id);
    [[nodiscard]] Math::Vector3 toWorld(const Instance& in, const FxEmitterDef& e, const Math::Vector3& tool, bool point) const;
    void emit(Instance& in, std::size_t e, std::mt19937& rng);

    std::vector<Instance> instances_;
    std::vector<Particle> particles_;
    std::vector<Renderer::Mesh*> pool_; // owned by the scene
    int32 nextId_ = 1;
};

} // namespace Game
