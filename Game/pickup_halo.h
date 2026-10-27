// Game/pickup_halo.h - the halo around an item lying in the level.
//
// The exe: a lying collectable (0x4c7cc0) queues a halo every frame (0x5fb220: up to 32, only
// within 50 m of the camera) at its model's middle (entity +0x150), and 0x5fb330 draws the queue:
// pictures/frontend_pc.txd's "halo" (a soft bright ring round a faint, darker middle) as a quad
// facing the camera, `size` wide (0x5faf60: the camera's right and up times size / 2), added to
// the scene (SRCBLEND source alpha, DESTBLEND one; no depth writes, depth-tested, no fog).
//   Colour (0..255) and size (0x4c7cc0), by COLLECTABLE_TYPE then HOLSTER_SLOT (type +0x168):
//     CT_PAINKILLERS, CT_G_FIRST_AID, CT_Y_FIRST_AID (8..10)  80, 80, 80; 0.9 + 0.08 sin(2 pi t / 1024 ms)
//     the ammo boxes CT_AMMO_* (102..107)                     60, 30, 0;  0.8
//     BACK 80, 0, 0   BELT_RIGHT 0, 0, 80   BELT_LEFT 0, 80, 0   BELT_REAR 80, 80, 0
//     SPECIAL 20, 70, 50  (all 0.9); no holster slot: no halo
//   0x5fb6d0: the colour fades in from 0.6 m to 4 m from the camera ((d - 0.6) / 3.4) and out from
//   40 m to 50 m ((50 - d) / 10), each channel truncated; the alpha flickers, a new random value in
//   50..110 every frame (rand & 0x7ff over 2047, times 60, plus 50).
//   Other entities queue the same halo:
//     a save point (CTrigger 0x4d27a0, while its +0x178 is set: a Gen_Save_Point, 0x4d0a20)
//       20, 80, 120; 1.5, at its origin
//     a gas can (CEntity 0x435a50: GAS_CAN, type +0x94 & 0x400000, while its physics object says
//       so through vtable +0x14 - ours: always) 20, 70, 30; 0.9, 0.3 m above its origin
//   0x5fbbd0 then draws a globe of lines within 13 m (20 meridians pulsing in turn, radius
//   size * 0.42), but with no texture of its own set it samples the halo's corner (0, 0), which
//   is black, so on the PC it adds nothing: not drawn here.
#pragma once
#include "engine_types.h"

#include <memory>
#include <random>
#include <string>

#include "Math/vector.h"
#include "Renderer/mesh.h"
#include "Renderer/texture.h"

namespace Game {

struct PickupHalo {
    bool shown = false;      // a holster slot or one of the CT types above
    int32 r = 0, g = 0, b = 0; // 0..255
    float32 size = 0.9f;     // metres across
    bool pulses = false;     // first aid: its size breathes
};

// The halo of an item of COLLECTABLE_TYPE `ct` (the CT_ number) and HOLSTER_SLOT `holsterSlot`.
[[nodiscard]] PickupHalo pickupHalo(int32 ct, const std::string& holsterSlot);
// A save point's halo and a gas can's (above).
[[nodiscard]] PickupHalo savePointHalo();
[[nodiscard]] PickupHalo gasCanHalo();

// A halo quad (hidden until placed): the halo texture, added unlit to the scene.
std::unique_ptr<Renderer::Mesh> makePickupHalo(const Renderer::Texture* texture);

// This frame's halo at `centre`: facing a camera at `eye` with world axes `right` / `up`, its
// colour faded by distance and its alpha drawn from `rng`. `timeMs` drives the first-aid pulse.
void placePickupHalo(Renderer::Mesh& mesh, const PickupHalo& halo, const Math::Vector3& centre, const Math::Vector3& eye,
                     const Math::Vector3& right, const Math::Vector3& up, uint32 timeMs, std::mt19937& rng);

} // namespace Game
