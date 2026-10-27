// Game/physics_props.cpp - props that fall, slide and crush. See physics_props.h.
#include "Game/physics_props.h"

#include <algorithm>
#include <cmath>

namespace Game {

namespace {

constexpr float32 kTick = 1.0f / 30.0f;
constexpr int32 kPasses = 4;           // contact passes a tick (ours)
constexpr float32 kGravity = 25.0f;     // 0x724890 (x the time step)
constexpr float32 kMaxFall = 30.0f;     // 0x724a0c
constexpr float32 kCrushMomentum = 200.0f; // 0x724a3c
constexpr float32 kCrushFromAbove = 1000.0f;
constexpr float32 kAboveNormal = 0.75f; // 0x7248ac
constexpr float32 kFallAcross = -0.2f;  // 0x7248e8

Math::Matrix3x3 mul(const Math::Matrix3x3& a, const Math::Matrix3x3& b)
{
    Math::Matrix3x3 r;
    const float32 A[3][3] = {{a.m00, a.m01, a.m02}, {a.m10, a.m11, a.m12}, {a.m20, a.m21, a.m22}};
    const float32 B[3][3] = {{b.m00, b.m01, b.m02}, {b.m10, b.m11, b.m12}, {b.m20, b.m21, b.m22}};
    float32 C[3][3] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k)
                C[i][j] += A[i][k] * B[k][j];
    r.m00 = C[0][0]; r.m01 = C[0][1]; r.m02 = C[0][2];
    r.m10 = C[1][0]; r.m11 = C[1][1]; r.m12 = C[1][2];
    r.m20 = C[2][0]; r.m21 = C[2][1]; r.m22 = C[2][2];
    return r;
}

Math::Vector3 scaled(const Math::Vector3& a, const Math::Vector3& b)
{
    return {a.x * b.x, a.y * b.y, a.z * b.z};
}

} // namespace

int32 PhysicsProps::impactSound(int32 material, uint32 random)
{
    // 0x7ab9b0: by material, the first and last sound id (14099: none).
    static constexpr int32 kRanges[][2] = {
        {14099, 14099}, {3, 3}, {5, 6}, {38, 39}, {18, 20}, {21, 22}, {23, 24}, {25, 25}, {26, 28}, {41, 42},
        {43, 45}, {46, 47}, {48, 48}, {14099, 14099}, {49, 50}, {55, 55}, {14099, 14099}, {48, 48}, {56, 57},
        {3, 3}, {16, 17}, {38, 39}, {59, 60}, {3, 3}, {61, 63}, {14099, 14099}, {64, 66}, {64, 66}, {14099, 14099},
        {33, 33}, {158, 158}, {159, 159}, {160, 160}, {5, 6}, {7, 9}, {10, 11}, {12, 13}, {14, 15}, {16, 17},
        {33, 33}, {34, 35}, {36, 37}, {38, 39}, {51, 52}, {53, 54}, {64, 66}, {67, 67}, {58, 58}, {4, 4},
        {29, 32}, {5, 6}, {34, 35}, {3, 3}, {230, 234}, {59, 60}, {58, 58}, {74, 76}, {34, 35}, {61, 63},
        {64, 66}, {48, 48}, {26, 28}, {34, 35}};
    if (material < 0 || material >= static_cast<int32>(std::size(kRanges)))
        return -1;
    const int32 lo = kRanges[material][0], hi = kRanges[material][1];
    if (lo == 14099)
        return -1;
    return lo + static_cast<int32>(random % static_cast<uint32>(hi - lo + 1));
}

Math::Matrix3x3 PhysicsProps::Body::rotation() const
{
    return mul(turn.toMatrix(), rest);
}

