// tests/core_test.cpp - unit tests for core types, math, physics, parsers and game logic.
// Needs no game data. Each check documents a bug that was fixed (see PLAN.md, 2026-10-18 entry).
#include "Assets/binary_reader.h"
#include "Assets/ifp/ifp.h"
#include "Assets/mls.h"
#include "Core/platform.h"
#include "test_paths.h"
#include "Game/ambient_light.h"
#include "Game/entity.h"
#include "Game/keymap.h"
#include "Game/level_frame.h"
#include "Game/level_score.h"
#include "Game/progress.h"
#include "Game/save_slots.h"
#include "Game/save_restore.h"
#include "Game/script_vm.h"
#include "Game/bindings.h"
#include "Game/settings.h"
#include "Game/texture_set.h"
#include "Game/video_modes.h"
#include "Math/transform.h"
#include "Physics/collision.h"
#include "Physics/rigidbody.h"
#include "Physics/triangle_field.h"
#include "Renderer/camera.h"
#include "Renderer/scene.h"
#include "Renderer/shadow_cascades.h"
#include "Renderer/texture.h"

#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void check(bool ok, const char* what)
{
    std::printf("%s %s\n", ok ? "  ok  " : "  FAIL", what);
    if (!ok)
        ++g_failures;
}

bool near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
bool near(const Math::Vector3& a, const Math::Vector3& b, float eps = 1e-4f)
{
    return near(a.x, b.x, eps) && near(a.y, b.y, eps) && near(a.z, b.z, eps);
}

Engine::EngineError makeRuntimeError(int i)
{
    return Engine::EngineError::failure(Engine::EngineError::Code::Invalid, "entry " + std::to_string(i) + " is bad");
}

void testEngineError()
{
    std::puts("EngineError");
    const Engine::EngineError e = makeRuntimeError(42); // message built from a temporary
    check(e.message == "entry 42 is bad", "runtime-built message survives (was a dangling string_view)");
    check(Engine::okError.isOk(), "okError is ok");
}

void testEntitiesAndScene()
{
    std::puts("Entity / Scene");
    Renderer::Scene scene;
    auto* a = scene.addEntity(std::make_unique<Game::GameEntity>(1u));
    auto* b = scene.addEntity(std::make_unique<Game::GameEntity>(2u));
    auto* cam = scene.addEntity(std::make_unique<Renderer::Camera>());
    check(a->id() != 0u && b->id() != 0u && a->id() != b->id(), "entities get unique non-zero ids (all were 0)");
    check(scene.find(b->id()) == b, "find by id");
    check(scene.activeCamera() == cam, "first camera becomes the active camera");
    scene.removeEntities({a->id()});
    check(scene.count() == 2u && scene.find(b->id()) == b && scene.find(a->id()) == nullptr,
          "removeEntities removes exactly the requested entity");
}

void testTransform()
{
    std::puts("Math");
    Math::Transform t;
    t.scale = {2.0f, 1.0f, 1.0f};
    t.rotation = Math::Quaternion::fromAxisAngle({0.0f, 0.0f, 1.0f}, Math::pi / 2.0f);
    // Local +X scaled by 2 then rotated 90 deg about Z -> (0, 2, 0).
    const Math::Vector3 p = t.toMatrix().multiplyPoint({1.0f, 0.0f, 0.0f});
    check(near(p, {0.0f, 2.0f, 0.0f}), "Transform applies scale before rotation (T*R*S)");

    Renderer::Camera cam;
    cam.setProjection(1.0f, 60.0f, 0.5f, 50.0f);
    cam.setView({0.0f, 0.0f, 0.0f}, 0.0f, 0.0f);
    cam.update();
    auto depthAt = [&](float dist) {
        const Math::Vector4 c = cam.clip().multiply({0.0f, 0.0f, -dist, 1.0f});
        return c.z / c.w;
    };
    check(near(depthAt(0.5f), 0.0f) && near(depthAt(50.0f), 1.0f), "projection maps near/far to Vulkan depth 0/1");
}

void testPhysics()
{
    std::puts("Physics");
    const auto sphere = Physics::Collider::makeSphere({0.0f, 0.0f, 0.0f}, 1.0f);
    check(near(sphere.volume(), 4.0f / 3.0f * Math::pi), "sphere volume includes pi");

    Physics::RigidBody ball, box;
    ball.setCollider(Physics::Collider::makeSphere({}, 0.5f));
    box.setCollider(Physics::Collider::makeAABB({}, {1.0f, 1.0f, 1.0f}));
    ball.setPosition({0.0f, 1.4f, 0.0f});  // overlaps the box top (1.0) by 0.1
    box.setPosition({0.0f, 0.0f, 0.0f});
    check(near(ball.boundingBox().min.y, 0.9f), "setPosition moves the collider (bounding box was stale)");
    const auto hits = Physics::CollisionDetector::detect({&ball, &box});
    check(hits.size() == 1u && near(hits[0].penetration, 0.1f, 1e-3f),
          "sphere-vs-box tests the sphere, not the box against itself");
    if (!hits.empty())
        check(hits[0].normal.y > 0.99f, "contact normal points from b (box) to a (ball)");
}

