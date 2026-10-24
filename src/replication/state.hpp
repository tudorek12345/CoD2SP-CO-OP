#pragma once
#include "networking/protocol.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_set>
namespace cod2coop {
// IDs are never reused within a session and never derive from process pointers.
class EntityIds {
 uint32_t next_=1;std::unordered_set<uint32_t> live_;
public:
 uint32_t allocate(){if(next_==0)return 0;auto id=next_++;live_.insert(id);return id;}
 bool release(uint32_t id){return live_.erase(id)!=0;}
 bool contains(uint32_t id)const{return live_.count(id)!=0;}
 size_t size()const{return live_.size();}
 void clear(){live_.clear();} // keep high-water mark, including across reconnects
};
class SnapshotBuffer {
 net::Snapshot previous_{},latest_{};bool have_=false,have_previous_=false;
public:
 bool push(const net::Snapshot& snapshot){if(have_&&!net::newer(snapshot.tick,latest_.tick))return false;if(have_){previous_=latest_;have_previous_=true;}latest_=snapshot;have_=true;return true;}
 bool sample(float alpha,net::Snapshot& out)const{if(!have_||!std::isfinite(alpha))return false;out=latest_;if(!have_previous_)return true;alpha=std::clamp(alpha,0.f,1.f);for(size_t i=0;i<2;++i){if(previous_.players[i].entity!=latest_.players[i].entity)continue;auto& p=out.players[i];const auto& a=previous_.players[i];const auto& b=latest_.players[i];p.x=a.x+(b.x-a.x)*alpha;p.y=a.y+(b.y-a.y)*alpha;p.z=a.z+(b.z-a.z)*alpha;float delta=std::remainder(b.yaw-a.yaw,360.f);p.yaw=a.yaw+delta*alpha;}return true;}
};
}
