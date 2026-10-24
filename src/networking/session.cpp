#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include "networking/session.hpp"
#include <algorithm>
#include <bitset>
#include <chrono>
#include <deque>
#include <random>
#include <sstream>

namespace cod2coop::net {
uint64_t monotonic_milliseconds(){return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
const char* state_name(State s){switch(s){case State::Idle:return "idle";case State::Listening:return "listening";case State::Connecting:return "connecting";case State::Connected:return "connected";case State::Closed:return "closed";default:return "failed";}}
namespace {
uint64_t token(){std::random_device r;uint64_t n=(uint64_t(r())<<32)^r()^monotonic_milliseconds();return n?n:1;}
bool same(const sockaddr_in& a,const sockaddr_in& b){return a.sin_addr.s_addr==b.sin_addr.s_addr&&a.sin_port==b.sin_port;}
bool lan_address(const in_addr& address){const uint32_t a=ntohl(address.s_addr);return (a>>24)==127||(a>>24)==10||(a>>20)==0xac1||(a>>16)==0xc0a8||(a>>16)==0xa9fe;}
Bytes number(uint32_t n){return {uint8_t(n>>24),uint8_t(n>>16),uint8_t(n>>8),uint8_t(n)};}
uint32_t number(const Bytes& b){return uint32_t(b[0])<<24|uint32_t(b[1])<<16|uint32_t(b[2])<<8|b[3];}
}
struct Session::Impl {
 SOCKET socket=INVALID_SOCKET;bool wsa=false,host=false;State state=State::Idle;Config config;std::string message;
 sockaddr_in peer{};uint16_t port=0;uint64_t nonce=0,session=0,now=0,last_seen=0,last_hello=0,last_heartbeat=0,window=0;
 uint32_t sequence=1,last_input=0,last_snapshot=0,event_send=1,event_received=0,window_packets=0;
 uint32_t last_world_tick=0,last_world_epoch=0;uint16_t world_pages=0;std::bitset<kMaxWorldPages> received_world_pages;
 uint64_t last_event_sent=0;uint32_t pending_retries=0;Statistics stats;
 std::deque<Event> pending;std::vector<Input> inputs;std::vector<Event> events;std::optional<Snapshot> snapshot;bool resync=false;
 std::vector<WorldUpdate> world_updates;
 ~Impl(){close();}
 void close(){if(socket!=INVALID_SOCKET){closesocket(socket);socket=INVALID_SOCKET;}if(wsa){WSACleanup();wsa=false;}}
 bool open(const Config& c,bool h){
  close();config=c;host=h;message.clear();stats={};pending.clear();events.clear();inputs.clear();snapshot.reset();world_updates.clear();last_world_tick=0;world_pages=0;received_world_pages.reset();resync=false;state=State::Idle;
  sequence=1;last_input=last_snapshot=event_received=0;event_send=1;session=0;nonce=token();peer={};now=monotonic_milliseconds();last_seen=window=now;last_hello=last_heartbeat=0;window_packets=0;last_event_sent=0;pending_retries=0;
  if(!valid_fingerprint(c.executable_sha256)||c.soldier>1||c.timeout_ms<1000||c.timeout_ms>60000){message="invalid session configuration or executable fingerprint";state=State::Failed;return false;}
  WSADATA data{};if(WSAStartup(MAKEWORD(2,2),&data)){message="WSAStartup failed";state=State::Failed;return false;}wsa=true;
  socket=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);if(socket==INVALID_SOCKET){message="UDP socket creation failed";state=State::Failed;return false;}
  u_long mode=1;if(ioctlsocket(socket,FIONBIO,&mode)){message="nonblocking mode failed";state=State::Failed;return false;}
  sockaddr_in address{};address.sin_family=AF_INET;address.sin_port=htons(h?c.port:0);
  if(inet_pton(AF_INET,c.bind_address.c_str(),&address.sin_addr)!=1||bind(socket,reinterpret_cast<sockaddr*>(&address),sizeof address)){message="invalid bind address or port already occupied";state=State::Failed;return false;}
  int n=sizeof address;if(getsockname(socket,reinterpret_cast<sockaddr*>(&address),&n)){message="getsockname failed";state=State::Failed;return false;}port=ntohs(address.sin_port);return true;
 }
 bool send(Kind kind,const Bytes& payload,const sockaddr_in& to,uint64_t id,uint32_t ack=0){
  Packet p{{kind,id,sequence++,ack},payload};auto b=encode(p);if(b.empty())return false;int n=sendto(socket,reinterpret_cast<const char*>(b.data()),int(b.size()),0,reinterpret_cast<const sockaddr*>(&to),sizeof to);if(n!=int(b.size()))return false;++stats.sent_packets;stats.sent_bytes+=b.size();return true;
 }
 bool send(Kind kind,const Bytes& payload={},uint32_t ack=0){return send(kind,payload,peer,session,ack);}
 void welcome(uint64_t client_nonce){send(Kind::Welcome,serialize(Welcome{client_nonce,2,"moscow"}));}
 void clear_peer(const std::string& why){session=0;pending.clear();inputs.clear();events.clear();snapshot.reset();world_updates.clear();last_world_tick=0;world_pages=0;received_world_pages.reset();resync=false;event_send=1;event_received=last_input=last_snapshot=0;last_event_sent=0;pending_retries=0;message=why;state=host?State::Listening:State::Closed;}
 void on_packet(const Packet& packet,const sockaddr_in& from){
  if(!lan_address(from.sin_addr)){++stats.rejected_packets;return;}
  const auto& h=packet.header;
  if(h.kind==Kind::Hello&&host){
   Hello hello;if(!h.session||!deserialize(packet.payload,hello)){++stats.rejected_packets;return;}
   auto reject=[&](const std::string& reason){send(Kind::Reject,Bytes(reason.begin(),reason.end()),from,h.session);};
   if(hello.executable_sha256!=config.executable_sha256){reject("executable mismatch");++stats.rejected_packets;return;}
   if(state==State::Connected){if(same(from,peer)&&h.session==nonce)welcome(nonce);else reject("session full (2/2)");return;}
   if(state!=State::Listening)return;
   peer=from;nonce=h.session;session=token();state=State::Connected;last_seen=now;message="client connected, slot 2";welcome(nonce);return;
  }
  if(!same(from,peer)){++stats.rejected_packets;return;}
  if(!host&&state==State::Connecting){
   if(h.kind==Kind::Reject&&h.session==nonce&&packet.payload.size()<=128){message=std::string(packet.payload.begin(),packet.payload.end());state=State::Failed;return;}
   Welcome welcome_data;if(h.kind!=Kind::Welcome||!h.session||!deserialize(packet.payload,welcome_data)||welcome_data.nonce!=nonce){++stats.rejected_packets;return;}
   session=h.session;state=State::Connected;last_seen=now;message="connected, slot 2";return;
  }
  if(state!=State::Connected||h.session!=session){++stats.rejected_packets;return;}
  bool valid=false;
  switch(h.kind){
   case Kind::Input:{Input input;if(!host||!deserialize(packet.payload,input))break;if(newer(input.command,last_input)){last_input=input.command;if(inputs.size()<128){inputs.push_back(input);++stats.inputs;}else ++stats.rate_limited;}else ++stats.stale_packets;valid=true;break;}
   case Kind::Snapshot:{Snapshot incoming;if(host||!deserialize(packet.payload,incoming))break;if(newer(incoming.tick,last_snapshot)){last_snapshot=incoming.tick;snapshot=incoming;++stats.snapshots;}else ++stats.stale_packets;valid=true;break;}
   case Kind::WorldUpdate:{
    WorldUpdate incoming;if(host||!deserialize(packet.payload,incoming))break;
    if(newer(incoming.tick,last_world_tick)){last_world_tick=incoming.tick;last_world_epoch=incoming.map_epoch;world_pages=incoming.pages;received_world_pages.reset();}
    else if(incoming.tick!=last_world_tick){++stats.stale_packets;valid=true;break;}
    if(incoming.pages!=world_pages||incoming.map_epoch!=last_world_epoch)break;
    if(received_world_pages.test(incoming.page)){++stats.stale_packets;valid=true;break;}
    received_world_pages.set(incoming.page);if(world_updates.size()>=128){world_updates.erase(world_updates.begin());++stats.rate_limited;}
    ++stats.world_updates;stats.world_entities+=incoming.entities.size();world_updates.push_back(std::move(incoming));valid=true;break;
   }
   case Kind::Event:{Event incoming;if(host||!deserialize(packet.payload,incoming))break;if(incoming.id==event_received+1){if(events.size()>=128){++stats.rate_limited;valid=true;break;}event_received=incoming.id;events.push_back(std::move(incoming));++stats.events;}else if(!newer(incoming.id,event_received))++stats.duplicate_events;send(Kind::Ack,{},event_received);valid=true;break;}
   case Kind::Ack:if(host&&packet.payload.empty()){if(!pending.empty()&&h.ack==pending.front().id){stats.round_trip_ms=uint32_t(std::min<uint64_t>(now-last_event_sent,60000));pending.pop_front();last_event_sent=0;pending_retries=0;}valid=true;}break;
   case Kind::Heartbeat:if(packet.payload.size()==4){send(Kind::Resync,packet.payload);valid=true;}break;
   // A four-byte Resync is a heartbeat pong; empty Resync requests a host world refresh.
   case Kind::Resync:if(packet.payload.empty()&&host){resync=true;valid=true;}else if(packet.payload.size()==4){uint32_t sent=number(packet.payload);uint32_t elapsed=uint32_t(now)-sent;if(elapsed<=config.timeout_ms)stats.round_trip_ms=elapsed;valid=true;}break;
   case Kind::Disconnect:if(packet.payload.empty()){clear_peer("peer disconnected");valid=true;}break;
   default:break;
  }
  if(valid)last_seen=now;else ++stats.rejected_packets;
 }
};
Session::Session():p_(std::make_unique<Impl>()){}Session::~Session()=default;
bool Session::host(const Config& config){if(!p_->open(config,true))return false;p_->state=State::Listening;p_->message="listening, slot 1 reserved for host";return true;}
bool Session::join(const Config& config,const std::string& address){if(!config.port){p_->state=State::Failed;p_->message="host port must be nonzero";return false;}if(!p_->open(config,false))return false;p_->peer.sin_family=AF_INET;p_->peer.sin_port=htons(config.port);if(inet_pton(AF_INET,address.c_str(),&p_->peer.sin_addr)!=1||!lan_address(p_->peer.sin_addr)){p_->state=State::Failed;p_->message="host must be a loopback, private LAN, or link-local IPv4 address";return false;}p_->state=State::Connecting;p_->message="connecting";return true;}
void Session::tick(uint64_t now){
 auto& p=*p_;if(p.socket==INVALID_SOCKET||p.state==State::Failed||p.state==State::Closed)return;if(now<p.now)return;p.now=now;
 if(now-p.window>=1000){p.window=now;p.window_packets=0;}
 for(int i=0;i<64;++i){uint8_t bytes[kMaxPacketBytes+1];sockaddr_in from{};int size=sizeof from;int n=recvfrom(p.socket,reinterpret_cast<char*>(bytes),sizeof bytes,0,reinterpret_cast<sockaddr*>(&from),&size);if(n==SOCKET_ERROR){int error=WSAGetLastError();if(error==WSAEWOULDBLOCK)break;if(error==WSAEMSGSIZE){++p.stats.rejected_packets;continue;}if(error==WSAECONNRESET)continue;p.message="UDP receive failed: "+std::to_string(error);p.state=State::Failed;break;}
  ++p.stats.received_packets;p.stats.received_bytes+=size_t(n);if(++p.window_packets>240){++p.stats.rate_limited;continue;}Packet packet;std::string error;if(!decode(bytes,size_t(n),packet,error)){++p.stats.rejected_packets;continue;}p.on_packet(packet,from);
 }
 if(p.state==State::Connecting){if(now-p.last_seen>p.config.timeout_ms){p.state=State::Failed;p.message="connection timed out";return;}if(!p.last_hello||now-p.last_hello>=400){p.send(Kind::Hello,serialize(Hello{p.config.executable_sha256,"moscow",p.config.soldier}),p.peer,p.nonce);p.last_hello=now;}}
 if(p.state!=State::Connected)return;
 if(now-p.last_seen>p.config.timeout_ms){p.clear_peer("peer timed out");return;}
 if(!p.last_heartbeat||now-p.last_heartbeat>=500){p.send(Kind::Heartbeat,number(uint32_t(now)));p.last_heartbeat=now;}
 if(p.host&&!p.pending.empty()&&(!p.last_event_sent||now-p.last_event_sent>=150)){
  if(p.pending_retries>=32){p.clear_peer("reliable event acknowledgement timed out");return;}
  if(p.last_event_sent){++p.stats.retries;++p.pending_retries;}p.send(Kind::Event,serialize(p.pending.front()));p.last_event_sent=now;
 }
}
bool Session::send_input(const Input& input){Input validated;if(p_->host||p_->state!=State::Connected||!deserialize(serialize(input),validated))return false;return p_->send(Kind::Input,serialize(input));}
bool Session::send_snapshot(const Snapshot& snapshot){Snapshot validated;if(!p_->host||p_->state!=State::Connected||!deserialize(serialize(snapshot),validated))return false;return p_->send(Kind::Snapshot,serialize(snapshot));}
bool Session::send_world_update(const WorldUpdate& update){WorldUpdate validated;auto payload=serialize(update);if(!p_->host||p_->state!=State::Connected||!deserialize(payload,validated))return false;return p_->send(Kind::WorldUpdate,payload);}
bool Session::send_event(Event event){if(!p_->host||p_->state!=State::Connected||p_->pending.size()>=64||!p_->event_send)return false;event.id=p_->event_send;Event validated;auto payload=serialize(event);if(!deserialize(payload,validated))return false;++p_->event_send;p_->pending.push_back(std::move(event));return true;}
bool Session::request_resync(){return !p_->host&&p_->state==State::Connected&&p_->send(Kind::Resync);}
void Session::disconnect(){if(p_->state==State::Connected)p_->send(Kind::Disconnect);p_->clear_peer("local disconnect");p_->state=State::Closed;p_->close();}
State Session::state()const{return p_->state;}bool Session::is_host()const{return p_->host;}uint16_t Session::local_port()const{return p_->port;}
std::string Session::peer_address()const{if(p_->state!=State::Connected)return{};char text[INET_ADDRSTRLEN]{};inet_ntop(AF_INET,&p_->peer.sin_addr,text,sizeof text);return std::string(text)+":"+std::to_string(ntohs(p_->peer.sin_port));}
std::string Session::status()const{return p_->message;}Statistics Session::statistics()const{return p_->stats;}
std::vector<Input> Session::drain_inputs(){auto out=std::move(p_->inputs);p_->inputs.clear();return out;}std::vector<Event> Session::drain_events(){auto out=std::move(p_->events);p_->events.clear();return out;}
std::optional<Snapshot> Session::take_snapshot(){auto out=p_->snapshot;p_->snapshot.reset();return out;}bool Session::take_resync_request(){bool out=p_->resync;p_->resync=false;return out;}
std::vector<WorldUpdate> Session::drain_world_updates(){auto out=std::move(p_->world_updates);p_->world_updates.clear();return out;}
}
