// Core/object.cpp - Entity base class implementation.
#include "renderware_api_stub.h"

#include <atomic>

namespace {

ObjectId nextObjectId() noexcept {
    static std::atomic<ObjectId> next{1u};
    return next.fetch_add(1u, std::memory_order_relaxed);
}

} // namespace

namespace Engine {
namespace Rw {

Entity::Entity(uint32 type) noexcept : type_(type), id_(nextObjectId()) {}

void Entity::setActive(bool v) noexcept {
    flags_ = v ? (flags_ | EntityFlag::Active) : (flags_ & ~EntityFlag::Active);
}

} // namespace Rw
} // namespace Engine