int32 PhysicsProps::add(int32 id, const PhysicsType& type, bool iron, const Math::Vector3& origin, const Math::Matrix3x3& rest,
                        const Math::Vector3& boxMin, const Math::Vector3& boxMax)
{
    Body b;
    b.id = id;
    b.type = type;
    b.iron = iron;
    b.material = 0;
    b.origin0 = origin;
    b.rest = rest;
    b.boxCentre = (boxMin + boxMax) * 0.5f;
    b.half = (boxMax - boxMin) * 0.5f;
    b.half = {std::max(b.half.x, 0.05f), std::max(b.half.y, 0.05f), std::max(b.half.z, 0.05f)};
    // CENTRE_OF_MASS is from the pivot in the entity's axes (y up): the model's (x, -z, y).
    b.cm = b.boxCentre + Math::Vector3{type.centreOfMass.x, -type.centreOfMass.z, type.centreOfMass.y};
    b.x = origin + rest.multiply(b.cm);
    // 0x48d540: a cuboid's inertia m (b^2 + c^2) / 12 on each axis, over the full sizes; a
    // sphere's 4/3 m r^2 / 4 (0x7248e0 x (d / 2)^2).
    const float32 m = std::max(type.mass, 1e-3f);
    const Math::Vector3 d = b.half * 2.0f;
    Math::Vector3 inertia;
    if (type.cls == PhysicsType::RigidSphere)
    {
        const float32 r = d.x * 0.5f;
        inertia = Math::Vector3{1.0f, 1.0f, 1.0f} * (m * 1.3333334f * r * r);
    }
    else
        inertia = {m * (d.y * d.y + d.z * d.z) / 12.0f, m * (d.x * d.x + d.z * d.z) / 12.0f, m * (d.x * d.x + d.y * d.y) / 12.0f};
    b.invInertia = {1.0f / std::max(inertia.x, 1e-4f), 1.0f / std::max(inertia.y, 1e-4f), 1.0f / std::max(inertia.z, 1e-4f)};
    b.awake = type.turnedOn;
    bodies_.push_back(std::move(b));
    return static_cast<int32>(bodies_.size()) - 1;
}

int32 PhysicsProps::find(int32 id) const
{
    for (std::size_t i = 0; i < bodies_.size(); ++i)
        if (bodies_[i].id == id)
            return static_cast<int32>(i);
    return -1;
}

bool PhysicsProps::wake(int32 i)
{
    Body& b = body(i);
    b.still = 0.0f;
    if (b.awake || b.type.immovable)
        return false;
    b.awake = true;
    if (std::find(woken_.begin(), woken_.end(), i) != woken_.end())
        return false;
    woken_.push_back(i);
    return true;
}

Math::Vector3 PhysicsProps::origin(int32 i) const
{
    const Body& b = body(i);
    return b.x - b.rotation().multiply(b.cm);
}

Math::Matrix4x4 PhysicsProps::transform(int32 i) const
{
    const Body& b = body(i);
    const Math::Matrix3x3 delta = mul(b.rotation(), b.rest.transpose());
    return Math::Matrix4x4::translation(origin(i)) * delta.make4x4() *
           Math::Matrix4x4::translation(Math::Vector3{0.0f, 0.0f, 0.0f} - b.origin0);
}

Math::Vector3 PhysicsProps::worldInvInertia(const Body& b, const Math::Vector3& v) const
{
    const Math::Matrix3x3 R = b.rotation();
    return R.multiply(scaled(b.invInertia, R.transpose().multiply(v)));
}

float32 PhysicsProps::effectiveMass(const Body& b, const Math::Vector3& r, const Math::Vector3& n) const
{
    return 1.0f / std::max(b.type.mass, 1e-3f) + n.dot(worldInvInertia(b, r.cross(n)).cross(r));
}

void PhysicsProps::applyImpulse(Body& b, const Math::Vector3& r, const Math::Vector3& j) const
{
    b.v += j * (1.0f / std::max(b.type.mass, 1e-3f));
    b.w += worldInvInertia(b, r.cross(j));
}

