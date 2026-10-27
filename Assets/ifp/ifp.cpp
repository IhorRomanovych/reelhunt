// Assets/ifp/ifp.cpp - IFP (Animation File Player) parser.
#include "ifp.h"
#include "Assets/binary_reader.h"
#include <algorithm>
#include <cstring>
#include <cmath>
#include <cctype>

namespace Assets {

namespace {

struct Reader {
    const uint8* d = nullptr;
    std::size_t size = 0u;
    std::size_t p = 0u;
    bool ok = true;

    // Overflow-safe: n is compared against the bytes left, never added to p first.
    bool need(std::size_t n) noexcept { if (!ok) return false; if (n > size - p) { ok = false; return false; } return true; }
    void skip(std::size_t n) noexcept { if (need(n)) p += n; else ok = false; }
    void back(std::size_t n) noexcept { if (ok && p >= n) p -= n; else ok = false; }

    uint8 u8() noexcept { if (!need(1u)) { ok = false; return 0u; } return d[p++]; }
    int16 i16le() noexcept { if (!need(2u)) { ok = false; return 0; } int16 v = static_cast<int16>(static_cast<uint16>(d[p] | (d[p + 1] << 8))); p += 2u; return v; }
    uint16 u16le() noexcept { if (!need(2u)) { ok = false; return 0u; } uint16 v = static_cast<uint16>(d[p] | (d[p + 1] << 8)); p += 2u; return v; }
    uint32 u32le() noexcept { if (!need(4u)) { ok = false; return 0u; } uint32 v = d[p] | (static_cast<uint32>(d[p + 1]) << 8) | (static_cast<uint32>(d[p + 2]) << 16) | (static_cast<uint32>(d[p + 3]) << 24); p += 4u; return v; }
    float32 f32le() noexcept { if (!need(4u)) { ok = false; return 0.0f; } uint32 b = u32le(); float32 f; std::memcpy(&f, &b, sizeof(f)); return f; }

    bool tag(const char* t) noexcept {
        if (!need(4u)) { ok = false; return false; }
        bool match = (std::memcmp(d + p, t, 4) == 0);
        p += 4u;
        if (!match) ok = false;   // count-driven walk: a tag mismatch means misalignment
        return match;
    }

    std::string str(uint32 n) {
        std::string s;
        if (n == 0u || !need(n)) { ok = false; return s; }
        s.assign(reinterpret_cast<const char*>(d + p), n);
        auto nul = s.find('\0');
        if (nul != std::string::npos) s.erase(nul);
        p += n;
        return s;
    }
};

// Normalize an (x,y,z,w) quaternion; a degenerate one becomes the identity.
void norm4(const float32 in[4], float32 out[4]) noexcept {
    float32 l = std::sqrt(in[0] * in[0] + in[1] * in[1] + in[2] * in[2] + in[3] * in[3]);
    if (l < 1e-6f) { out[0] = 0; out[1] = 0; out[2] = 0; out[3] = 1; return; }
    float32 inv = 1.0f / l;
    for (int i = 0; i < 4; ++i) out[i] = in[i] * inv;
}

// Spherical linear interpolation with nlerp fallback for near-parallel quaternions.
void quatSlerp(const float32 a[4], const float32 b[4], float32 u, float32 out[4]) noexcept {
    float32 dot = a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
    float32 bs[4];
    if (dot < 0.0f) {
        for (int i = 0; i < 4; ++i) bs[i] = -b[i];
        dot = -dot;
    } else {
        for (int i = 0; i < 4; ++i) bs[i] = b[i];
    }

    if (dot > 0.9995f) {
        // Near-parallel: nlerp fallback (avoids division by ~zero sinTheta).
        for (int i = 0; i < 4; ++i) out[i] = a[i] + (bs[i] - a[i]) * u;
    } else {
        float32 theta = std::acos(std::min(1.0f, std::max(-1.0f, dot)));
        float32 sinTheta = std::sin(theta);
        float32 wa = std::sin((1.0f - u) * theta) / sinTheta;
        float32 wb = std::sin(u * theta) / sinTheta;
        for (int i = 0; i < 4; ++i) out[i] = a[i] * wa + bs[i] * wb;
    }
    norm4(out, out);
}

} // namespace

const IfpBoneTrack* IfpAnimation::findBone(int16 boneId) const noexcept {
    for (const auto& b : bones) if (b.boneId == boneId) return &b;
    return nullptr;
}

uint32 IfpFile::totalAnimations() const noexcept {
    uint32 n = 0u;
    for (const auto& b : blocks) n += static_cast<uint32>(b.animations.size());
    return n;
}

uint32 IfpFile::totalBones() const noexcept {
    uint32 n = 0u;
    for (const auto& b : blocks)
        for (const auto& a : b.animations)
            n += static_cast<uint32>(a.bones.size());
    return n;
}

const IfpAnimation* IfpFile::findAnimation(const std::string& name, const std::string& block) const noexcept {
    auto same = [](const std::string& a, const std::string& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
                   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
               });
    };
    for (const auto& b : blocks)
        if (same(b.name, block))
            for (const auto& a : b.animations)
                if (same(a.name, name))
                    return &a;
    if (same(block, "GenHunterAnims"))
        return findAnimation(name, "PlayerAnims");
    if (same(block, "PlayerAnims") || same(block, "EntityAnims") || same(block, "HeadAnim"))
        return nullptr;
    return findAnimation(name, "GenHunterAnims");
}

