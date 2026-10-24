#pragma once
#include "networking/protocol.hpp"
#include <array>
#include <cstdint>

namespace cod2coop {
enum class ObjectiveState:uint8_t { NotStarted, Active, Completed, Failed, Blocked, WaitingForTeam };
// This controller records authorized engine outcomes. It never executes a second
// copy of campaign scripts, and does not pretend those engine callbacks exist.
class MissionState {
 bool host_;uint32_t last_event_=0;
 std::array<ObjectiveState,64> objectives_{};
 static bool transition(ObjectiveState old,ObjectiveState next){
  if(old==next||old==ObjectiveState::Completed||old==ObjectiveState::Failed)return false;
  if(old==ObjectiveState::NotStarted)return next==ObjectiveState::Active||next==ObjectiveState::Blocked;
  if(old==ObjectiveState::Blocked)return next==ObjectiveState::Active||next==ObjectiveState::Failed;
  return next!=ObjectiveState::NotStarted;
 }
public:
 explicit MissionState(bool host):host_(host){}
 bool set(uint8_t objective,ObjectiveState next){if(!host_||objective>=objectives_.size()||uint8_t(next)>5||!transition(objectives_[objective],next))return false;objectives_[objective]=next;return true;}
 ObjectiveState get(uint8_t objective)const{return objective<objectives_.size()?objectives_[objective]:ObjectiveState::Blocked;}
 net::Event event(uint8_t objective)const{net::Event e;e.type=net::EventType::Objective;e.data={objective,uint8_t(get(objective))};return e;}
 bool apply(const net::Event& e){if(host_||e.type!=net::EventType::Objective||e.data.size()!=2||e.data[0]>=objectives_.size()||e.data[1]>5||!e.id||!net::newer(e.id,last_event_))return false;auto state=ObjectiveState(e.data[1]);if(!transition(objectives_[e.data[0]],state))return false;objectives_[e.data[0]]=state;last_event_=e.id;return true;}
 void checkpoint_restart(){objectives_.fill(ObjectiveState::NotStarted);last_event_=0;}
};
}
