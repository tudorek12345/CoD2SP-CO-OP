#include "replication/native_world.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>

namespace cod2coop {
const char* native_world_error(NativeWorldError error) noexcept {
 switch(error){
 case NativeWorldError::None:return "none";
 case NativeWorldError::InvalidTick:return "world tick must be nonzero";
 case NativeWorldError::TooManySlots:return "entity count exceeds verified slot bound";
 case NativeWorldError::InvalidClientCapacity:return "client capacity must be one or two for a nonempty world";
 case NativeWorldError::MissingBuffer:return "nonempty world has no byte buffer";
 case NativeWorldError::TruncatedBuffer:return "copied entity array is truncated";
 case NativeWorldError::IdentityExhausted:return "dynamic network entity identity space exhausted";
 }
 return "unknown native world error";
}
NativeWorldTracker::Role NativeWorldTracker::role(const uint8_t* slot,size_t index,unsigned clients) noexcept {
 if(!slot[0xa4])return Role::Inactive;
 if(index==0)return Role::PlayerOne;
 uint32_t client=0;std::memcpy(&client,slot+0xfc,sizeof(client));
 if(index==1 && clients==2 && client!=0)return Role::PlayerTwo;
 return Role::Dynamic;
}
NativeWorldObservation NativeWorldTracker::observe(const uint8_t* bytes,size_t byte_count,
                                                 uint32_t count,unsigned clients,uint32_t tick) {
 NativeWorldObservation out;out.tick=tick;out.map_epoch=map_epoch_;
 auto fail=[&out](NativeWorldError error){out.error=error;return out;};
 // Reject invalid samples before changing identity/lifecycle state.
 if(!tick)return fail(NativeWorldError::InvalidTick);
 if(count>kMaxSlots)return fail(NativeWorldError::TooManySlots);
 if(clients>2 || (count && clients==0))return fail(NativeWorldError::InvalidClientCapacity);
 if(count && !bytes)return fail(NativeWorldError::MissingBuffer);
 if(byte_count<size_t(count)*kEntityStride)return fail(NativeWorldError::TruncatedBuffer);
 out.source_slots=static_cast<uint16_t>(count);
 if(!count){
  if(previous_count_){
   for(const auto& slot:slots_)if(slot.role!=Role::Inactive)++out.retired_slots;
   slots_.fill({});++map_epoch_;out.map_reset=true;
  }
  previous_count_=0;out.map_epoch=map_epoch_;out.valid=true;return out;
 }
 size_t needed=0;
 for(size_t i=0;i<count;++i){
  auto current=role(bytes+i*kEntityStride,i,clients);
  if(current==Role::Dynamic && slots_[i].role!=Role::Dynamic)++needed;
 }
 constexpr uint64_t limit=std::numeric_limits<uint32_t>::max();
 if(needed && (next_dynamic_id_>limit || needed>limit-next_dynamic_id_+1))return fail(NativeWorldError::IdentityExhausted);
 out.entities.reserve(std::min<size_t>(count,kMaxObservedEntities));
 for(size_t i=0;i<kMaxSlots;++i){
  auto& state=slots_[i];
  const uint8_t* native=i<count?bytes+i*kEntityStride:nullptr;
  Role current=native?role(native,i,clients):Role::Inactive;
  if(current!=state.role){
   if(state.role!=Role::Inactive)++out.retired_slots;
   state={};state.role=current;
   if(current!=Role::Inactive){
    ++out.created_slots;
    state.identity=current==Role::PlayerOne?1:current==Role::PlayerTwo?2:static_cast<uint32_t>(next_dynamic_id_++);
   }
  }
  if(current==Role::Inactive)continue;
  ++out.active_slots;
  float position[3]{};std::memcpy(position,native+0xdc,sizeof(position));
  bool position_valid=true;
  for(float value:position)if(!std::isfinite(value) || std::abs(value)>kMaxCoordinate)position_valid=false;
  if(!position_valid){++out.invalid_positions;continue;}
  if(out.entities.size()==kMaxObservedEntities){++out.omitted_entities;continue;}
  net::WorldEntity entity;entity.entity=state.identity;entity.native_slot=static_cast<uint16_t>(i);entity.native_type=native[0];
  entity.fields=1;entity.x=position[0];entity.y=position[1];entity.z=position[2];
  // Rotation, health, weapons, AI state and animation have not been observed by
  // this interface. In particular no inferred zero rotation is marked valid.
  entity.pitch=entity.yaw=entity.roll=0;out.entities.push_back(entity);
 }
 previous_count_=count;out.valid=true;return out;
}
std::vector<net::WorldUpdate> native_world_pages(const NativeWorldObservation& observation){
 if(!observation.valid || !observation.tick || observation.entities.size()>NativeWorldTracker::kMaxObservedEntities)return{};
 const size_t pages=std::max<size_t>(1,(observation.entities.size()+net::kMaxWorldEntities-1)/net::kMaxWorldEntities);
 std::vector<net::WorldUpdate> result;result.reserve(pages);
 for(size_t page=0;page<pages;++page){
  net::WorldUpdate update;update.tick=observation.tick;update.map_epoch=observation.map_epoch;update.page=static_cast<uint16_t>(page);update.pages=static_cast<uint16_t>(pages);
  const size_t start=page*net::kMaxWorldEntities,end=std::min(start+net::kMaxWorldEntities,observation.entities.size());
  update.entities.assign(observation.entities.begin()+start,observation.entities.begin()+end);result.push_back(std::move(update));
 }
 return result;
}
}
