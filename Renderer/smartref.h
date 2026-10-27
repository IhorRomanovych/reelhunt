// Renderer/smartref.h - scene object kind identifiers.
#pragma once
#include "engine_types.h"

namespace Renderer {

// Object kind identifiers used by the Entity subclasses (mesh, camera, light).
enum class ObjectType : uint32 {
    Mesh = 0x0001,
    Camera = 0x0002,
    Light = 0x0004,
};

} // namespace Renderer