const IfpAnimation* IfpFile::findAnimation(const std::string& name) const noexcept {
    // Exact (case-insensitive) match first: a substring-only search returned the first clip
    // whose name merely contained `name` (e.g. "Idle_01" also matches "Idle_010").
    auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
    const std::string needle = lower(name);
    if (needle.empty()) return nullptr;
    for (const auto& b : blocks)
        for (const auto& a : b.animations)
            if (lower(a.name) == needle) return &a;
    for (const auto& b : blocks)
        for (const auto& a : b.animations)
            if (lower(a.name).find(needle) != std::string::npos) return &a;
    return nullptr;
}

std::expected<IfpFile, Engine::EngineError> loadIfp(const std::string& path) {
    auto bytes = readFileBytes(path);
    if (!bytes)
        return std::unexpected(std::move(bytes.error()));
    return parseIfp(bytes->data(), bytes->size());
}

std::expected<IfpFile, Engine::EngineError> parseIfp(const uint8* data, std::size_t size) {
    IfpFile out;
    if (!data || size < 8u)
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Invalid, "ifp: too small"));

    Reader r{data, size};
    if (!r.tag("ANCT"))
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Invalid, "ifp: bad ANCT magic"));

    out.blockCount = r.u32le();   // number of BLOC sections (was mislabelled "version")

    for (uint32 b = 0u; b < out.blockCount && r.ok; ++b) {
        if (!r.tag("BLOC")) break;
        uint32 nameLen = r.u32le();
        IfpBlock block;
        block.name = r.str(nameLen);
        if (!r.tag("ANPK")) break;
        uint32 animCount = r.u32le();

        for (uint32 a = 0u; a < animCount && r.ok; ++a) {
            IfpAnimation anim;
            if (!r.tag("NAME")) break;
            uint32 anLen = r.u32le();
            anim.name = r.str(anLen);

            uint32 numBones = r.u32le();      // PC path (u32)
            r.u32le();                        // chunkSize (byte size of bone region; not needed for count-driven walk)
            float32 frameTimeCount = r.f32le();   // duration in seconds (PC)
            anim.duration = frameTimeCount;

            for (uint32 bn = 0u; bn < numBones && r.ok; ++bn) {
                IfpBoneTrack track;
                if (!r.tag("SEQU")) break;    // MH1 uses SEQU (MH2 uses SEQT)
                track.boneId = r.i16le();
                track.frameType = r.u8();
                int16 framesRaw = r.i16le();
                uint32 frames = (framesRaw > 0) ? static_cast<uint32>(framesRaw) : 0u;
                // Times are in 1/2048 s. Zero = every key carries its own delta (below); non-zero =
                // the keys are evenly spaced this far apart (68 = one 30 fps frame; a single-key
                // track stores the clip length). The toolkit's `index/2048*30 + start - 1` packed all
                // such keys into the first ~10 ms, freezing those bones on their last key.
                uint16 startTimeRaw = r.u16le();
                const float32 keySpacing = static_cast<float32>(startTimeRaw) / 2048.0f;   // seconds
                bool startZero = (startTimeRaw == 0u);

                // Frame type 3: one rotation for the whole track (this header, a quaternion in
                // 1/4096 like the per-key ones) and positions only in the keys.
                float32 constantQuat[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                if (track.frameType == 3u) {
                    for (int i = 0; i < 4; ++i) constantQuat[i] = static_cast<float32>(r.i16le()) / 4096.0f;
                } else if (track.frameType < 3u && startZero) {
                    r.back(2u);   // startTime bytes are actually the first frame's time delta
                }

                float32 accTime = 0.0f;   // seconds
                for (uint32 f = 0u; f < frames && r.ok; ++f) {
                    IfpFrame fr;
                    if (startZero) {
                        if (!(f == 0u && track.frameType == 3u))
                            accTime += static_cast<float32>(r.u16le()) / 2048.0f;   // cumulative delta
                        else
                            accTime = 0.0f;
                    } else {
                        accTime = static_cast<float32>(f) * keySpacing;
                    }
                    fr.time = accTime;

                    if (track.frameType < 3u) {
                        for (int i = 0; i < 4; ++i) fr.quat[i] = static_cast<float32>(r.i16le() / 4096.0f);
                        fr.hasQuat = true;
                    } else if (track.frameType == 3u) {
                        for (int i = 0; i < 4; ++i) fr.quat[i] = constantQuat[i];
                        fr.hasQuat = true;
                    }
                    if (track.frameType > 1u) {
                        for (int i = 0; i < 3; ++i) fr.pos[i] = static_cast<float32>(r.i16le() / 2048.0f);
                        fr.hasPos = true;
                    }
                    track.frames.push_back(std::move(fr));
                }
                anim.bones.push_back(std::move(track));
            }

            // Trailing per-animation data (effect/particle entries - not skeletal, skipped).
            r.u32le();   // headerSize (16)
            r.u32le();   // unknown5
            const std::size_t eachEntrySize = r.u32le();
            const std::size_t numEntry = r.u32le();
            if (!r.ok) break;
            // 64-bit product: a corrupt pair no longer wraps to a small skip and misaligns the walk.
            r.skip(numEntry * eachEntrySize);

            block.animations.push_back(std::move(anim));
        }
        out.blocks.push_back(std::move(block));
    }

    if (!r.ok)
        return std::unexpected(Engine::EngineError::failure(Engine::EngineError::Code::Invalid, "ifp: truncated or misaligned"));
    return out;
}