std::vector<PhysicsProps::Crush> PhysicsProps::update(float32 dt, const World& world)
{
    // The characters' velocities from their moves since the last update.
    World w = world;
    for (Character& c : w.characters)
    {
        if (const auto it = lastFeet_.find(c.id); it != lastFeet_.end() && dt > 0.0f && c.velocity.dot(c.velocity) == 0.0f)
            c.velocity = (c.feet - it->second) * (1.0f / dt);
        lastFeet_[c.id] = c.feet;
    }
    std::vector<Crush> crushed;
    carry_ += dt;
    while (carry_ >= kTick)
    {
        carry_ -= kTick;
        time_ += kTick;
        // Move, then a few passes over the contacts (the floor's and each other's) so stacked
        // ones answer each other's pushes (ours; out of the surfaces on the first pass only),
        // then sleep and the characters.
        for (Body& b : bodies_)
            if (b.awake)
            {
                integrate(b, kTick);
                b.contacts = 0;
            }
        for (int32 pass = 0; pass < kPasses; ++pass)
        {
            for (std::size_t i = 0; i < bodies_.size(); ++i)
                for (std::size_t j = i + 1; j < bodies_.size(); ++j)
                    if (bodies_[i].awake || bodies_[j].awake)
                        collide(bodies_[i], bodies_[j], pass == 0);
            for (Body& b : bodies_)
                if (b.awake && !b.type.immovable)
                    b.contacts = std::max(b.contacts, solveWorld(b, kTick, w, time_, pass == 0));
        }
        for (Body& b : bodies_)
            if (b.awake)
                settle(b, kTick, w, time_, crushed);
    }
    return crushed;
}

float32 PhysicsProps::bump(const Character& ch) const
{
    float32 most = 0.0f;
    for (const Body& b : bodies_)
    {
        const Math::Matrix3x3 R = b.rotation(), Rt = R.transpose();
        for (int32 k = 0; k < 3; ++k)
        {
            const Math::Vector3 p =
                ch.feet + Math::Vector3{0.0f, ch.radius + (ch.height - 2.0f * ch.radius) * static_cast<float32>(k) * 0.5f, 0.0f};
            const Math::Vector3 local = Rt.multiply(p - b.x) + b.cm;
            const Math::Vector3 lo = b.boxCentre - b.half, hi = b.boxCentre + b.half;
            const Math::Vector3 clamped{std::clamp(local.x, lo.x, hi.x), std::clamp(local.y, lo.y, hi.y), std::clamp(local.z, lo.z, hi.z)};
            const Math::Vector3 closest = b.x + R.multiply(clamped - b.cm);
            const float32 d = (p - closest).length();
            if (d >= ch.radius || d < 1e-4f)
                continue;
            const Math::Vector3 n = (p - closest) * (1.0f / d); // out of the body, to him
            const Math::Vector3 r = closest - b.x;
            const float32 approach = (b.v + b.w.cross(r) - ch.velocity).dot(n);
            if (approach <= 0.0f)
                continue;
            const float32 k2 = (b.type.immovable ? 0.0f : effectiveMass(b, r, n)) + 1.0f / std::max(ch.mass, 1.0f);
            most = std::max(most, approach / k2);
        }
    }
    return most;
}