void testByteReader()
{
    std::puts("ByteReader");
    const uint8_t data[] = {'a', 'b', 'c', 0, 'x', 0, 0, 0, 1, 0, 0, 0};
    Assets::ByteReader r(data, sizeof(data));
    std::string s1, s2;
    uint32 v = 0;
    check(r.readPaddedString(s1) && s1 == "abc" && r.offset() == 4u, "padded string, len 3 -> 4-byte block");
    check(r.readPaddedString(s2) && s2 == "x" && r.offset() == 8u, "padded string, len 1 -> 4-byte block");
    check(r.readU32(v) && v == 1u && r.eof(), "u32 little-endian");
    check(!r.readU32(v), "read past the end fails");
}

void testKeymap()
{
    std::puts("Keymap");
    check(Game::Keymap::keyToScancode("F12") == 69, "F12 scancode");
    check(Game::Keymap::keyToScancode("F13") == 104, "F13 scancode (was 70 = PRINTSCREEN)");
    check(Game::Keymap::keyToScancode("F24") == 115, "F24 scancode");
    const Game::Keymap km;
    check(km.actionForKey(10).empty(), "an unloaded keymap binds nothing");
}

void testScriptBranch()
{
    std::puts("ScriptVM");
    Assets::MlsScript sc;
    sc.code = {
        0x12, 0x01, 5, 0x0f, 0x04,                               // 0: push 5
        0x12, 0x01, 6, 0x0f, 0x04,                               // 5: push 6
        0x23, 0x04, 0x01, 0x12, 0x01, 0x01, 0x3f, 30 * 4, 0x33, 0x01, 0x01, // 10: if (5 == 6) else -> word 30
        0x12, 0x01, 1, 0x0f, 0x04,                               // 21: push 1      (then-branch)
        0x1d, 0x01, 99, 0x04,                                    // 26: gamevar_99 := 1
        0x12, 0x01, 7, 0x0f, 0x04,                               // 30: push 7      (branch target)
        0x1d, 0x01, 98, 0x04,                                    // 35: gamevar_98 := 7
        0x11, 0x09, 0x0a, 0x0f, 0x0a, 0x3b, 0x00,                // 39: script end
    };
    Game::ScriptVM vm;
    vm.execute(sc, 0);
    check(vm.getVar("gamevar_99") == 0, "false condition skips the then-branch");
    check(vm.getVar("gamevar_98") == 7, "taken branch resumes exactly at its target (was target + 11)");

    int calls = 0;
    Assets::MlsScript call;
    call.code = {0x5555, 0x11, 0x09, 0x0a, 0x0f, 0x0a, 0x3b, 0x00};
    vm.bind(0x5555, [&](Game::ScriptVM&) { ++calls; });
    vm.execute(call, 0);
    check(calls == 1, "bound native function is dispatched by its id");
}

void testIfpLookup()
{
    std::puts("IFP");
    Assets::IfpFile f;
    Assets::IfpBlock b;
    Assets::IfpAnimation a1, a2;
    a1.name = "Idle_010";
    a2.name = "Idle_01";
    b.animations = {a1, a2};
    f.blocks.push_back(b);
    const Assets::IfpAnimation* found = f.findAnimation("idle_01");
    check(found && found->name == "Idle_01", "exact (case-insensitive) name beats an earlier substring match");
    check(f.findAnimation("dle_01") != nullptr, "substring fallback still works");

    Assets::IfpBoneTrack track;
    float q[4], p[3];
    Assets::sampleTrack(track, 0.0f, q, p);
    check(q[0] == 0.0f && q[1] == 0.0f && q[2] == 0.0f && q[3] == 1.0f, "empty track samples the (x,y,z,w) identity");
}

void testSaveClassData()
{
    std::puts("Save restore");
    const auto shortTrigger = Game::parseClassData("Trigger", {1, 0, 0, 0}); // 4 bytes, layout needs 8
    check(shortTrigger.kind == Game::EntityKind::Trigger && shortTrigger.state == 0u,
          "short classData is not read past its end");
    check(Game::hasIntegrity(Game::EntityKind::Dynamic), "dynamic (D) entities carry integrity");

    Game::RestoredEntity re;
    re.state = Game::parseClassData("Gen_Door_(D)", {0, 0, 0, 0, 1, 0, 0, 0, 2, 0, 0, 0});
    Game::GameEntity ent(1u);
    Game::applyToEntity(ent, re);
    check(ent.health() == 0u, "a destroyed (integrity 0) entity restores as destroyed (was left at 100)");
}

