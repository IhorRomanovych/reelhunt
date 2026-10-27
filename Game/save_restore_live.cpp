// Game/save_restore_live.cpp - live wiring: apply a RestoredEntity to a GameEntity.
// Kept separate from save_restore.cpp so the pure-data restore stays linkable into save_test
// without pulling in the Game subsystem (AiBrain / Animator / ...).
#include "Game/save_restore.h"
#include "Game/entity.h"

void Game::applyToEntity(Game::GameEntity& ent, const RestoredEntity& re)
{
    // Set the transform verbatim (setRotation would renormalise the quaternion, so an untouched
    // entity would no longer write back bit-identical plane floats).
    Math::Transform t = ent.transform();
    t.position = re.position;
    t.rotation = re.rotation;
    ent.setTransform(t);
    // Every integrity-bearing kind restores its health, including 0 (destroyed) and the dynamic
    // (D) kind: writeBack() saves the live health for all of them, so skipping any here wrote
    // the default 100 back over the saved value (a destroyed prop came back intact).
    if (hasIntegrity(re.state.kind))
        ent.setHealth(re.state.integrity);
}

void Game::writeBack(SaveEntityRecord& rec, const Game::GameEntity& ent)
{
    RestoredEntity re;
    const auto& t = ent.transform();
    re.position = t.position;
    re.rotation = t.rotation;
    re.state.integrity = ent.health();
    writeBack(rec, re);
}