void PhysicsProps::collide(Body& a, Body& b, bool first)
{
    // Far apart: nothing (their boxes' bounding spheres).
    const float32 ra = a.half.length(), rb = b.half.length();
    const Math::Vector3 ca = a.x + a.rotation().multiply(a.boxCentre - a.cm), cb = b.x + b.rotation().multiply(b.boxCentre - b.cm);
    if ((ca - cb).length() > ra + rb)
        return;
    for (int32 pass = 0; pass < 2; ++pass)
    {
        Body& p = pass == 0 ? a : b; // its corners
        Body& q = pass == 0 ? b : a; // into this box
        const Math::Matrix3x3 Rp = p.rotation(), Rq = q.rotation(), Rqt = Rq.transpose();
        // The face of q towards p: the axis p's centre lies farthest along (by q's half sizes).
        const Math::Vector3 pc = Rqt.multiply(p.x + Rp.multiply(p.boxCentre - p.cm) - q.x) + q.cm - q.boxCentre;
        const float32 ax = std::fabs(pc.x) / q.half.x, ay = std::fabs(pc.y) / q.half.y, az = std::fabs(pc.z) / q.half.z;
        const int32 axis = ax >= ay && ax >= az ? 0 : (ay >= az ? 1 : 2);
        Math::Vector3 nl{0.0f, 0.0f, 0.0f};
        (axis == 0 ? nl.x : axis == 1 ? nl.y : nl.z) = (axis == 0 ? pc.x : axis == 1 ? pc.y : pc.z) < 0.0f ? -1.0f : 1.0f;
        const Math::Vector3 n = Rq.multiply(nl); // from q towards p
        // The contacts: p's face most against n, clipped to q's face towards p (the reference
        // face; Sutherland-Hodgman in q's frame), the points through it.
        int32 face = 0;
        float32 most = 2.0f;
        for (int32 f = 0; f < 6; ++f)
        {
            Math::Vector3 fl{0.0f, 0.0f, 0.0f};
            (f / 2 == 0 ? fl.x : f / 2 == 1 ? fl.y : fl.z) = (f & 1) ? -1.0f : 1.0f;
            const float32 d = Rp.multiply(fl).dot(n);
            if (d < most)
            {
                most = d;
                face = f;
            }
        }
        const int32 fa = face / 2, u = (fa + 1) % 3, v = (fa + 2) % 3;
        auto comp = [](const Math::Vector3& x, int32 i) { return i == 0 ? x.x : i == 1 ? x.y : x.z; };
        auto setc = [](Math::Vector3& x, int32 i, float32 val) { (i == 0 ? x.x : i == 1 ? x.y : x.z) = val; };
        std::vector<Math::Vector3> poly; // in q's box frame (about its centre)
        for (int32 c = 0; c < 4; ++c)
        {
            Math::Vector3 corner = p.boxCentre;
            setc(corner, fa, comp(p.boxCentre, fa) + ((face & 1) ? -comp(p.half, fa) : comp(p.half, fa)));
            const float32 su = (c == 0 || c == 3) ? -1.0f : 1.0f, sv = c < 2 ? -1.0f : 1.0f;
            setc(corner, u, comp(p.boxCentre, u) + su * comp(p.half, u));
            setc(corner, v, comp(p.boxCentre, v) + sv * comp(p.half, v));
            poly.push_back(Rqt.multiply(p.x + Rp.multiply(corner - p.cm) - q.x) + q.cm - q.boxCentre);
        }
        // Clip to q's reference face's sides (the two axes other than `axis`).
        for (int32 side = 0; side < 4 && !poly.empty(); ++side)
        {
            const int32 i = side < 2 ? (axis + 1) % 3 : (axis + 2) % 3;
            const float32 sign = (side & 1) ? -1.0f : 1.0f, lim = comp(q.half, i);
            std::vector<Math::Vector3> out;
            for (std::size_t k = 0; k < poly.size(); ++k)
            {
                const Math::Vector3& A = poly[k];
                const Math::Vector3& B = poly[(k + 1) % poly.size()];
                const float32 da = lim - sign * comp(A, i), db = lim - sign * comp(B, i);
                if (da >= 0.0f)
                    out.push_back(A);
                if ((da >= 0.0f) != (db >= 0.0f))
                    out.push_back(A + (B - A) * (da / (da - db)));
            }
            poly = std::move(out);
        }
        std::vector<Math::Vector3> points;
        float32 deepest = 0.0f;
        for (const Math::Vector3& l : poly)
        {
            const float32 depth = comp(q.half, axis) - comp(l, axis) * comp(nl, axis);
            if (depth < 0.0f)
                continue;
            points.push_back(q.x + Rq.multiply(l + q.boxCentre - q.cm));
            deepest = std::max(deepest, depth);
        }
        if (points.empty())
            continue;
        p.contacts = std::max(p.contacts, static_cast<int32>(points.size()));
        q.contacts = std::max(q.contacts, static_cast<int32>(points.size()));
        // The closing speed at the deepest point decides a sleeping one's waking (over 0.3 m/s).
        float32 closing = 0.0f;
        for (const Math::Vector3& at : points)
            closing = std::min(closing, (p.v + p.w.cross(at - p.x) - q.v - q.w.cross(at - q.x)).dot(n));
        for (Body* s : {&p, &q})
            if (!s->awake && !s->type.immovable && closing < -0.3f)
            {
                s->awake = true;
                s->still = 0.0f;
                const int32 index = static_cast<int32>(s - bodies_.data());
                if (std::find(woken_.begin(), woken_.end(), index) == woken_.end())
                {
                    woken_.push_back(index);
                    newlyAwake_.push_back(s->id);
                }
            }
        const bool pFixed = p.type.immovable || !p.awake, qFixed = q.type.immovable || !q.awake;
        const float32 ip = pFixed ? 0.0f : 1.0f / std::max(p.type.mass, 1e-3f);
        const float32 iq = qFixed ? 0.0f : 1.0f / std::max(q.type.mass, 1e-3f);
        if (ip + iq <= 0.0f)
            continue;
        // Apart again once, by the deepest corner, shared by mass.
        if (first)
        {
            p.x += n * (deepest * ip / (ip + iq));
            q.x -= n * (deepest * iq / (ip + iq));
        }
        // The impulses from one state, split (as with the world: several contacts sized on the
        // masses alone), then friction.
        const float32 count = static_cast<float32>(points.size());
        const float32 e = (p.type.bounce + q.type.bounce) * 0.5f;
        std::vector<std::pair<Math::Vector3, float32>> normal;
        for (const Math::Vector3& at : points)
        {
            const Math::Vector3 rp = at - p.x, rq = at - q.x;
            const float32 vn = (p.v + p.w.cross(rp) - q.v - q.w.cross(rq)).dot(n);
            if (vn >= 0.0f)
                continue;
            const float32 k = points.size() > 1 ? ip + iq
                                                : (pFixed ? 0.0f : effectiveMass(p, rp, n)) + (qFixed ? 0.0f : effectiveMass(q, rq, n));
            normal.emplace_back(at, -(1.0f + e) * vn / k / count);
        }
        for (const auto& [at, j] : normal)
        {
            if (!pFixed)
                applyImpulse(p, at - p.x, n * j);
            if (!qFixed)
                applyImpulse(q, at - q.x, n * -j);
        }
        // Friction between them (ours: the world's rule, the lesser FRICTION and the lighter mass).
        const float32 cap = std::min(p.type.friction, q.type.friction) * std::min(p.type.mass, q.type.mass) * kTick /
                            static_cast<float32>(kPasses);
        const bool flat = closing > -kGravity * kTick * 1.5f && points.size() >= 3;
        if (flat)
        {
            // Flat on each other they slide as one (as the world's above).
            const Math::Vector3 vr = p.v - q.v;
            const Math::Vector3 vt = vr - n * vr.dot(n);
            const float32 sl = vt.length();
            if (sl > 1e-5f)
            {
                const float32 jt = std::min(sl / (ip + iq), cap * count);
                p.v -= vt * (jt * ip / sl);
                q.v += vt * (jt * iq / sl);
            }
        }
        std::vector<std::pair<Math::Vector3, Math::Vector3>> friction;
        for (const Math::Vector3& at : flat ? std::vector<Math::Vector3>{} : points)
        {
            const Math::Vector3 rp = at - p.x, rq = at - q.x;
            const Math::Vector3 vr = p.v + p.w.cross(rp) - q.v - q.w.cross(rq);
            const Math::Vector3 vt = vr - n * vr.dot(n);
            const float32 sl = vt.length();
            if (sl <= 1e-5f)
                continue;
            const Math::Vector3 tan = vt * (1.0f / sl);
            const float32 kt = (pFixed ? 0.0f : effectiveMass(p, rp, tan)) + (qFixed ? 0.0f : effectiveMass(q, rq, tan));
            friction.emplace_back(at, tan * (std::min(sl / kt, cap) / count));
        }
        for (const auto& [at, j] : friction)
        {
            if (!pFixed)
                applyImpulse(p, at - p.x, j * -1.0f);
            if (!qFixed)
                applyImpulse(q, at - q.x, j);
        }
        // Resting flat on it (three or more points): it cannot tip, only turn about the normal,
        // and the upper one lies flush on the other (what tilt the landing left goes; ours).
        if (flat)
        {
            if (!pFixed)
                p.w = n * p.w.dot(n);
            if (!qFixed)
                q.w = n * q.w.dot(n);
            Math::Vector3 fl{0.0f, 0.0f, 0.0f};
            (face / 2 == 0 ? fl.x : face / 2 == 1 ? fl.y : fl.z) = (face & 1) ? -1.0f : 1.0f;
            const Math::Vector3 from = Rp.multiply(fl), to = n * -1.0f; // p's face, as it should be
            const bool turnP = !pFixed && (qFixed || p.x.y > q.x.y);
            if (turnP || !qFixed)
            {
                // The turn taking p's face onto -n (or, turning q, n onto -p's face).
                const Math::Vector3 axisV = turnP ? from.cross(to) : to.cross(from);
                const Math::Quaternion r = Math::Quaternion{1.0f + from.dot(to), axisV.x, axisV.y, axisV.z}.normalized();
                Body& s = turnP ? p : q;
                s.turn = (r * s.turn).normalized();
            }
        }
        return; // (one face contact a pair: the other way round is the same contact)
    }
}