void testTextureSet()
{
    std::puts("TextureSet");
    check(Game::TextureSet::normalizeName("Mp_Wall.PNG") == "mp_wall", "names are lower-cased without extension");

    auto make = [](const char* name, uint32 w, uint8_t alpha) {
        Assets::TxdTexture t;
        t.name = name;
        t.width = w;
        t.height = w;
        t.rgba.assign(static_cast<std::size_t>(w) * w * 4u, 255u);
        t.rgba[3] = alpha;
        return t;
    };
    Game::TextureSet set;
    set.add({make("wall", 2, 255), make("leaves", 2, 0), make("plating", 2, 0)});
    set.add({make("WALL", 4, 255)});
    const Renderer::Texture* wall = set.find("wall");
    check(wall && wall->width() == 4u, "the larger of two same-named textures wins");
    set.setForceOpaque({"plating"});
    Renderer::Material m;
    set.apply(m, set.find("leaves"));
    check(m.isFlag(Engine::RenderMode::AlphaTest), "cut-out texture enables alpha test");
    set.apply(m, set.find("plating"));
    check(!m.isFlag(Engine::RenderMode::AlphaTest), "force-opaque texture disables alpha test");

    // Alpha classes from the histogram: holes only -> cut-out; real partial alpha -> blended.
    Assets::TxdTexture glass = make("glass", 4, 255);
    for (std::size_t i = 3; i < glass.rgba.size(); i += 4)
        glass.rgba[i] = 90; // a uniformly see-through pane
    Assets::TxdTexture fence = make("fence", 4, 255);
    for (std::size_t i = 3; i < fence.rgba.size(); i += 8)
        fence.rgba[i] = 0; // every other texel a hole
    Assets::TxdTexture solid = make("solid", 4, 255);
    check(glass.alphaClass() == Assets::AlphaClass::Blend && fence.alphaClass() == Assets::AlphaClass::Cutout &&
              solid.alphaClass() == Assets::AlphaClass::Opaque,
          "glass blends, a chain-link fence alpha-tests, a wall is opaque");
    Game::TextureSet classes;
    classes.add({glass, fence});
    Renderer::Material gm, fm;
    classes.apply(gm, classes.find("glass"));
    classes.apply(fm, classes.find("fence"));
    check(gm.isFlag(Engine::RenderMode::BlendOn) && !gm.isFlag(Engine::RenderMode::AlphaTest) &&
              fm.isFlag(Engine::RenderMode::AlphaTest) && !fm.isFlag(Engine::RenderMode::BlendOn),
          "blended textures go to the forward pass, cut-outs to alpha test");

    // A character's ambient (exe 0x49a6f0): eased toward the floor light, never below the floor.
    Game::AmbientLight amb;
    const Math::Vector4 first = amb.update(Math::Vector3{0.5f, 0.5f, 0.5f}, 1.0f / 30.0f);
    const Math::Vector4 step = amb.update(Math::Vector3{0.1f, 0.1f, 0.1f}, 1.0f / 30.0f);
    check(std::fabs(first.x - 0.5f) < 1e-5f && std::fabs(step.x - (0.5f - 0.4f / 8.0f)) < 1e-4f,
          "the first sample is taken as is, then it moves an eighth per 1/30 s");
    Math::Vector4 dark{};
    for (int i = 0; i < 200; ++i)
        dark = amb.update(Math::Vector3{0.0f, 0.0f, 0.0f}, 1.0f / 30.0f);
    check(std::fabs(dark.x - 0.08f) < 1e-4f && std::fabs(dark.z - 0.12f) < 1e-4f, "in the dark it stops at (0.08, 0.08, 0.12)");
    const Math::Vector4 kept = amb.update(Math::Vector3{2.0f, 2.0f, 2.0f}, 1.0f / 30.0f);
    check(std::fabs(kept.x - dark.x) < 1e-6f, "an invalid sample (above 1.0) is ignored");

    Renderer::Texture t;
    check(t.create(1, 1), "texture create");
    const Math::Vector4 px[] = {{2.0f, -1.0f, 0.5f, 1.0f}};
    check(t.setPixels(px) && t.rgba8()[0] == 255u && t.rgba8()[1] == 0u && t.rgba8()[2] == 128u,
          "float pixels are clamped and rounded to RGBA8");
    const Math::Vector4 two[2] = {};
    check(!t.setPixels(two), "a pixel buffer of the wrong size is rejected");
}

void testLevelFrame()
{
    std::puts("LevelFrame");
    Game::LevelFrame f;
    f.center = {10.0f, 0.0f, 0.0f};
    f.scale = 0.5f;
    const Math::Matrix4x4 r = Game::LevelFrame::placementRotation({0.0f, 0.0f, 0.0f, 1.0f});
    check(near(r.multiplyPoint({1.0f, 2.0f, 3.0f}), {1.0f, 2.0f, 3.0f}), "identity placement keeps the model upright");
    const Math::Vector3 origin = f.placementOrigin({10.0f, -4.0f, 2.0f}); // Z-up inst -> (x, z, -y)
    check(near(origin, {0.0f, 1.0f, 2.0f}), "placement origin maps Z-up level coords into the scene");
    check(near(f.placedPoint(r, origin, {2.0f, 0.0f, 0.0f}), {1.0f, 1.0f, 2.0f}),
          "model-local points are scaled like the world (collision spheres were not)");
}