bool trackTravel(const IfpBoneTrack& track, float32 outDelta[3], float32& t0, float32& t1) noexcept {
    const auto& fr = track.frames;
    if (fr.size() < 2u || !fr.front().hasPos || !fr.back().hasPos)
        return false;
    t0 = fr.front().time;
    t1 = fr.back().time;
    if (t1 - t0 < 1e-4f)
        return false;
    for (int k = 0; k < 3; ++k)
        outDelta[k] = fr.back().pos[k] - fr.front().pos[k];
    return true;
}

float32 rootGroundSpeed(const IfpAnimation& anim) noexcept {
    const IfpBoneTrack* t = anim.findBone(kRootMotionBoneId);
    float32 d[3], t0 = 0.0f, t1 = 0.0f;
    if (!t || !trackTravel(*t, d, t0, t1))
        return 0.0f;
    return std::sqrt(d[0] * d[0] + d[2] * d[2]) / (t1 - t0);
}

bool sampleBone(const IfpAnimation& anim, int16 boneId, float32 tSec,
                float32 outQuat[4], float32 outPos[3]) {
    const IfpBoneTrack* tr = anim.findBone(boneId);
    if (!tr || tr->frames.empty()) return false;
    sampleTrack(*tr, tSec, outQuat, outPos);
    return true;
}

void sampleTrack(const IfpBoneTrack& track, float32 tSec, float32 outQuat[4], float32 outPos[3]) {
    const auto& fr = track.frames;
    if (fr.empty()) {
        outQuat[0] = 0; outQuat[1] = 0; outQuat[2] = 0; outQuat[3] = 1;
        outPos[0] = 0; outPos[1] = 0; outPos[2] = 0;
        return;
    }
    float32 t0 = fr.front().time;
    float32 t1 = fr.back().time;
    float32 t = (tSec < t0) ? t0 : ((tSec > t1) ? t1 : tSec);

    // Binary search: find last keyframe with time <= t.
    size_t lo = 0u, hi = fr.size() - 1u;
    while (lo + 1u < hi) {
        size_t mid = (lo + hi) / 2u;
        if (fr[mid].time <= t) lo = mid; else hi = mid;
    }
    const IfpFrame& a = fr[lo];
    const IfpFrame& b = (lo + 1u < fr.size()) ? fr[lo + 1] : fr[lo];
    float32 span = b.time - a.time;
    float32 u = (span > 1e-6f) ? ((t - a.time) / span) : 0.0f;

    // Rotation
    if (a.hasQuat && b.hasQuat) {
        quatSlerp(a.quat, b.quat, u, outQuat);
    } else if (a.hasQuat) {
        norm4(a.quat, outQuat);
    } else if (b.hasQuat) {
        norm4(b.quat, outQuat);
    } else {
        outQuat[0] = 0; outQuat[1] = 0; outQuat[2] = 0; outQuat[3] = 1; // x,y,z,w identity
    }

    // Translation
    if (a.hasPos && b.hasPos) {
        for (int k = 0; k < 3; ++k) outPos[k] = a.pos[k] + (b.pos[k] - a.pos[k]) * u;
    } else if (a.hasPos) {
        std::memcpy(outPos, a.pos, sizeof(float32) * 3);
    } else if (b.hasPos) {
        std::memcpy(outPos, b.pos, sizeof(float32) * 3);
    } else {
        outPos[0] = 0; outPos[1] = 0; outPos[2] = 0;
    }
}

} // namespace Assets
