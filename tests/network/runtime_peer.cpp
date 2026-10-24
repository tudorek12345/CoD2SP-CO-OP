#include "networking/session.hpp"
#include "compatibility/platform.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <thread>

namespace {
std::string json(const std::string& value){std::string out="\"";const char* digits="0123456789abcdef";for(unsigned char c:value){if(c=='"'||c=='\\'){out+='\\';out+=c;}else if(c<32){out+="\\u00";out+=digits[c>>4];out+=digits[c&15];}else out+=c;}return out+'"';}
void player_json(std::ostream& out,const cod2coop::net::Snapshot& snapshot){const auto& p=snapshot.players[0];out<<"{\"tick\":"<<snapshot.tick<<",\"entity\":"<<p.entity<<",\"slot\":"<<unsigned(p.slot)<<",\"position\":["<<p.x<<","<<p.y<<","<<p.z<<"],\"second_player_present\":"<<(snapshot.players[1].entity?"true":"false")<<"}";}
void entity_json(std::ostream& out,const cod2coop::net::WorldEntity& entity){out<<"{\"entity\":"<<entity.entity<<",\"native_type\":"<<entity.native_type<<",\"fields\":"<<entity.fields<<",\"position\":["<<entity.x<<","<<entity.y<<","<<entity.z<<"],\"rotation\":["<<entity.pitch<<","<<entity.yaw<<","<<entity.roll<<"]}";}
void second_json(std::ostream& out,const cod2coop::net::Player& player){out<<"{\"entity\":"<<player.entity<<",\"slot\":"<<unsigned(player.slot)<<",\"position\":["<<player.x<<","<<player.y<<","<<player.z<<"]}";}
}
int main(int argc,char** argv){
 using namespace cod2coop::net;
 if(argc<4){std::cerr<<"usage: COD2CoopRuntimePeer private-host-ip port output.json [connected-duration-ms] [local-bind-ip] [--require-snapshots] [--require-world-updates] [--drive-player2-for-seconds 1..3]\nDefault sends no inputs/events. Drive mode requires native capacity2 and two client-backed actors verified separately; it sends forward0.25 only after observing player2 ID2, then repeated stop commands.\n";return 2;}
 unsigned long port=0,duration=5000,drive_seconds=0;bool require_snapshots=false,require_world=false,have_duration=false,have_bind=false;std::string bind="127.0.0.1";
 try{size_t end=0;port=std::stoul(argv[2],&end);if(end!=std::string(argv[2]).size())return 2;for(int i=4;i<argc;++i){std::string arg=argv[i];if(arg=="--require-snapshots")require_snapshots=true;else if(arg=="--require-world-updates")require_world=true;else if(arg=="--drive-player2-for-seconds"){if(++i>=argc)return 2;std::string seconds=argv[i];drive_seconds=std::stoul(seconds,&end);if(end!=seconds.size()||drive_seconds<1||drive_seconds>3)return 2;require_snapshots=true;}else if(arg.rfind("--",0)==0)return 2;else if(!have_duration){duration=std::stoul(arg,&end);if(end!=arg.size())return 2;have_duration=true;}else if(!have_bind){bind=arg;have_bind=true;}else return 2;}}catch(...){return 2;}
 if(port<1||port>65535||duration<1000||duration>30000||(drive_seconds&&duration<drive_seconds*1000+1000))return 2;
 Config config;config.port=uint16_t(port);config.executable_sha256=coop::sp_hash;config.bind_address=bind;Session session;
 bool started=session.join(config,argv[1]),connected=false,pass=false,have_snapshot=false,have_entity=false,position_changed=false,second_player_observed=false;uint64_t began=monotonic_milliseconds(),connection_time=0;std::string reason=started?"connection did not complete":session.status();
 Snapshot first_snapshot{},last_snapshot{};WorldEntity first_entity{};std::set<uint32_t> entity_ids;uint64_t sampled_snapshots=0,world_pages=0;
 Player first_second{},last_second{};bool have_second=false,second_entity_changed=false;uint64_t drive_started=0,last_input_sent=0,last_second_seen=0;uint32_t input_command=0,drive_commands=0,stop_commands=0;
 while(started&&monotonic_milliseconds()-began<duration+7000){
  uint64_t now=monotonic_milliseconds();session.tick(now);
  if(auto snapshot=session.take_snapshot()){if(!have_snapshot){first_snapshot=*snapshot;have_snapshot=true;}else{const auto& previous=last_snapshot.players[0];const auto& current=snapshot->players[0];if(previous.x!=current.x||previous.y!=current.y||previous.z!=current.z)position_changed=true;}second_player_observed=second_player_observed||snapshot->players[1].entity!=0;const auto& second=snapshot->players[1];if(second.entity&&second.slot==2){if(!have_second){first_second=second;have_second=true;}else if(second.entity!=first_second.entity)second_entity_changed=true;last_second=second;last_second_seen=now;}last_snapshot=*snapshot;++sampled_snapshots;}
  for(const auto& update:session.drain_world_updates()){++world_pages;for(const auto& entity:update.entities){if(!have_entity){first_entity=entity;have_entity=true;}if(entity_ids.size()<4096)entity_ids.insert(entity.entity);}}
  if(session.state()==State::Connected){if(!connected){connected=true;connection_time=now;}
   if(drive_seconds&&now-last_input_sent>=30){
    bool eligible=have_second&&first_second.entity==2&&!second_entity_changed&&now-last_second_seen<500&&last_snapshot.players[1].entity==2;
    if(!drive_started&&eligible)drive_started=now;
    if(drive_started){bool moving=eligible&&now-drive_started<drive_seconds*1000;Input input;input.command=++input_command;input.milliseconds=30;input.forward=moving?.25f:0.f;if(session.send_input(input)){if(moving)++drive_commands;else ++stop_commands;}last_input_sent=now;}
   }
   if(now-connection_time>=duration){auto stats=session.statistics();float dx=last_second.x-first_second.x,dy=last_second.y-first_second.y;bool drive_ok=!drive_seconds||(have_second&&first_second.entity==2&&last_second.entity==2&&last_snapshot.players[1].entity==2&&now-last_second_seen<500&&!second_entity_changed&&drive_commands>=drive_seconds*1000/30*8/10&&stop_commands>=3&&std::sqrt(dx*dx+dy*dy)>1.f);pass=stats.received_packets>=4&&stats.sent_packets>=4&&(!require_snapshots||(stats.snapshots>=10&&have_snapshot))&&(!require_world||(stats.world_updates>=1&&have_entity))&&drive_ok;reason=pass?"original SP runtime connection and requested telemetry/control observations verified":"connection succeeded but requested telemetry or player2 movement evidence was not received";break;}}
  else if(session.state()==State::Failed||session.state()==State::Closed){reason=session.status();break;}
  std::this_thread::sleep_for(std::chrono::milliseconds(5));
 }
 auto stats=session.statistics();auto peer=session.peer_address();auto local=session.local_port();session.disconnect();
 std::ofstream out(argv[3]);if(!out){std::cerr<<"Could not write evidence\n";return 1;}out<<std::setprecision(9);
 out<<"{\n  \"test\": \"original SP runtime connection and observed native-state transport; no client engine application\",\n  \"pass\": "<<(pass?"true":"false")<<",\n  \"connected\": "<<(connected?"true":"false")<<",\n  \"require_snapshots\": "<<(require_snapshots?"true":"false")<<", \"require_world_updates\": "<<(require_world?"true":"false")<<",\n  \"executable_sha256\": "<<json(config.executable_sha256)<<",\n  \"peer\": "<<json(peer)<<",\n  \"local_port\": "<<local<<",\n  \"duration_ms\": "<<(connected?monotonic_milliseconds()-connection_time:0)<<",\n  \"sent_packets\": "<<stats.sent_packets<<", \"received_packets\": "<<stats.received_packets<<",\n  \"sent_bytes\": "<<stats.sent_bytes<<", \"received_bytes\": "<<stats.received_bytes<<",\n  \"round_trip_ms\": "<<stats.round_trip_ms<<",\n  \"snapshots_received\": "<<stats.snapshots<<", \"snapshot_samples_recorded\": "<<sampled_snapshots<<",\n  \"first_host_snapshot\": ";if(have_snapshot)player_json(out,first_snapshot);else out<<"null";out<<",\n  \"last_host_snapshot\": ";if(have_snapshot)player_json(out,last_snapshot);else out<<"null";
 out<<",\n  \"host_position_changed_during_capture\": "<<(position_changed?"true":"false")<<", \"second_player_observed\": "<<(second_player_observed?"true":"false");
 out<<",\n  \"world_updates_received\": "<<stats.world_updates<<", \"world_pages_drained\": "<<world_pages<<", \"world_entity_observations\": "<<stats.world_entities<<", \"unique_world_entity_ids\": "<<entity_ids.size()<<",\n  \"first_world_entity\": ";if(have_entity)entity_json(out,first_entity);else out<<"null";
 out<<",\n  \"drive_player2_requested_seconds\": "<<drive_seconds<<", \"drive_commands_sent\": "<<drive_commands<<", \"stop_commands_sent\": "<<stop_commands<<",\n  \"first_second_player\": ";if(have_second)second_json(out,first_second);else out<<"null";out<<",\n  \"last_second_player\": ";if(have_second)second_json(out,last_second);else out<<"null";float dx=last_second.x-first_second.x,dy=last_second.y-first_second.y,dz=last_second.z-first_second.z;out<<",\n  \"second_player_distance\": "<<(have_second?std::sqrt(dx*dx+dy*dy+dz*dz):0.f)<<", \"second_player_horizontal_distance\": "<<(have_second?std::sqrt(dx*dx+dy*dy):0.f)<<", \"second_player_entity_changed\": "<<(second_entity_changed?"true":"false");
 out<<",\n  \"player_inputs_sent\": "<<drive_commands+stop_commands<<", \"snapshots_sent\": 0, \"gameplay_events_sent\": 0,\n  \"reason\": "<<json(reason)<<"\n}\n";
 std::cout<<(pass?"PASS ":"FAIL ")<<reason<<"; received "<<stats.snapshots<<" snapshots, "<<stats.world_updates<<" world pages\n";return pass?0:1;
}