void testTriangleField()
{
    std::puts("TriangleField");
    using Tri = Physics::TriangleField::Triangle;
    // A 20 x 20 m floor at y = 0 (two triangles, mixed winding) and a wall at x = 5 facing -x.
    Physics::TriangleField field;
    field.build({Tri{{-10, 0, -10}, {10, 0, -10}, {10, 0, 10}}, Tri{{-10, 0, -10}, {-10, 0, 10}, {10, 0, 10}},
                 Tri{{5, 0, -10}, {5, 10, -10}, {5, 0, 10}}, Tri{{5, 10, -10}, {5, 10, 10}, {5, 0, 10}},
                 Tri{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}}}, 4.0f);
    check(field.size() == 4u, "degenerate triangles are dropped");

    check(near(Physics::TriangleField::closestPoint({3, 5, 3}, Tri{{0, 0, 0}, {10, 0, 0}, {0, 0, 10}}), {3, 0, 3}),
          "closest point inside the face is the projection");
    check(near(Physics::TriangleField::closestPoint({-2, 1, -3}, Tri{{0, 0, 0}, {10, 0, 0}, {0, 0, 10}}), {0, 0, 0}),
          "closest point beyond a corner is that vertex");

    Math::Vector3 p{0.0f, 0.3f, 0.0f}, v{1.0f, -4.0f, 0.0f};
    const bool grounded = field.resolveSphere(p, 0.5f, v);
    check(grounded && near(p, {0.0f, 0.5f, 0.0f}) && near(v, {1.0f, 0.0f, 0.0f}),
          "a sphere sunk into the floor is pushed up to rest on it and grounded");

    p = {4.8f, 2.0f, 0.0f};
    v = {3.0f, 0.0f, 1.0f};
    check(!field.resolveSphere(p, 0.5f, v) && near(p, {4.5f, 2.0f, 0.0f}) && near(v, {0.0f, 0.0f, 1.0f}),
          "a wall pushes the sphere out sideways, removes the velocity into it and does not ground");

    p = {4.8f, 0.3f, 3.0f};
    v = {0.0f, 0.0f, 0.0f};
    check(field.resolveSphere(p, 0.5f, v) && p.x <= 4.5f + 1e-4f && p.y >= 0.5f - 1e-4f,
          "the floor/wall corner pushes the sphere out of both");

    const auto down = field.raycast({1, 5, 1}, {1, -5, 1});
    check(down && std::fabs(*down - 0.5f) < 1e-4f, "a ray down through the floor hits it halfway");
    const auto side = field.raycast({1, 2, 1}, {9, 2, 1});
    check(side && std::fabs(*side - 0.5f) < 1e-4f, "a ray across the wall hits it (camera clipping)");
    check(!field.raycast({1, 2, 1}, {4, 2, 1}), "a segment that stops short of the wall hits nothing");
    {
        // A damaged prop loses its collision (NO_COL_ON_DAMAGE): its triangles leave the field.
        Physics::TriangleField walls = field;
        walls.remove(2, 2); // the wall (build order 2, 3)
        check(!walls.raycast({1, 2, 1}, {9, 2, 1}) && walls.raycast({1, 5, 1}, {1, -5, 1}),
              "removed triangles are hit no more; the rest still are");
    }

    // groundHit reports which triangle is underfoot (in build order, degenerates skipped) and
    // where on it: the floor's first triangle has corners (-10,-10) (10,-10) (10,10).
    const auto hit = field.groundHit(5.0f, -5.0f, 3.0f);
    check(hit && hit->triangle == 0u && near(hit->height, 0.0f), "groundHit finds the floor triangle under a point");
    if (hit)
    {
        const Math::Vector3 a{-10, 0, -10}, b{10, 0, -10}, c{10, 0, 10};
        const Math::Vector3 p = a * (1.0f - hit->u - hit->w) + b * hit->u + c * hit->w;
        check(near(p, {5.0f, 0.0f, -5.0f}), "its barycentric weights rebuild the point");
    }

    p = {0.0f, 3.0f, 0.0f};
    v = {0.0f, -1.0f, 0.0f};
    check(!field.resolveSphere(p, 0.5f, v) && near(p, {0.0f, 3.0f, 0.0f}), "a sphere in the air is untouched");

    const auto g = field.groundBelow(2.0f, -3.0f, 5.0f);
    check(g && near(*g, 0.0f), "groundBelow finds the floor");
    check(!field.groundBelow(2.0f, -3.0f, -1.0f) && !field.groundBelow(50.0f, 0.0f, 5.0f),
          "groundBelow ignores surfaces above the start and outside the field");
}

} // namespace