void PhysicsProps::integrate(Body& b, float32 dt)
{
    const PhysicsType& t = b.type;
    if (t.immovable)
    {
        b.v = b.w = {0.0f, 0.0f, 0.0f};
        return;
    }
    if (!t.noGravity)
    {
        b.v.y -= kGravity * dt;
        b.v.y = std::max(b.v.y, -kMaxFall);
    }
    if (t.airResistance > 0.0f)
        b.v *= 1.0f - std::min(t.airResistance, 1.0f);
    if (t.spinResistance > 0.0f)
        b.w *= 1.0f - std::min(t.spinResistance, 1.0f);
    b.x += b.v * dt;
    // The turn: dq = 1/2 (0, w) q dt.
    {
        const Math::Quaternion wq{0.0f, b.w.x, b.w.y, b.w.z};
        const Math::Quaternion dq = (wq * b.turn) * (0.5f * dt);
        b.turn = Math::Quaternion{b.turn.w + dq.w, b.turn.x + dq.x, b.turn.y + dq.y, b.turn.z + dq.z}.normalized();
    }
}

int32 PhysicsProps::solveWorld(Body& b, float32 dt, const World& w, float32 now, bool first)
{
    const PhysicsType& t = b.type;
    // Contacts: each corner against the world's collision.
    struct Contact {
        Math::Vector3 at, n;
        float32 depth;
    };
    std::vector<Contact> contacts;
    const Math::Matrix3x3 R = b.rotation();
    if (w.hit)
        for (int32 k = 0; k < 8; ++k)
        {
            const Math::Vector3 corner = b.boxCentre + Math::Vector3{(k & 1) ? b.half.x : -b.half.x, (k & 2) ? b.half.y : -b.half.y,
                                                                     (k & 4) ? b.half.z : -b.half.z};
            const Math::Vector3 c = b.x + R.multiply(corner - b.cm);
            if (const auto h = w.hit(b.x, c))
            {
                Math::Vector3 n = h->normal;
                if (n.dot(b.x - h->at) < 0.0f)
                    n = n * -1.0f;
                contacts.push_back({h->at, n, std::max(0.0f, (h->at - c).dot(n))});
            }
        }
    if (!contacts.empty())
    {
        // Out of the surfaces.
        Math::Vector3 push{0.0f, 0.0f, 0.0f};
        int32 deep = 0;
        for (const Contact& c : contacts)
            if (c.depth > 1e-4f)
            {
                push += c.n * c.depth;
                ++deep;
            }
        if (deep > 0 && first)
            b.x += push * (1.0f / static_cast<float32>(deep));
        const float32 count = static_cast<float32>(contacts.size());
        const bool resting = b.v.dot(b.v) < (kGravity * dt) * (kGravity * dt) * 2.0f;
        // (the friction's budget a tick shared between the passes: ours)
        const float32 cap = t.friction * t.mass * dt / static_cast<float32>(kPasses);
        // Each contact's impulse from the same state, split between them (as the exe's 1/count),
        // then all applied: first along the normals, then against the sliding.
        std::vector<std::pair<Math::Vector3, Math::Vector3>> impulses; // (r, j)
        for (const Contact& c : contacts)
        {
            const Math::Vector3 r = c.at - b.x;
            const float32 vn = (b.v + b.w.cross(r)).dot(c.n);
            if (vn >= 0.0f)
                continue;
            // Its impact sound: closing faster than 3 m/s, once per two frames.
            if (first && -vn > 3.0f && now - b.soundAt > 2.0f * dt)
            {
                b.soundAt = now;
                impacts_.push_back({b.id, c.at, std::min(40, static_cast<int32>(std::floor(-vn * 40.0f / 6.0f)))});
            }
            const float32 e = resting ? 0.0f : t.bounce;
            // (ours: with several contacts each share is sized on the mass alone, so a flat landing
            // stops; their turning parts cancel)
            const float32 k = contacts.size() > 1 ? 1.0f / std::max(t.mass, 1e-3f) : effectiveMass(b, r, c.n);
            impulses.emplace_back(r, c.n * (-(1.0f + e) * vn / k / count));
        }
        for (const auto& [r, j] : impulses)
            applyImpulse(b, r, j);
        impulses.clear();
        // Resting, what of the approach is left (the corners' share that went into turning) goes
        // too (ours: the exe's accumulated resting impulses, +0x7c.., are not traced).
        if (resting)
        {
            for (const Contact& c : contacts)
                if (const float32 vn = b.v.dot(c.n); vn < 0.0f)
                    b.v -= c.n * vn;
            if (first)
                b.w *= 0.8f; // (and its rocking dies down: ours)
            // Flat on three or more corners it cannot tip, only turn about the floor's normal.
            if (contacts.size() >= 3)
            {
                Math::Vector3 nAvg{0.0f, 0.0f, 0.0f};
                for (const Contact& c : contacts)
                    nAvg += c.n;
                nAvg = nAvg.normalized();
                b.w = nAvg * b.w.dot(nAvg);
            }
        }
        if (cap > 0.0f && resting && contacts.size() >= 3)
        {
            // Flat, it slides as one: against its sliding along the floor (ours: no tipping turn
            // the lock above would take away again).
            Math::Vector3 nAvg{0.0f, 0.0f, 0.0f};
            for (const Contact& c : contacts)
                nAvg += c.n;
            nAvg = nAvg.normalized();
            const Math::Vector3 vt = b.v - nAvg * b.v.dot(nAvg);
            const float32 s = vt.length();
            if (s > 1e-5f)
            {
                const float32 stop = s * t.mass;
                const float32 jt = t.properFriction ? std::min(stop, cap) : std::min(stop, cap * s);
                b.v -= vt * (jt / stop);
            }
        }
        else if (cap > 0.0f)
            for (const Contact& c : contacts)
            {
                const Math::Vector3 r = c.at - b.x;
                const Math::Vector3 vp = b.v + b.w.cross(r);
                const Math::Vector3 vt = vp - c.n * vp.dot(c.n);
                const float32 s = vt.length();
                if (s <= 1e-5f)
                    continue;
                const Math::Vector3 tan = vt * (1.0f / s);
                const float32 stop = s / effectiveMass(b, r, tan);
                const float32 jt = (t.properFriction ? std::min(stop, cap) : std::min(stop, cap * s)) / count;
                impulses.emplace_back(r, tan * -jt);
            }
        for (const auto& [r, j] : impulses)
            applyImpulse(b, r, j);
    }
    return static_cast<int32>(contacts.size());
}

