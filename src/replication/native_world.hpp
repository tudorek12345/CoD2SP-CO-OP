#pragma once
#include "networking/protocol.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace cod2coop {
enum class NativeWorldError {
 None, InvalidTick, TooManySlots, InvalidClientCapacity, MissingBuffer,
 TruncatedBuffer, IdentityExhausted
};
const char* native_world_error(NativeWorldError error) noexcept;

struct NativeWorldObservation {
 bool valid=false;
 NativeWorldError error=NativeWorldError::None;
 uint32_t tick=0, map_epoch=1;
 bool map_reset=false;
 uint16_t source_slots=0, active_slots=0, invalid_positions=0;
 uint16_t omitted_entities=0, created_slots=0, retired_slots=0;
 // Only stable network IDs, observed native eType and actual finite positions.
 // No native slot indices, client pointers, inferred rotations or fixture actors.
 std::vector<net::WorldEntity> entities;
};

// One owning game thread supplies an already copied, exact-build-verified entity
// array. This class never reads process memory or invokes an engine function.
// Retain the tracker across maps so dynamic IDs remain monotonic. Player IDs 1/2
// are reserved logical participant identities; dynamic IDs start at 3 and are
// never reused by this tracker, including after a zero-count map reset.
class NativeWorldTracker {
public:
 static constexpr size_t kEntityStride=0x1fc;
 static constexpr size_t kMaxSlots=2046;
 static constexpr size_t kMaxObservedEntities=net::kMaxWorldEntities*net::kMaxWorldPages;
 static constexpr float kMaxCoordinate=1048576.f;
 NativeWorldTracker()=default;
 NativeWorldTracker(const NativeWorldTracker&)=delete;
 NativeWorldTracker& operator=(const NativeWorldTracker&)=delete;
 NativeWorldObservation observe(const uint8_t* bytes,size_t byte_count,
                                uint32_t entity_count,unsigned client_capacity,
                                uint32_t monotonic_tick);
private:
 enum class Role : uint8_t { Inactive, PlayerOne, PlayerTwo, Dynamic };
 struct Slot { uint32_t identity=0; Role role=Role::Inactive; };
 static Role role(const uint8_t* slot,size_t index,unsigned client_capacity) noexcept;
 std::array<Slot,kMaxSlots> slots_{};
 uint64_t next_dynamic_id_=3;
 uint32_t map_epoch_=1, previous_count_=0;
};

// At most 64 pages of 20 records; an empty valid world yields one empty page.
// The supplied tick must be a nonzero monotonic/session tick, not resettable
// mission time. Page loss is possible: consumers must not treat a partial set as
// a complete world or infer removal from a selection that reports omissions.
std::vector<net::WorldUpdate> native_world_pages(const NativeWorldObservation& observation);
}