void testVideoModes()
{
    std::puts("launcher modes (exe 0x4bee40 / 0x4c0830)");
    using Game::ScreenMode;
    const std::vector<ScreenMode> all = {
        {1920, 1080, 32, 144.0f}, {1920, 1080, 32, 60.0f}, {1920, 1080, 32, 120.0f}, {640, 400, 32, 60.0f},
        {800, 600, 32, 60.0f},    {640, 480, 32, 60.0f},   {800, 600, 32, 75.0f},   {1024, 768, 16, 60.0f}};
    const std::vector<ScreenMode> rows = Game::launcherModes(all);
    check(rows.size() == 4 && rows[0].width == 1024 && rows[0].depth == 16 && rows[1].width == 640 && rows[1].height == 480 &&
              rows[2].width == 800 && rows[3].width == 1920,
          "one row per size and depth, at least 640 x 480, smallest first");
    check(Game::modeLabel(rows[2]) == "800 x 600 x 32 ( Fullscreen )", "the exe's row text");
    check(Game::launcherRefresh(all, rows[3]) == 120.0f || Game::launcherRefresh(all, rows[3]) == 60.0f, "a listed rate");
    check(Game::launcherRefresh(all, rows[3]) == 60.0f && Game::launcherRefresh(all, rows[2]) == 60.0f,
          "the lowest refresh of at least 60 Hz");
    check(Game::launcherRefresh({{800, 600, 32, 0.0f}, {800, 600, 32, 75.0f}}, rows[2]) == 0.0f, "a default rate wins");

    Game::VideoChoice c;
    check(Game::launcherModeRow(rows, c) == 2, "nothing saved: 800 x 600 x 32");
    c.index = 3;
    c.mode = rows[3];
    check(Game::launcherModeRow(rows, c) == 3, "the saved row");
    c.index = 0;
    check(Game::launcherModeRow(rows, c) == 3, "a saved mode that moved rows");

    std::vector<Game::DisplayAdapter> adapters(3);
    adapters[0].name = adapters[1].name = "NVIDIA GeForce RTX 5090";
    adapters[0].monitor = "DELL U2720Q";
    adapters[1].monitor = "LG 27GN950";
    adapters[2].name = "AMD Radeon";
    const auto labels = Game::adapterLabels(adapters);
    check(labels[0] == "NVIDIA GeForce RTX 5090 - DELL U2720Q" && labels[1] == "NVIDIA GeForce RTX 5090 - LG 27GN950" &&
              labels[2] == "AMD Radeon",
          "adapters by GPU name; a shared name gets its display's");

    const std::string path = (std::filesystem::temp_directory_path() / "reelhunt_video_test.cfg").string();
    Game::VideoChoice saved;
    saved.device = 1;
    saved.index = 2;
    saved.mode = {1280, 1024, 32, 0.0f};
    const bool wrote = Game::saveVideoChoice(path, saved);
    const auto back = Game::loadVideoChoice(path);
    std::filesystem::remove(path);
    check(wrote && back && back->device == 1 && back->index == 2 && back->mode.width == 1280 && back->mode.height == 1024,
          "the choice round-trips through its file");
}

