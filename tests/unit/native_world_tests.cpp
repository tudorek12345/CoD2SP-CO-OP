#include "replication/native_world.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <vector>

namespace {
using cod2coop::NativeWorldTracker;
using cod2coop::NativeWorldError;
int checks=0;
void check(bool yes,const char* description){++checks;if(!yes){std::cerr<<"FAIL "<<description<<'\n';std::exit(1);}}
void fill(std::vector<uint8_t>& data,size_t slot,bool active,float x,float y,float z,uint32_t client=0,uint8_t type=0){
 auto* entry=data.data()+slot*NativeWorldTracker::kEntityStride;entry[0xa4]=active?1:0;entry[0]=type;
 const float position[]={x,y,z};std::memcpy(entry+0xdc,position,sizeof(position));std::memcpy(entry+0xfc,&client,sizeof(client));
}
}
int main(){
 using namespace cod2coop;
 NativeWorldTracker tracker;
 auto empty=tracker.observe(nullptr,0,0,0,1);
 check(empty.valid&&!empty.map_reset&&empty.entities.empty(),"empty startup is a valid observation");
 auto pages=native_world_pages(empty);check(pages.size()==1&&pages[0].entities.empty()&&pages[0].pages==1,"empty world has a complete empty page");
 std::vector<uint8_t> data(4*NativeWorldTracker::kEntityStride);
 fill(data,0,true,10,20,30,0xaaaa0000,1);fill(data,1,true,40,50,60,0,2);fill(data,2,true,70,80,90,0,255);
 auto a=tracker.observe(data.data(),data.size(),4,1,2);
 check(a.valid&&a.active_slots==3&&a.entities.size()==3&&a.created_slots==3,"active source slots observed");
 check(a.entities[0].entity==1&&a.entities[1].entity==3&&a.entities[2].entity==4,"reserved player and independent dynamic IDs");
 check(a.entities[0].native_slot==0&&a.entities[1].native_slot==1&&a.entities[2].native_slot==2,"bounded template slot hints remain separate from identity");
 check(a.entities[0].x==10&&a.entities[0].y==20&&a.entities[0].z==30&&a.entities[2].native_type==255,"actual source position and byte type retained");
 for(const auto& e:a.entities)check(e.fields==1&&e.pitch==0&&e.yaw==0&&e.roll==0,"unknown rotations are unmarked zeroes");
 const auto first_npc=a.entities[1].entity,second_npc=a.entities[2].entity;
 auto b=tracker.observe(data.data(),data.size(),4,1,3);check(b.entities[1].entity==first_npc&&b.created_slots==0,"stable active slot preserves identity");
 fill(data,2,false,70,80,90);auto removed=tracker.observe(data.data(),data.size(),4,1,4);check(removed.retired_slots==1&&removed.entities.size()==2,"sampled inactive transition retires slot");
 fill(data,2,true,71,81,91);auto recreated=tracker.observe(data.data(),data.size(),4,1,5);check(recreated.entities[2].entity>second_npc&&recreated.created_slots==1,"reused native slot gets a fresh dynamic ID");
 auto unchanged_id=recreated.entities[2].entity;
 auto bad=tracker.observe(data.data(),data.size()-1,4,1,6);check(!bad.valid&&bad.error==NativeWorldError::TruncatedBuffer,"truncated source rejected");
 bad=tracker.observe(nullptr,data.size(),4,1,6);check(!bad.valid&&bad.error==NativeWorldError::MissingBuffer,"missing source rejected");
 bad=tracker.observe(data.data(),data.size(),2047,1,6);check(!bad.valid&&bad.error==NativeWorldError::TooManySlots,"oversize slot count rejected before reading");
 bad=tracker.observe(data.data(),data.size(),4,3,6);check(!bad.valid&&bad.error==NativeWorldError::InvalidClientCapacity,"unsupported client capacity rejected");
 bad=tracker.observe(data.data(),data.size(),4,0,6);check(!bad.valid,"nonempty world requires client capacity");
 bad=tracker.observe(data.data(),data.size(),4,1,0);check(!bad.valid&&bad.error==NativeWorldError::InvalidTick,"wire-invalid zero tick rejected");
 auto after_bad=tracker.observe(data.data(),data.size(),4,1,7);check(after_bad.entities[2].entity==unchanged_id&&after_bad.created_slots==0,"bad samples do not reset lifecycle tracking");
 fill(data,1,true,40,50,60,0xbbbb0000,2);auto two=tracker.observe(data.data(),data.size(),4,2,8);check(two.entities[1].entity==2&&two.retired_slots==1&&two.created_slots==1,"verified second client role uses reserved ID2");
 fill(data,1,true,40,50,60,0,2);auto npc_again=tracker.observe(data.data(),data.size(),4,1,9);check(npc_again.entities[1].entity>unchanged_id&&npc_again.entities[1].entity!=first_npc,"player-to-map-slot transition gets fresh dynamic identity");
 auto before_shrink=npc_again.entities[2].entity;auto shrink=tracker.observe(data.data(),data.size(),2,1,10);check(shrink.retired_slots==1,"slots outside new source count retire");
 auto grow=tracker.observe(data.data(),data.size(),4,1,11);check(grow.entities[2].entity>before_shrink,"regrown source slot cannot reuse stale identity");
 fill(data,0,true,std::numeric_limits<float>::quiet_NaN(),0,0,0xaaaa0000,1);fill(data,1,true,std::numeric_limits<float>::infinity(),0,0);fill(data,2,true,NativeWorldTracker::kMaxCoordinate+1,0,0);
 auto invalid=tracker.observe(data.data(),data.size(),4,1,12);check(invalid.valid&&invalid.active_slots==3&&invalid.invalid_positions==3&&invalid.entities.empty(),"nonfinite and implausible coordinates never emitted");
 fill(data,0,true,-NativeWorldTracker::kMaxCoordinate,NativeWorldTracker::kMaxCoordinate,0,0xaaaa0000,1);
 auto edge=tracker.observe(data.data(),data.size(),4,1,13);check(edge.entities.size()==1&&edge.entities[0].entity==1,"coordinate bounds inclusive and active identity survives invalid position");
 auto reset=tracker.observe(nullptr,0,0,0,14);check(reset.valid&&reset.map_reset&&reset.retired_slots==3&&reset.map_epoch==2,"zero-count reset retires all live slots and increments epoch");
 auto reset_again=tracker.observe(nullptr,0,0,0,15);check(!reset_again.map_reset&&reset_again.map_epoch==2,"repeated empty samples do not repeatedly reset map");
 fill(data,0,true,1,2,3,0xaaaa0000,1);fill(data,1,true,4,5,6);fill(data,2,true,7,8,9);
 auto new_map=tracker.observe(data.data(),data.size(),4,1,16);check(new_map.entities[0].entity==1&&new_map.entities[1].entity>grow.entities[2].entity,"dynamic identity high-water mark survives map reset");
 // Maximum-size source: every active slot is tracked, even those omitted from
 // this bounded observation, so their generations cannot alias selected IDs.
 NativeWorldTracker full;std::vector<uint8_t> many(NativeWorldTracker::kMaxSlots*NativeWorldTracker::kEntityStride);
 for(size_t i=0;i<NativeWorldTracker::kMaxSlots;++i)fill(many,i,true,static_cast<float>(i),-static_cast<float>(i),3,i<2?0x1000u+static_cast<uint32_t>(i):0,static_cast<uint8_t>(i%256));
 auto maximum=full.observe(many.data(),many.size(),static_cast<uint32_t>(NativeWorldTracker::kMaxSlots),2,1);
 check(maximum.valid&&maximum.active_slots==2046&&maximum.entities.size()==1280&&maximum.omitted_entities==766,"selection and scanning bounds enforced separately");
 check(maximum.entities[0].entity==1&&maximum.entities[1].entity==2,"both actual client slots prioritized by native slot order");
 std::set<uint32_t> unique;for(const auto& e:maximum.entities)check(unique.insert(e.entity).second,"all selected IDs unique");
 pages=native_world_pages(maximum);check(pages.size()==64,"maximum world uses bounded 64 pages");size_t seen=0;
 for(size_t i=0;i<pages.size();++i){const auto& page=pages[i];check(page.page==i&&page.pages==64&&page.entities.size()==20&&page.map_epoch==maximum.map_epoch,"page metadata and epoch limits");net::WorldUpdate decoded;auto bytes=net::serialize(page);check(bytes.size()+net::kHeaderBytes<=net::kMaxPacketBytes&&net::deserialize(bytes,decoded),"actual protocol accepts every produced page");seen+=decoded.entities.size();}
 check(seen==1280,"pagination emits each selected record exactly once");
 for(size_t i=2;i<NativeWorldTracker::kMaxSlots;++i)many[i*NativeWorldTracker::kEntityStride+0xa4]=0;
 full.observe(many.data(),many.size(),static_cast<uint32_t>(NativeWorldTracker::kMaxSlots),2,2);
 for(size_t i=2;i<NativeWorldTracker::kMaxSlots;++i)many[i*NativeWorldTracker::kEntityStride+0xa4]=1;
 auto generation=full.observe(many.data(),many.size(),static_cast<uint32_t>(NativeWorldTracker::kMaxSlots),2,3);
 check(generation.entities[2].entity>2046,"generations advance past all tracked omitted slots");
 std::vector<uint8_t> unaligned(data.size()+1);std::copy(data.begin(),data.end(),unaligned.begin()+1);NativeWorldTracker unaligned_tracker;auto unaligned_observation=unaligned_tracker.observe(unaligned.data()+1,data.size(),4,1,1);check(unaligned_observation.valid&&unaligned_observation.entities[0].x==1,"unaligned copied buffers supported without struct casts");
 NativeWorldTracker partial;std::vector<uint8_t> twenty_one(21*NativeWorldTracker::kEntityStride);for(size_t i=0;i<21;++i)fill(twenty_one,i,true,1,2,3);
 auto split=partial.observe(twenty_one.data(),twenty_one.size(),21,1,1);pages=native_world_pages(split);check(pages.size()==2&&pages[0].entities.size()==20&&pages[1].entities.size()==1,"partial final page handled");
 check(native_world_pages(bad).empty(),"invalid observations cannot produce wire pages");
 std::cout<<"PASS "<<checks<<" native-world buffer/lifecycle/bounds/pagination checks; fixtures only, no native entity application\n";
}
