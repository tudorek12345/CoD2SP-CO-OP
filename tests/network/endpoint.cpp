#include "networking/session.hpp"
#include "missions/mission_state.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>

using namespace cod2coop::net;
namespace {
int finish(const std::string& path,bool pass,const std::string& role,const Session& session,int inputs,int snapshots,int events,const std::string& reason){
 auto stats=session.statistics();std::ofstream f(path);f<<"{\n  \"test\": \"two-process UDP fixture transport (not engine gameplay)\",\n  \"role\": \""<<role<<"\",\n  \"pass\": "<<(pass?"true":"false")<<",\n  \"reason\": \""<<reason<<"\",\n  \"inputs\": "<<inputs<<", \"snapshots\": "<<snapshots<<", \"events\": "<<events<<",\n  \"sent_packets\": "<<stats.sent_packets<<", \"received_packets\": "<<stats.received_packets<<",\n  \"reliable_retries\": "<<stats.retries<<", \"duplicate_events\": "<<stats.duplicate_events<<", \"rejected_packets\": "<<stats.rejected_packets<<",\n  \"local_port\": "<<session.local_port()<<", \"round_trip_ms\": "<<stats.round_trip_ms<<"\n}\n";std::cout<<(pass?"PASS ":"FAIL ")<<role<<": "<<reason<<"\n";return pass?0:1;
}
}
int main(int argc,char** argv){
 if(argc<4){std::cerr<<"usage: COD2CoopNetworkEndpoint host|client port result.json [peer-ip] [bind-ip]\n";return 2;}
 std::string role=argv[1],output=argv[3];int port=0;try{port=std::stoi(argv[2]);}catch(...){return 2;}if(port<1||port>65535||(role!="host"&&role!="client"))return 2;
 Config config;config.port=uint16_t(port);config.executable_sha256=std::string(64,'a');config.bind_address=argc>5?argv[5]:"127.0.0.1";Session session;
 bool started=role=="host"?session.host(config):session.join(config,argc>4?argv[4]:"127.0.0.1");if(!started)return finish(output,false,role,session,0,0,0,session.status());
 auto start=monotonic_milliseconds();uint64_t last_send=0;uint32_t command=0,tick=0;int inputs=0,snapshots=0,events=0;bool queued=false;cod2coop::MissionState mirror(false);
 while(monotonic_milliseconds()-start<10000){auto now=monotonic_milliseconds();session.tick(now);
  if(session.state()==State::Failed)return finish(output,false,role,session,inputs,snapshots,events,session.status());
  if(role=="host"){
   for(const auto& input:session.drain_inputs()){++inputs;command=input.command;}
   if(session.state()==State::Connected&&now-last_send>=30){Snapshot s;s.tick=++tick;s.last_command=command;s.players[0].entity=1;s.players[0].slot=1;s.players[0].x=float(tick);s.players[1].entity=2;s.players[1].slot=2;s.players[1].x=float(command);session.send_snapshot(s);++snapshots;last_send=now;}
   if(session.state()==State::Connected&&!queued){Event e;e.type=EventType::Objective;e.data={0,uint8_t(cod2coop::ObjectiveState::Active)};session.send_event(e);e.data[1]=uint8_t(cod2coop::ObjectiveState::Completed);session.send_event(e);queued=true;}
   if(queued&&session.state()==State::Listening&&inputs>=10)return finish(output,true,role,session,inputs,snapshots,2,"received client inputs and clean disconnect");
  }else{
   if(session.state()==State::Connected&&now-last_send>=30){Input input;input.command=++command;input.milliseconds=30;input.forward=.5f;session.send_input(input);++inputs;last_send=now;}
   if(auto snapshot=session.take_snapshot()){if(snapshot->players[0].entity!=1||snapshot->players[1].entity!=2)return finish(output,false,role,session,inputs,snapshots,events,"invalid fixture entities");++snapshots;}
   for(const auto& e:session.drain_events()){if(!mirror.apply(e))return finish(output,false,role,session,inputs,snapshots,events,"objective apply failed");++events;}
   if(inputs>=15&&snapshots>=10&&events==2&&mirror.get(0)==cod2coop::ObjectiveState::Completed){session.disconnect();return finish(output,true,role,session,inputs,snapshots,events,"snapshots and ordered objective fixture events received");}
  }
  std::this_thread::sleep_for(std::chrono::milliseconds(2));
 }
 return finish(output,false,role,session,inputs,snapshots,events,"test timed out");
}