void testSettingsFile()
{
    std::puts("Settings.dat (exe 0x604c70 / 0x605e50 / 0x606130)");
    const Game::GameSettings d;
    check(d.sfxVolume == 14 && d.musicVolume == 14 && d.subtitles == 1 && d.brightness == 6 && d.mouseX == 10 &&
              d.mouseY == 10 && d.gameDetail == 3 && d.shadowDetail == 3 && d.noiseFx == 1 && d.cameraFx == 1 &&
              d.difficulty == 0 && d.language == 0,
          "the exe's default settings");
    check(std::fabs(d.sfxGain() - 14.0f / 15.0f) < 1e-6f && std::fabs(d.gammaExponent() - 0.9f) < 1e-6f,
          "volume v / 15, gamma 1.2 - b / 20");
    Game::GameSettings ger;
    ger.language = 1;
    check(ger.globalTextSuffix() == "_ger" && ger.levelTextSuffix() == "_g", "German text files");

    Game::SettingsFile f = Game::SettingsFile::defaults();
    f.settings.musicVolume = 3;
    f.settings.ratio = 1;
    f.settings.language = 4;
    const std::string path = (std::filesystem::temp_directory_path() / "reelhunt_settings_test.dat").string();
    const bool wrote = f.save(path);
    const auto size = std::filesystem::file_size(path);
    Game::SettingsFile back;
    const bool read = back.load(path);
    std::filesystem::remove(path);
    check(wrote && size == 704u, "704 bytes: bindings, the settings block, bindings");
    check(read && back.settings.musicVolume == 3 && back.settings.ratio == 1 && back.settings.language == 4 &&
              back.bindingsA == f.bindingsA && back.bindingsB == f.bindingsB,
          "the file round-trips");
    check(!back.load(path), "a missing file is refused");

    check(Game::Bindings::gxtName('W') == "KEYW" && Game::Bindings::gxtName(0x3e9) == "KEYF1" &&
              Game::Bindings::gxtName(0x404) == "KEYPEND" && Game::Bindings::gxtName(Game::Bindings::kMouse + 2) == "PCRMB",
          "key names (0x5d9dd4): KEYW, KEYF1, KEYPEND, PCRMB");

    // The bindings (0x710994 / 0x5d9a20): the defaults come from the player's exe.
    if (!TestPaths::exeTables()) // (it makes them the running game's)
    {
        std::puts("  skip  the default keys (no game executable)");
        return;
    }
    Game::SettingsFile keysFile = Game::SettingsFile::defaults();
    Game::Bindings keys(&keysFile.bindingsA);
    check(keys.key(Game::Bindings::Forward, 0) == 0x3fb && keys.key(Game::Bindings::Forward, 1) == 'W' &&
              keys.key(Game::Bindings::Use, 1) == ' ' && keys.key(Game::Bindings::Action1, 1) == Game::Bindings::kMouse &&
              keys.key(Game::Bindings::Action2, 1) == Game::Bindings::kMouse + 2 && keys.key(Game::Bindings::Sprint, 1) == 0x418,
          "the default keys: arrows / W S A D, Space, the mouse buttons, Shift");
    keys.assign(Game::Bindings::Forward, 'W'); // already on it
    check(keys.key(Game::Bindings::Forward, 0) == 0x3fb && keys.key(Game::Bindings::Forward, 1) == 'W', "a key already bound changes nothing");
    keys.assign(Game::Bindings::Forward, 'Q'); // Q was PEEK LEFT's
    check(keys.key(Game::Bindings::Forward, 0) == 'W' && keys.key(Game::Bindings::Forward, 1) == 'Q' &&
              !keys.bound(Game::Bindings::PeekLeft, 'Q'),
          "rebinding: column 1 moves to 0, the new key to 1, and it leaves its old slot");
    keys.assign(Game::Bindings::Sprint, 'V');
    check(keys.key(25, 1) == 'V' && keys.key(25, 0) == 0x418, "SPRINT's twin slot 25 follows it");
}

void testLevelScore()
{
    std::puts("scene score and rating (exe 0x5b6030 / 0x5d2140)");
    Game::LevelScore s;
    s.setHunters(7, 7); // Jury Turf: SetNumberOfKillableHuntersInLevel(7,7)
    check(s.target() == 42.0f, "7 executable hunters: 42 points");
    s.execution(4); // gruesome
    s.execution(2); // hasty
    s.kill();
    s.headBurst();
    check(s.stylePoints == 6 + 4 + 2 + 1 && s.executions == 2 && s.kills == 1, "+6 gruesome, +4 hasty, +2 kill, +1 head");
    check(Game::styleStars(13, 42.0f, true) == 1 && Game::styleStars(13, 42.0f, false) == 0, "13 / 42: hardcore 1 star, fetish 0");
    check(Game::styleStars(42, 42.0f, true) == 4 && Game::styleStars(42, 42.0f, false) == 3, "the full target: 4 / 3 stars");
    check(Game::styleStars(0, 0.0f, false) == 3, "no target: full marks (0x5d2140)");
    check(Game::parSeconds(0) == 600 && Game::parSeconds(15) == 420 && Game::parSeconds(30) == 0, "par times (0x7c7d88)");
    s.timeMs = 599 * 1000;
    Game::LevelResults r = Game::levelResults(s, 0, true);
    check(r.timeBonus && r.stars == 2, "under par: one star more");
    s.timeMs = 600 * 1000;
    r = Game::levelResults(s, 0, true);
    check(!r.timeBonus && r.stars == 1, "at par: no time bonus");
    r = Game::levelResults(s, 19, true);
    check(r.score.kills == 0 && r.score.executions == 0, "the last scene counts one kill and two executions less");
}

