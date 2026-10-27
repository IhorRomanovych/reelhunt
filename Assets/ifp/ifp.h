// Assets/ifp/ifp.h - IFP (Animation File Player) parser.
//
// Authoritative layout for the game PC (little-endian, "SEQU" sequences),
// verified against the toolkit reference Ifp.php and allanims.ifp:
//
//   ROOT:
//     "ANCT" + u32 numBlock
//   per block (numBlock times):
//     "BLOC" + u32 nameLen + name[nameLen]          // name includes NUL terminator
//     "ANPK" + u32 animationCount
//   per animation (animationCount times):
//     "NAME" + u32 nameLen + name[nameLen]
//     u32 numberOfBones + u32 chunkSize + f32 frameTimeCount  // duration in seconds
//     numberOfBones bone tracks:
//       "SEQU" + i16 boneId + i8 frameType + i16 frames + u16 startTime
//       if frameType == 3 : 4 x i16 direction (/2048)
//       else if frameType < 3 && startTime == 0 : (first frame time re-read below)
//       per frame (frames times):
//         if startTime == 0: u16 timeDelta (/2048 = seconds, cumulative) [skipped for first frame of type 3]
//         if frameType < 3 : quat 4 x i16 / 4096   // x,y,z,w (RW RtQuat order: imaginary part first)
//         if frameType > 1 : pos  3 x i16 / 2048   // x,y,z
//     u32 headerSize + u32 unknown5 + u32 eachEntrySize + u32 numEntry
//     numEntry effect entries (eachEntrySize bytes; skipped - not skeletal)
//
// frameType semantics: 0/1 = rotation only, 2 = rotation+translation,
//                      3 = direction header + per-frame position only.

#pragma once
#include "engine_types.h"
#include <expected>
#include <string>
#include <vector>

namespace Assets {

struct IfpFrame {
    float32 time = 0.0f;          // absolute seconds
    bool hasQuat = false;
    float32 quat[4] = {0.0f, 0.0f, 0.0f, 1.0f};   // x,y,z,w (identity)
    bool hasPos = false;
    float32 pos[3] = {0.0f, 0.0f, 0.0f};          // x,y,z
};

struct IfpBoneTrack {
    int16 boneId = 0;             // matches DffBone::id / HAnimNodeInfo id
    uint8 frameType = 0;
    std::vector<IfpFrame> frames; // ascending by time
};

struct IfpAnimation {
    std::string name;
    float32 duration = 0.0f;      // seconds (frameTimeCount)
    std::vector<IfpBoneTrack> bones;
    const IfpBoneTrack* findBone(int16 boneId) const noexcept;
};

struct IfpBlock {
    std::string name;
    std::vector<IfpAnimation> animations;
    uint32 numAnimations() const noexcept { return static_cast<uint32>(animations.size()); }
};

struct IfpFile {
    uint32 blockCount = 0u;       // the u32 at offset 4 (was mislabelled "version")
    std::vector<IfpBlock> blocks;
    uint32 numBlocks() const noexcept { return static_cast<uint32>(blocks.size()); }
    uint32 totalAnimations() const noexcept;
    uint32 totalBones() const noexcept;
    // The animation whose name equals `name` (case-insensitive); if there is none, the first
    // whose name contains it. nullptr if neither exists.
    const IfpAnimation* findAnimation(const std::string& name) const noexcept;
    // The animation named `name` (case-insensitive) as an entity of animation block `block`
    // finds it (exe 0x496720): in that block; failing that, GenHunterAnims falls back to
    // PlayerAnims and any other block but PlayerAnims / EntityAnims / HeadAnim (a hunter's
    // DefaultHunterAnims, ...) to GenHunterAnims. nullptr if none has it.
    const IfpAnimation* findAnimation(const std::string& name, const std::string& block) const noexcept;
};

std::expected<IfpFile, Engine::EngineError> parseIfp(const uint8* data, std::size_t size);
std::expected<IfpFile, Engine::EngineError> loadIfp(const std::string& path);

// Interpolate a bone track at time t (seconds, clamped to the track's range).
// outQuat (x,y,z,w) is normalized; outPos is zero when the track carries no translation.
void sampleTrack(const IfpBoneTrack& track, float32 tSec, float32 outQuat[4], float32 outPos[3]);

// The game's skeleton root: the bone whose track carries a character's travel (root motion).
inline constexpr int16 kRootMotionBoneId = 1000;

// Straight-line travel of a position track from its first to its last key: the displacement
// (`outDelta`) and the keys' times. False when the track has under two position keys.
bool trackTravel(const IfpBoneTrack& track, float32 outDelta[3], float32& t0, float32& t1) noexcept;

// Ground speed (units/s, X/Z of the Y-up IFP frame) of `anim`'s root motion; 0 if it stays put.
float32 rootGroundSpeed(const IfpAnimation& anim) noexcept;

// As sampleTrack for the animation's track with this boneId; false if there is none.
bool sampleBone(const IfpAnimation& anim, int16 boneId, float32 tSec,
                float32 outQuat[4], float32 outPos[3]);

} // namespace Assets