void PhysicsProps::settle(Body& b, float32 dt, const World& w, float32 now, std::vector<Crush>& crushed)
{
    const PhysicsType& t = b.type;
    const Math::Matrix3x3 R = b.rotation();
    // A velocity under 1e-5 (squared) is no velocity (0x48bb80); still for half a second, it
    // sleeps (ours).
    if (b.v.dot(b.v) < 1e-5f)
        b.v = {0.0f, 0.0f, 0.0f};
    if (b.contacts > 0 && b.v.dot(b.v) < 0.01f && b.w.dot(b.w) < 0.04f)
        b.still += dt;
    else
        b.still = 0.0f;
    if (b.still > 0.5f)
    {
        b.awake = false;
        b.v = b.w = {0.0f, 0.0f, 0.0f};
    }

    // The characters: an impulse off each it runs into, and crushing (iron only).
    if (b.v.dot(b.v) < 0.0025f && b.w.dot(b.w) < 0.0025f)
        return;
    const Math::Matrix3x3 Rt = R.transpose();
    for (const Character& ch : w.characters)
    {
        for (int32 k = 0; k < 3; ++k)
        {
            const Math::Vector3 p =
                ch.feet + Math::Vector3{0.0f, ch.radius + (ch.height - 2.0f * ch.radius) * static_cast<float32>(k) * 0.5f, 0.0f};
            const Math::Vector3 local = Rt.multiply(p - b.x) + b.cm;
            const Math::Vector3 lo = b.boxCentre - b.half, hi = b.boxCentre + b.half;
            const Math::Vector3 clamped{std::clamp(local.x, lo.x, hi.x), std::clamp(local.y, lo.y, hi.y), std::clamp(local.z, lo.z, hi.z)};
            const Math::Vector3 closest = b.x + R.multiply(clamped - b.cm);
            const float32 d = (p - closest).length();
            if (d >= ch.radius)
                continue;
            const Math::Vector3 n = d > 1e-4f ? (p - closest) * (1.0f / d) : (p - b.x).normalized();
            const Math::Vector3 r = closest - b.x;
            const float32 approach = (b.v + b.w.cross(r) - ch.velocity).dot(n);
            if (approach <= 0.0f)
                break;
            const float32 j = (1.0f + t.bounce * 0.5f) * approach / (effectiveMass(b, r, n) + 1.0f / std::max(ch.mass, 1.0f));
            applyImpulse(b, r, n * -j);
            b.x -= n * (ch.radius - d);
            if (!b.iron)
                break;
            const bool above = n.y < -kAboveNormal; // the body is over him
            const bool falling = b.v.y < -1.0f && b.v.y < kFallAcross * (std::fabs(b.v.x) + std::fabs(b.v.z));
            const float32 damage = above && falling ? kCrushFromAbove : approach * t.mass;
            float32& last = b.crushedAt.try_emplace(ch.id, -1e9f).first->second;
            if (damage > kCrushMomentum && now - last > 1.0f)
            {
                last = now;
                crushed.push_back({b.id, ch.id, damage, closest});
            }
            break;
        }
    }
}

} // namespace Game