void testProgress()
{
    std::puts("scene progress (exe 0x5d20b0 / 0x5d6d60 / 0x5d6af0 / 0x5d2320)");
    Game::GameProgress p;
    check(p.playable(0) && !p.playable(1) && p.groupOpen(0) && !p.groupOpen(1) && !p.groupOpen(5),
          "new game: scene 1 only, group 1-4 only");
    p.complete(0, 2);
    check(p.playable(1) && !p.playable(2), "scene 1 done opens scene 2");
    p.complete(0, 1);
    check(p.scene(0).best == 2, "a worse run keeps the best rating");
    for (int i = 1; i < 4; ++i)
        p.complete(i, 3);
    check(p.groupOpen(1) && !p.groupOpen(2), "scenes 1..4 done open group 5-8");
    check(!p.bonusOpen(20), "bonus 1 needs 3 stars on scenes 1..5");
    p.complete(0, 3);
    p.complete(4, 4);
    check(p.bonusOpen(20) && p.groupOpen(5) && p.playable(20) && !p.playable(21), "3+ stars on 1..5 open bonus 1");

    std::vector<std::uint8_t> state(0x10e0, 0);
    state[0x60 + 0xb0 * 2] = 1;
    state[0x60 + 0xb0 * 2 + 4] = 5;
    Game::GameProgress q;
    q.readStateBlock(state.data(), state.size());
    check(q.scene(2).done && q.scene(2).best == 5 && !q.scene(1).done, "a save's state block: record 3 at 0x60 + 0xb0 * 2");

    // PLAY (0x5d6900) and a loaded save (0x606b00).
    Game::GameProgress fresh;
    check(fresh.continueScene() == -1, "before any game PLAY makes a new game");
    p.setLast(2, Game::GameProgress::kSceneDone);
    check(p.continueScene() == 5, "PLAY: the last open scene (6) when it is later than the last played");
    p.setLast(7, Game::GameProgress::kSceneDone);
    check(p.continueScene() == 7 && p.resumeScene() == 8, "a finished scene's save starts the next one");
    p.setLast(7, 7);
    check(p.resumeScene() == 7, "a save point's checkpoint starts its own scene");
    fresh.newGame();
    check(fresh.resumeScene() == 0 && fresh.sceneState() == Game::GameProgress::kNewGame && !fresh.scene(0).done,
          "a new game: scene 1, state -2");
    check(p.totalStars() == 3 + 3 + 3 + 3 + 4, "GAME COMPLETED's total (0x5daac0)");

    // A slot written and read back.
    const std::string dir = (std::filesystem::temp_directory_path() / "reelhunt_slots_test").string();
    std::filesystem::remove_all(dir);
    Game::SaveSlots slots;
    slots.setDirs(dir, "");
    slots.refresh();
    check(slots.slot(3).status == Game::SaveSlotInfo::Status::Empty, "an empty slot");
    Game::GameSettings hard;
    hard.difficulty = 1;
    p.setLast(4, Game::GameProgress::kSceneDone);
    // The inventory scene 4 was won with, carried into scene 5 (0x5d24a0 / 0x5d2590).
    std::array<Game::SaveInventorySlot, 4> won{};
    won[0] = {"Glock_(CT)", 23};
    won[2] = {"Bag_(CT)", -1};
    p.storeInventory(3, won);
    check(p.carriedInto(4) && (*p.carriedInto(4))[0].record == "Glock_(CT)" && !p.carriedInto(5) && !p.carriedInto(3),
          "scene 5 starts with scene 4's inventory; scenes 4 and 6 carry none");
    std::array<uint8, 0x130> keys{};
    keys[0] = 0x2a;
    keys[0x12f] = 0x7e;
    slots.setBindings(&keys);
    const bool wrote = slots.save(3, p, hard, "3)SCENE 5-(COMPLETE)");
    Game::GameProgress r;
    Game::GameSettings back;
    const bool read = slots.load(3, r, back);
    check(wrote && read && slots.slot(3).status == Game::SaveSlotInfo::Status::Saved && slots.slot(3).scene == 4 &&
              slots.slot(3).difficulty == 1,
          "a slot written: scene, state and difficulty listed");
    check(r.scene(4).best == 4 && r.scene(0).best == 3 && r.lastScene() == 4 && r.resumeScene() == 5 && back.difficulty == 1,
          "the slot loads the records, the scene and the difficulty");
    check(r.carriedInto(4) && (*r.carriedInto(4))[0].rounds == 23 && (*r.carriedInto(4))[2].record == "Bag_(CT)" &&
              (*r.carriedInto(4))[1].record.empty(),
          "the carried inventory in the scene record (+8 + 0x1c * slot)");
    Game::SaveFile written;
    check(Game::loadSaveFile(slots.slot(3).path, written) && written.buffer7.size() == 0x130 && written.buffer7[0] == 0x2a &&
              written.buffer7[0x12f] == 0x7e,
          "the key bindings in buffer 7 (Settings.dat's first 0x130 bytes)");
    std::filesystem::remove_all(dir);
}

