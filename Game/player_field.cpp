// Game/player_field.cpp - build the level collision field + drop/resolve the player capsule.
#include "Game/player_field.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

#include "Assets/col.h"
#include "Game/doors.h"
#include "Game/material_types.h"
#include "Physics/collider.h"
#include "Physics/physics.h"
#include "Physics/rigidbody.h"
#include "Renderer/material.h"
#include "Renderer/mesh.h"

namespace Game {

namespace {

// A placement's internalName is the .col entry name plus a 1-3 digit instance suffix (entry
// "CJ_AIRCON" -> placements "CJ_AIRCON01".."05"). Match exactly, then with the digits stripped.
const Assets::ColEntry* matchEntry(const Assets::ColData& col, const std::string& name)
{
    if (const auto* e = col.find(name))
        return e;
    for (std::size_t k = 1; k <= 3 && k < name.size(); ++k)
    {
        if (!std::all_of(name.end() - static_cast<std::ptrdiff_t>(k), name.end(),
                         [](char c) { return c >= '0' && c <= '9'; }))
            break;
        if (const auto* e = col.find(name.substr(0, name.size() - k)))
            return e;
    }
    return nullptr;
}

std::unique_ptr<Renderer::Mesh> makeMarkerCube(const Math::Vector3& c, float32 h)
{
    auto mk = std::make_unique<Renderer::Mesh>();
    const Math::Vector3 corners[8] = {
        c + Math::Vector3{-h, -h, -h}, c + Math::Vector3{h, -h, -h}, c + Math::Vector3{h, h, -h}, c + Math::Vector3{-h, h, -h},
        c + Math::Vector3{-h, -h, h},  c + Math::Vector3{h, -h, h},  c + Math::Vector3{h, h, h},  c + Math::Vector3{-h, h, h}};
    const uint32 idx[36] = {0, 3, 2, 0, 2, 1, 4, 5, 6, 4, 6, 7, 0, 4, 7, 0, 7, 3,
                            1, 2, 6, 1, 6, 5, 0, 1, 5, 0, 5, 4, 3, 7, 6, 3, 6, 2};
    for (const auto& p : corners)
        mk->addVertex(p, (p - c).normalized(), Math::Vector2{0.0f, 0.0f});
    for (uint32 i = 0; i < 36u; i += 3u)
        mk->addTriangle(idx[i], idx[i + 1], idx[i + 2]);
    mk->computeBoundingBox();
    Renderer::Material m = Renderer::Material::defaultMaterial();
    m.setDiffuse(Math::Vector4{1.0f, 0.15f, 0.15f, 1.0f});
    mk->setMaterial(m);
    return mk;
}

} // namespace

void PlayerField::buildField(const std::string& levelDir, const LevelContent& level)
{
    std::vector<Physics::TriangleField::Triangle> tris = level.collisionTriangles();
    const std::size_t worldCount = tris.size();
    worldLight_ = level.collisionLight();
    worldSurfaces_ = level.collisionSurfaces();
    worldMaterials_ = level.collisionMaterials();

    auto col = Assets::Col::load(levelDir + "collisions.col");
    if (!col)
        std::cerr << "collision: no prop field (" << col.error().message << ")\n";

    const LevelFrame& frame = level.frame();
    std::size_t matched = 0;
    static const std::vector<Assets::InstPlacement> kNone;
    for (const auto& p : col ? level.placements() : kNone)
    {
        if (p.entityClass == "Door_Inst" || LevelContent::isSlideDoor(p.record))
            continue; // doors swing or slide: Doors collides with them where they are
        // The record names the model (CJ_WRECK03 is a CJ_WRECK2); the instance name is a fallback.
        const auto* e = col->find(p.record);
        if (!e)
            e = matchEntry(*col, p.internalName);
        if (!e || (e->spheres.empty() && e->faces.empty()))
            continue;
        // Exactly the transform the prop's model gets (LevelContent::placeProps), so the shapes
        // sit on the visible geometry.
        const Math::Vector3 origin = frame.placementOrigin(p.position);
        const Math::Matrix4x4 rotation = LevelFrame::placementRotation(p.rotation);
        PropShapes& shapes = propShapes_[p.index];
        shapes.firstTriangle = static_cast<uint32>(tris.size());
        shapes.firstSphere = colSpheres_.size();
        shapes.spheres = e->spheres.size();
        for (const auto& s : e->spheres)
            colSpheres_.push_back({frame.placedPoint(rotation, origin, s.center), s.radius * frame.scale});
        const auto vertex = [&](int32 i) { return frame.placedPoint(rotation, origin, e->verticals[static_cast<std::size_t>(i)]); };
        const auto valid = [&](int32 i) { return i >= 0 && static_cast<std::size_t>(i) < e->verticals.size(); };
        if (LevelContent::isMover(p.record))
        {
            // A mover keeps its own field, collided at its offset (setMoverOffset).
            std::vector<Physics::TriangleField::Triangle> own;
            for (const auto& f : e->faces)
                if (valid(f.i0) && valid(f.i1) && valid(f.i2))
                    own.push_back({vertex(f.i0), vertex(f.i1), vertex(f.i2)});
            colSpheres_.resize(shapes.firstSphere); // (its spheres are not kept: ours)
            propShapes_.erase(p.index);
            movers_[p.index].tris.build(std::move(own));
            continue;
        }
        for (const auto& f : e->faces)
            if (valid(f.i0) && valid(f.i1) && valid(f.i2))
                tris.push_back({vertex(f.i0), vertex(f.i1), vertex(f.i2)});
        shapes.triangles = static_cast<uint32>(tris.size()) - shapes.firstTriangle;
        ++matched;
    }
    const std::size_t propCount = tris.size() - worldCount;
    worldTris_.build(std::move(tris));

    // The player's shape: the lowest body sphere and the leg line.
    if (const auto* pe = col ? col->find("player") : nullptr)
    {
        const Assets::ColSphere* body = nullptr;
        for (const auto& s : pe->spheres)
            if (!body || s.center.y < body->center.y)
                body = &s;
        if (body && body->radius > 0.1f)
        {
            playerRadius_ = body->radius * frame.scale;
            bodyHeight_ = body->center.y * frame.scale;
        }
        for (const auto& l : pe->lines)
            legLength_ = std::max(l.a.y, l.b.y) * frame.scale;
    }

    std::cerr << "collision: world triangles=" << worldCount << " prop triangles=" << propCount
              << " prop spheres=" << colSpheres_.size() << " matchedPlacements=" << matched
              << " player body r=" << playerRadius_ << " at " << bodyHeight_ << " leg=" << legLength_ << "\n";
}

void PlayerField::setMoverOffset(int32 placement, const Math::Vector3& offset)
{
    if (const auto it = movers_.find(placement); it != movers_.end())
        it->second.offset = offset;
}

bool PlayerField::makeDynamic(int32 placement)
{
    const auto it = propShapes_.find(placement);
    if (it == propShapes_.end())
        return false;
    std::vector<Physics::TriangleField::Triangle> own = worldTris_.sourceTriangles(it->second.firstTriangle, it->second.triangles);
    removeProp(placement);
    removedProps_.erase(placement); // (its own shape from now on)
    if (own.empty())
        return false;
    MoverShape& m = movers_[placement];
    m.tris.build(std::move(own));
    m.rotated = true;
    return true;
}

std::vector<Physics::TriangleField::Triangle> PlayerField::propTriangles(int32 placement) const
{
    const auto it = propShapes_.find(placement);
    if (it == propShapes_.end())
        return {};
    return worldTris_.sourceTriangles(it->second.firstTriangle, it->second.triangles);
}

void PlayerField::setDynamicTransform(int32 placement, const Math::Matrix4x4& transform)
{
    if (const auto it = movers_.find(placement); it != movers_.end())
    {
        it->second.transform = transform;
        it->second.inverse = transform.inverse();
    }
}

void PlayerField::removeProp(int32 placement)
{
    const auto it = propShapes_.find(placement);
    if (it == propShapes_.end())
        return;
    const PropShapes gone = it->second;
    propShapes_.erase(it);
    worldTris_.remove(gone.firstTriangle, gone.triangles);
    removedProps_[placement] = {gone, std::vector<WorldSphere>(colSpheres_.begin() + static_cast<std::ptrdiff_t>(gone.firstSphere),
                                                               colSpheres_.begin() + static_cast<std::ptrdiff_t>(gone.firstSphere + gone.spheres))};
    colSpheres_.erase(colSpheres_.begin() + static_cast<std::ptrdiff_t>(gone.firstSphere),
                      colSpheres_.begin() + static_cast<std::ptrdiff_t>(gone.firstSphere + gone.spheres));
    for (auto& [index, shapes] : propShapes_)
        if (shapes.firstSphere > gone.firstSphere)
            shapes.firstSphere -= gone.spheres;
}

void PlayerField::restoreProp(int32 placement)
{
    const auto it = removedProps_.find(placement);
    if (it == removedProps_.end())
        return;
    PropShapes back = it->second.first;
    worldTris_.restore(back.firstTriangle, back.triangles);
    back.firstSphere = colSpheres_.size();
    colSpheres_.insert(colSpheres_.end(), it->second.second.begin(), it->second.second.end());
    propShapes_[placement] = back;
    removedProps_.erase(it);
}

Math::Vector3 PlayerField::spawnPoint(const LevelContent& level)
{
    // The level's own player start; without one, the middle of the world.
    Math::Vector3 start{0.0f, 0.0f, 0.0f};
    bool fromInst = false;
    for (const auto& p : level.placements())
        if (p.entityClass == "Player_Inst")
        {
            start = level.frame().placementOrigin(p.position);
            fromInst = true;
            // Heading about the level's up axis (+Z) from the placement quaternion (x, y, z, w). A
            // level heading keeps its sign in the scene frame (see LevelFrame::placementRotation);
            // an unrotated character faces level -Y (LevelFrame::characterYaw).
            spawnYaw_ = LevelFrame::characterYaw(p.rotation);
            break;
        }

    // Land on the highest surface just above the start point (the .inst position is near the
    // feet), so a roof or overhang above the start is not mistaken for the ground.
    const float32 searchFrom = fromInst ? start.y + 1.0f : 1e30f;
    Math::Vector3 spawn{start.x, 12.0f, start.z};
    if (const auto ground = worldTris_.groundBelow(start.x, start.z, searchFrom))
        spawn.y = *ground + bodyHeight_ + 0.05f;
    else if (fromInst)
        spawn.y = start.y + bodyHeight_ + 0.5f;
    std::cerr << "collision: spawn " << (fromInst ? "Player_Inst" : "world centre") << " at ("
              << spawn.x << ", " << spawn.y << ", " << spawn.z << ") heading " << spawnYaw_ << " rad\n";
    return spawn;
}

void PlayerField::load(const std::string& levelDir, const LevelContent& level,
                       Physics::PhysicsEngine& physics, Renderer::Scene& scene)
{
    buildField(levelDir, level);

    auto body = std::make_unique<Physics::RigidBody>();
    body->setMass(8.0f);
    body->setCollider(Physics::Collider::makeSphere(Math::Vector3{0.0f, 0.0f, 0.0f}, playerRadius_));

    const Math::Vector3 spawn = spawnPoint(level);
    body->setPosition(spawn);
    playerBody_ = body.get(); // the physics engine owns it
    physics.addBody(std::move(body));

    // Pre-simulate the drop to rest.
    int firstContactFrame = -1;
    bool grounded = false;
    for (int i = 0; i < 900; ++i)
    {
        physics.step(1.0f / 60.0f);
        grounded = resolve(playerBody_);
        if (grounded && firstContactFrame < 0)
            firstContactFrame = i;
        if (grounded && playerBody_->velocity().length() < 1e-3f)
            break;
    }
    std::cerr << "collision: player startY=" << spawn.y << " restY=" << playerBody_->position().y
              << " firstContactFrame=" << firstContactFrame << " grounded=" << (grounded ? 1 : 0) << "\n";

    auto marker = makeMarkerCube(playerBody_->position(), std::max(playerRadius_ * 1.5f, 0.4f));
    marker->setVisible(false); // debug marker, toggled with V
    marker_ = marker.get();    // the scene keeps it alive
    scene.addEntity(std::move(marker));
}

FootSurface PlayerField::surfaceBelow(const Math::Vector3& p) const
{
    const auto hit = worldTris_.groundHit(p.x, p.z, p.y + 0.5f);
    if (!hit || p.y - hit->height > 1.0f || hit->triangle >= worldSurfaces_.size())
        return FootSurface::Concrete;
    return static_cast<FootSurface>(worldSurfaces_[hit->triangle]);
}

std::optional<Math::Vector3> PlayerField::lightBelow(const Math::Vector3& p) const
{
    const auto hit = worldTris_.groundHit(p.x, p.z, p.y);
    if (!hit || p.y - hit->height > 4.0f || hit->triangle >= worldLight_.size())
        return std::nullopt;
    const auto& c = worldLight_[hit->triangle];
    const float32 a = 1.0f - hit->u - hit->w;
    const Math::Vector4 l = c[0] * a + c[1] * hit->u + c[2] * hit->w;
    return Math::Vector3{l.x, l.y, l.z};
}

Math::Vector3 PlayerField::clipCamera(const Math::Vector3& pivot, const Math::Vector3& desired, float32 margin) const
{
    const auto hit = worldTris_.raycast(pivot, desired);
    if (!hit)
        return desired;
    const Math::Vector3 d = desired - pivot;
    const float32 len = d.length();
    if (len < 1e-4f)
        return desired;
    const float32 keep = std::max(0.0f, *hit * len - margin);
    return pivot + d * (keep / len);
}

void PlayerField::setMarkerVisible(bool v) const noexcept
{
    if (marker_)
        marker_->setVisible(v);
}

bool PlayerField::resolve(Physics::RigidBody* b) const
{
    Math::Vector3 P = b->position();
    Math::Vector3 V = b->velocity();
    const bool grounded = resolveCharacter(P, V);
    b->setPosition(P);
    b->setVelocity(V);
    return grounded;
}

bool PlayerField::lineOfSight(const Math::Vector3& a, const Math::Vector3& b) const
{
    return !worldTris_.raycast(a, b).has_value();
}

float32 PlayerField::soundThrough(const Math::Vector3& a, const Math::Vector3& b) const
{
    // Every surface on the line, one hit after another (ours: stepping 1 mm past each).
    const Math::Vector3 d = b - a;
    const float32 length = d.length();
    if (length < 1e-4f)
        return 1.0f;
    float32 through = 1.0f, at = 0.0f;
    for (int32 i = 0; i < 64 && through > 0.0f; ++i)
    {
        const Math::Vector3 from = a + d * (at / length);
        const auto hit = worldTris_.raycastHit(from, b);
        if (!hit)
            break;
        if (hit->triangle < worldMaterials_.size()) // (the props' triangles follow the world's)
            through *= MaterialTypes::soundThrough(worldMaterials_[hit->triangle]);
        at += hit->fraction * (length - at) + 1e-3f;
        if (at >= length)
            break;
    }
    return through;
}

std::optional<float32> PlayerField::soundHit(const Math::Vector3& a, const Math::Vector3& b) const
{
    const Math::Vector3 d = b - a;
    const float32 length = d.length();
    if (length < 1e-4f)
        return std::nullopt;
    float32 at = 0.0f;
    for (int32 i = 0; i < 64; ++i)
    {
        const Math::Vector3 from = a + d * (at / length);
        const auto hit = worldTris_.raycastHit(from, b);
        if (!hit)
            break;
        at += hit->fraction * (length - at);
        if (hit->triangle < worldMaterials_.size() && MaterialTypes::stopsSound(worldMaterials_[hit->triangle]))
            return at;
        at += 1e-3f;
        if (at >= length)
            break;
    }
    return std::nullopt;
}

bool PlayerField::worldClear(const Math::Vector3& a, const Math::Vector3& b) const
{
    const Math::Vector3 d = b - a;
    const float32 length = d.length();
    if (length < 1e-4f)
        return true;
    float32 at = 0.0f;
    for (int32 i = 0; i < 64; ++i)
    {
        const auto hit = worldTris_.raycastHit(a + d * (at / length), b);
        if (!hit)
            return true;
        if (hit->triangle < worldMaterials_.size()) // (the props' triangles follow the world's)
            return false;
        at += hit->fraction * (length - at) + 1e-3f;
        if (at >= length)
            return true;
    }
    return true;
}

std::optional<PlayerField::ShotHit> PlayerField::shotHit(const Math::Vector3& a, const Math::Vector3& b) const
{
    std::optional<ShotHit> best;
    if (const auto hit = worldTris_.raycastHit(a, b))
        best = ShotHit{hit->fraction, hit->normal, hit->triangle < worldMaterials_.size() ? worldMaterials_[hit->triangle] : uint8{0}};
    // The moving and woken props' own triangles (in their frame), and the props' spheres, as the
    // bodies collide with them.
    for (const auto& [index, m] : movers_)
    {
        const Math::Vector3 la = m.rotated ? m.inverse.multiplyPoint(a) : a - m.offset;
        const Math::Vector3 lb = m.rotated ? m.inverse.multiplyPoint(b) : b - m.offset;
        if (const auto hit = m.tris.raycastHit(la, lb); hit && (!best || hit->fraction < best->fraction))
        {
            Math::Vector3 n = hit->normal;
            if (m.rotated)
            {
                const Math::Vector4 w = m.transform.multiply(Math::Vector4{n.x, n.y, n.z, 0.0f});
                n = Math::Vector3{w.x, w.y, w.z}.normalized();
            }
            best = ShotHit{hit->fraction, n, uint8{0}};
        }
    }
    const Math::Vector3 d = b - a;
    const float32 dd = d.dot(d);
    if (dd > 1e-12f)
        for (const auto& s : colSpheres_)
        {
            // |a + d t - c| = r, the nearer root in 0..1.
            const Math::Vector3 f = a - s.center;
            const float32 bq = f.dot(d), c = f.dot(f) - s.radius * s.radius;
            const float32 disc = bq * bq - dd * c;
            if (disc < 0.0f || c < 0.0f) // (starting inside it: not a hit)
                continue;
            const float32 t = (-bq - std::sqrt(disc)) / dd;
            if (t < 0.0f || t > 1.0f || (best && t >= best->fraction))
                continue;
            best = ShotHit{t, (a + d * t - s.center).normalized(), uint8{0}};
        }
    return best;
}

std::optional<float32> PlayerField::raycast(const Math::Vector3& a, const Math::Vector3& b) const
{
    return worldTris_.raycast(a, b);
}

bool PlayerField::resolveCharacter(Math::Vector3& P, Math::Vector3& V) const
{
    bool grounded = worldTris_.resolveSphere(P, playerRadius_, V);
    for (const auto& [index, m] : movers_)
    {
        if (m.rotated)
        {
            // A physics prop: in its rest frame.
            Math::Vector3 local = m.inverse.multiplyPoint(P);
            const Math::Vector4 vl = m.inverse.multiply(Math::Vector4{V.x, V.y, V.z, 0.0f});
            Math::Vector3 lv{vl.x, vl.y, vl.z};
            grounded = m.tris.resolveSphere(local, playerRadius_, lv) || grounded;
            P = m.transform.multiplyPoint(local);
            const Math::Vector4 vw = m.transform.multiply(Math::Vector4{lv.x, lv.y, lv.z, 0.0f});
            V = {vw.x, vw.y, vw.z};
            continue;
        }
        Math::Vector3 local = P - m.offset;
        grounded = m.tris.resolveSphere(local, playerRadius_, V) || grounded;
        P = local + m.offset;
    }
    if (doors_)
        doors_->collide(P, playerRadius_, V);
    for (const auto& s : colSpheres_)
    {
        const Math::Vector3 d = P - s.center;
        const float32 dist = d.length();
        const float32 rr = playerRadius_ + s.radius;
        if (dist < rr && dist > 1e-6f)
        {
            const Math::Vector3 n = d * (1.0f / dist);
            P += n * (rr - dist);
            if (n.y > 0.5f)
            {
                V.y = 0.0f;
                grounded = true;
            }
            else if (const float32 vn = V.dot(n); vn < 0.0f)
            {
                V -= n * vn;
            }
        }
    }
    // The leg: stand on the highest floor within its reach above the feet, or just below them.
    if (V.y <= 0.5f)
    {
        const float32 feet = P.y - bodyHeight_;
        std::optional<float32> ground = worldTris_.groundBelow(P.x, P.z, feet + legLength_);
        for (const auto& [index, m] : movers_)
        {
            if (m.rotated)
            {
                // On a physics prop: its floor below him in its rest frame (near enough upright).
                const Math::Vector3 lf = m.inverse.multiplyPoint(Math::Vector3{P.x, feet + legLength_, P.z});
                if (const auto g = m.tris.groundBelow(lf.x, lf.z, lf.y))
                {
                    const float32 y = m.transform.multiplyPoint(Math::Vector3{lf.x, *g, lf.z}).y;
                    if (!ground || y > *ground)
                        ground = y;
                }
                continue;
            }
            if (const auto g = m.tris.groundBelow(P.x - m.offset.x, P.z - m.offset.z, feet + legLength_ - m.offset.y))
                if (!ground || *g + m.offset.y > *ground)
                    ground = *g + m.offset.y; // standing on a mover (a lift carries him)
        }
        if (ground && *ground >= feet - kStepDown)
        {
            P.y = *ground + bodyHeight_;
            if (V.y < 0.0f)
                V.y = 0.0f;
            grounded = true;
        }
    }
    return grounded;
}

} // namespace Game
