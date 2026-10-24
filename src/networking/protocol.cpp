#include "networking/protocol.hpp"
#include <cmath>
#include <cstring>
#include <limits>

namespace cod2coop::net {
namespace {
struct Writer {
 Bytes b;
 void u8(uint8_t n){b.push_back(n);} void u16(uint16_t n){u8(uint8_t(n>>8));u8(uint8_t(n));}
 void u32(uint32_t n){u16(uint16_t(n>>16));u16(uint16_t(n));} void u64(uint64_t n){u32(uint32_t(n>>32));u32(uint32_t(n));}
 void f(float n){uint32_t bits;std::memcpy(&bits,&n,4);u32(bits);}
 void str(const std::string& s){u8(uint8_t(s.size()));b.insert(b.end(),s.begin(),s.end());}
};
struct Reader {
 const Bytes& b;size_t i=0;bool ok=true;
 uint8_t u8(){if(i>=b.size()){ok=false;return 0;}return b[i++];}
 uint16_t u16(){uint16_t a=u8();return uint16_t(a<<8|u8());}
 uint32_t u32(){uint32_t a=u16();return a<<16|u16();}
 uint64_t u64(){uint64_t a=u32();return a<<32|u32();}
 float f(){uint32_t a=u32();float v;std::memcpy(&v,&a,4);if(!std::isfinite(v))ok=false;return v;}
 std::string str(size_t max){size_t n=u8();if(n>max||i+n>b.size()){ok=false;return{};}std::string s(b.begin()+i,b.begin()+i+n);i+=n;return s;}
 bool done()const{return ok&&i==b.size();}
};
void player(Writer& w,const Player& p){w.u32(p.entity);w.u8(p.slot);w.u8(p.stance);w.u16(p.weapon);w.f(p.x);w.f(p.y);w.f(p.z);w.f(p.yaw);w.f(p.pitch);w.f(p.vx);w.f(p.vy);w.f(p.vz);w.u16(uint16_t(p.health));}
Player player(Reader& r){Player p;p.entity=r.u32();p.slot=r.u8();p.stance=r.u8();p.weapon=r.u16();p.x=r.f();p.y=r.f();p.z=r.f();p.yaw=r.f();p.pitch=r.f();p.vx=r.f();p.vy=r.f();p.vz=r.f();p.health=int16_t(r.u16());if(p.slot>2||p.stance>2||p.health<0||p.health>10000)r.ok=false;return p;}
}
bool valid_fingerprint(const std::string& s){if(s.size()!=64)return false;for(char c:s)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool newer(uint32_t a,uint32_t b){return a!=b&&uint32_t(a-b)<0x80000000u;}
Bytes encode(const Packet& p){if(p.payload.size()>kMaxPacketBytes-kHeaderBytes)return{};Writer w;for(uint8_t c:std::array<uint8_t,8>{'C','2','C','O','O','P','1',0})w.u8(c);w.u16(kProtocolVersion);w.u16(uint16_t(p.header.kind));w.u16(uint16_t(p.payload.size()));w.u16(0);w.u64(p.header.session);w.u32(p.header.sequence);w.u32(p.header.ack);w.b.insert(w.b.end(),p.payload.begin(),p.payload.end());return w.b;}
bool decode(const uint8_t* data,size_t n,Packet& out,std::string& error){
 if(!data||n<kHeaderBytes||n>kMaxPacketBytes){error="packet size";return false;}Bytes b(data,data+n);Reader r{b};for(uint8_t c:std::array<uint8_t,8>{'C','2','C','O','O','P','1',0})if(r.u8()!=c){error="protocol magic";return false;}
 if(r.u16()!=kProtocolVersion){error="protocol version";return false;}auto kind=r.u16();auto size=r.u16();auto reserved=r.u16();if(kind<1||kind>uint16_t(Kind::WorldUpdate)||reserved||size!=n-kHeaderBytes){error="header fields";return false;}
 Packet p;p.header.kind=Kind(kind);p.header.session=r.u64();p.header.sequence=r.u32();p.header.ack=r.u32();p.payload.assign(b.begin()+kHeaderBytes,b.end());out=std::move(p);error.clear();return true;
}
Bytes serialize(const Hello& h){if(!valid_fingerprint(h.executable_sha256)||h.mission!="moscow"||h.soldier>1)return{};Writer w;w.str(h.executable_sha256);w.str(h.mission);w.u8(h.soldier);return w.b;}
bool deserialize(const Bytes& b,Hello& out){Reader r{b};Hello h;h.executable_sha256=r.str(64);h.mission=r.str(32);h.soldier=r.u8();if(!r.done()||!valid_fingerprint(h.executable_sha256)||h.mission!="moscow"||h.soldier>1)return false;out=std::move(h);return true;}
Bytes serialize(const Welcome& h){Writer w;w.u64(h.nonce);w.u8(h.slot);w.str(h.mission);return w.b;}
bool deserialize(const Bytes& b,Welcome& out){Reader r{b};Welcome h;h.nonce=r.u64();h.slot=r.u8();h.mission=r.str(32);if(!r.done()||h.slot!=2||h.mission!="moscow")return false;out=std::move(h);return true;}
Bytes serialize(const Input& p){Writer w;w.u32(p.command);w.u32(p.milliseconds);w.f(p.forward);w.f(p.right);w.f(p.up);w.f(p.yaw);w.f(p.pitch);w.u16(p.buttons);return w.b;}
bool deserialize(const Bytes& b,Input& out){Reader r{b};Input p;p.command=r.u32();p.milliseconds=r.u32();p.forward=r.f();p.right=r.f();p.up=r.f();p.yaw=r.f();p.pitch=r.f();p.buttons=r.u16();if(!r.done()||!p.command||p.milliseconds>250||std::abs(p.forward)>1||std::abs(p.right)>1||std::abs(p.up)>1||std::abs(p.pitch)>90||std::abs(p.yaw)>360||p.buttons>0xff)return false;out=p;return true;}
Bytes serialize(const Snapshot& p){Writer w;w.u32(p.tick);w.u32(p.last_command);for(const auto& a:p.players)player(w,a);return w.b;}
bool deserialize(const Bytes& b,Snapshot& out){Reader r{b};Snapshot p;p.tick=r.u32();p.last_command=r.u32();for(auto& a:p.players)a=player(r);const auto& second=p.players[1];bool absent=second.entity==0&&second.slot==0&&second.stance==0&&second.weapon==0&&second.health==0&&second.x==0&&second.y==0&&second.z==0&&second.yaw==0&&second.pitch==0&&second.vx==0&&second.vy==0&&second.vz==0;bool active=second.entity!=0&&second.slot==2&&second.entity!=p.players[0].entity;if(!r.done()||p.players[0].slot!=1||!p.players[0].entity||(!absent&&!active))return false;out=p;return true;}
Bytes serialize(const Event& p){if(p.data.size()>kMaxEventData)return{};Writer w;w.u32(p.id);w.u32(p.entity);w.u16(uint16_t(p.type));w.u16(uint16_t(p.data.size()));w.b.insert(w.b.end(),p.data.begin(),p.data.end());return w.b;}
bool deserialize(const Bytes& b,Event& out){Reader r{b};Event p;p.id=r.u32();p.entity=r.u32();auto type=r.u16();auto n=r.u16();if(!r.ok||!p.id||type<1||type>uint16_t(EventType::Checkpoint)||n>kMaxEventData||r.i+n!=b.size())return false;p.type=EventType(type);p.data.assign(b.begin()+r.i,b.end());out=std::move(p);return true;}
Bytes serialize(const WorldUpdate& update){
 if(!update.tick||!update.map_epoch||update.entities.size()>kMaxWorldEntities||!update.pages||update.pages>kMaxWorldPages||update.page>=update.pages)return{};
 Writer w;w.u32(update.tick);w.u32(update.map_epoch);w.u16(update.page);w.u16(update.pages);w.u16(uint16_t(update.entities.size()));w.u16(0);
 for(const auto& entity:update.entities){w.u32(entity.entity);w.u16(entity.native_slot);w.u16(entity.native_type);w.u16(entity.fields);w.f(entity.x);w.f(entity.y);w.f(entity.z);w.f(entity.pitch);w.f(entity.yaw);w.f(entity.roll);}return w.b;
}
bool deserialize(const Bytes& b,WorldUpdate& out){
 Reader r{b};WorldUpdate update;update.tick=r.u32();update.map_epoch=r.u32();update.page=r.u16();update.pages=r.u16();auto count=r.u16();auto reserved=r.u16();
 if(!r.ok||!update.tick||!update.map_epoch||!update.pages||update.pages>kMaxWorldPages||update.page>=update.pages||count>kMaxWorldEntities||reserved||b.size()!=16+size_t(count)*34)return false;
 update.entities.reserve(count);
 for(uint16_t i=0;i<count;++i){WorldEntity entity;entity.entity=r.u32();entity.native_slot=r.u16();entity.native_type=r.u16();entity.fields=r.u16();entity.x=r.f();entity.y=r.f();entity.z=r.f();entity.pitch=r.f();entity.yaw=r.f();entity.roll=r.f();if(!entity.entity||entity.native_slot>2045||entity.native_type>255||!entity.fields||(entity.fields&~3u)||(!(entity.fields&1)&&(entity.x!=0||entity.y!=0||entity.z!=0))||(!(entity.fields&2)&&(entity.pitch!=0||entity.yaw!=0||entity.roll!=0)))return false;for(const auto& prior:update.entities)if(prior.entity==entity.entity||prior.native_slot==entity.native_slot)return false;update.entities.push_back(entity);}
 if(!r.done())return false;out=std::move(update);return true;
}
}