void testShadowCascades()
{
    std::puts("Shadow cascades");
    Renderer::CascadeCamera cam;
    cam.position = {3.0f, 2.0f, 5.0f};
    cam.view = glm::lookAt(cam.position, cam.position + glm::vec3(0.3f, -0.2f, -1.0f), glm::vec3(0, 1, 0));
    cam.fovYDegrees = 50.0f;
    cam.aspect = 4.0f / 3.0f;
    const float splits[3] = {10.0f, 35.0f, 120.0f};
    const glm::vec3 sun = glm::normalize(glm::vec3(-0.2f, -1.0f, -0.4f));
    const Renderer::CascadeFit fit = Renderer::fitShadowCascades(cam, 0.1f, splits, 3, sun, 2048);
    check(fit.count == 3u, "three cascades fitted");
    check(fit.cascades[0].texelWorld < fit.cascades[1].texelWorld && fit.cascades[1].texelWorld < fit.cascades[2].texelWorld,
          "near cascades are sharper than far ones");

    // Every point of a slice lands inside its cascade's map and depth range.
    const glm::mat4 toWorld = glm::inverse(cam.view);
    const float tanY = std::tan(glm::radians(25.0f)), tanX = tanY * cam.aspect;
    bool inside = true;
    float lo = 0.1f;
    for (uint32_t i = 0; i < 3; ++i)
    {
        for (float d : {lo, splits[i]})
            for (float sx : {-1.0f, 1.0f})
                for (float sy : {-1.0f, 1.0f})
                {
                    const glm::vec4 w = toWorld * glm::vec4(sx * d * tanX, sy * d * tanY, -d, 1.0f);
                    const glm::vec4 c = fit.cascades[i].viewProj * w;
                    inside = inside && std::fabs(c.x / c.w) <= 1.0f && std::fabs(c.y / c.w) <= 1.0f && c.z / c.w >= 0.0f &&
                             c.z / c.w <= 1.0f;
                }
        lo = splits[i];
    }
    check(inside, "each cascade contains its whole view slice");

    // Moving the camera a fraction of a texel must not move the shadow texels (no shimmer): the
    // projection of a fixed world point shifts by whole texels only.
    Renderer::CascadeCamera moved = cam;
    moved.position += glm::vec3(0.013f, 0.0f, 0.007f);
    moved.view = glm::lookAt(moved.position, moved.position + glm::vec3(0.3f, -0.2f, -1.0f), glm::vec3(0, 1, 0));
    const Renderer::CascadeFit fit2 = Renderer::fitShadowCascades(moved, 0.1f, splits, 3, sun, 2048);
    const glm::vec4 probe(1.0f, 0.0f, -3.0f, 1.0f);
    const glm::vec4 p1 = fit.cascades[0].viewProj * probe, p2 = fit2.cascades[0].viewProj * probe;
    const float shiftTexels = (p2.x - p1.x) * 0.5f * 2048.0f;
    check(std::fabs(shiftTexels - std::round(shiftTexels)) < 1e-2f, "the cascade moves in whole texels");
}

void testPlatformPaths()
{
    // A game layout in mixed case, looked up the way the code and the data's text files write it.
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "reelhunt_platform_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root / "Levels" / "JURY_TURF" / "Pak");
    fs::create_directories(root / "AUDIO");
    fs::create_directories(root / "pictures");
    { FILE* f = std::fopen((root / "Levels" / "JURY_TURF" / "Pak" / "ModelsPC.TXD").string().c_str(), "wb"); if (f) std::fclose(f); }

    const std::string found = Engine::Platform::resolvePath((root / "levels/jury_turf/pak/modelspc.txd").string());
    check(fs::exists(found), "resolvePath finds a file whose every component differs in case");
    check(Engine::Platform::resolvePath((root / "levels/jury_turf/missing.txd").string()).ends_with("missing.txd"),
          "a missing component keeps the rest of the path as written");
    check(Engine::Platform::isGameRoot(root), "isGameRoot accepts levels/audio/pictures in any case");
    check(!Engine::Platform::isGameRoot(root / "AUDIO"), "isGameRoot rejects a folder without the game layout");
    check(!Engine::Platform::executableDir().empty(), "executableDir is known");
    fs::remove_all(root, ec);
}

int main()
{
    // The save-slot names and default keys come from the game's executable; without one, stand-ins
    // (the tests only need names that differ and keys that are not all zero).
    if (!TestPaths::exeTables())
    {
        Game::ExeTables stand;
        for (std::size_t i = 0; i < stand.saveNames.size(); ++i)
            stand.saveNames[i] = "SLOT" + std::to_string(i) + ".SAV";
        for (std::size_t i = 0; i < stand.defaultBindings.size(); ++i)
            stand.defaultBindings[i] = static_cast<uint8>(i * 7 + 1);
        Game::setGameExe(stand);
    }
    testEngineError();
    testEntitiesAndScene();
    testTransform();
    testPhysics();
    testByteReader();
    testKeymap();
    testScriptBranch();
    testIfpLookup();
    testSaveClassData();
    testTextureSet();
    testLevelFrame();
    testTriangleField();
    testShadowCascades();
    testVideoModes();
    testSettingsFile();
    testLevelScore();
    testProgress();
    testPlatformPaths();
    std::printf("%s (%d failure%s)\n", g_failures == 0 ? "PASS" : "FAIL", g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? 0 : 1;
}
