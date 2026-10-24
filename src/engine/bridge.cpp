#include "engine/bridge.hpp"
#include "compatibility/platform.hpp"
#include <sstream>
#include <cstring>
#include <cmath>
#include <cctype>
namespace coop {
EngineBridge* EngineBridge::active_=nullptr;
bool EngineBridge::prepare(std::string& evidence){
 auto renderer=GetModuleHandleW(L"gfx_d3d_x86_s.dll");if(!renderer){evidence="renderer not loaded";return false;}
 if(sha256(module_path(renderer))!="5b364adca51d945e3af2ec321be0646b38cb61ad129849939aca548d4eefa584"){evidence="unsupported renderer SHA256; no engine calls";return false;}
 auto* base=reinterpret_cast<unsigned char*>(renderer);auto getref=GetProcAddress(renderer,"GetRefAPI");if(reinterpret_cast<unsigned char*>(getref)!=base+0x13430){evidence="GetRefAPI RVA mismatch";return false;}
 // Checked against actual SP renderer callsites, not the incompatible MP refimport_t layout.
 auto* imports=reinterpret_cast<uintptr_t*>(base+0x1d49d8);const unsigned slots[]={0,62,63,66,67,68,69,70};
 std::ostringstream report;report<<"renderer_sha256=5b364adca51d945e3af2ec321be0646b38cb61ad129849939aca548d4eefa584";
 for(auto slot:slots){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(reinterpret_cast<void*>(imports[slot]),&info,sizeof(info)) || info.State!=MEM_COMMIT || (info.Protect&(PAGE_GUARD|PAGE_NOACCESS)) || !(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))){evidence="non-executable or empty renderer import slot "+std::to_string(slot);return false;}report<<" slot"<<std::dec<<slot<<"=0x"<<std::hex<<imports[slot];}
 print_=reinterpret_cast<Print>(imports[0]);get_string_=reinterpret_cast<GetString>(imports[62]);get_variant_=reinterpret_cast<GetString>(imports[63]);add_=reinterpret_cast<Add>(imports[66]);remove_=reinterpret_cast<Remove>(imports[67]);argc_=reinterpret_cast<Argc>(imports[68]);argv_=reinterpret_cast<Argv>(imports[69]);execute_=reinterpret_cast<Execute>(imports[70]);prepared_=true;evidence=report.str();return true;
}
void __cdecl EngineBridge::status_command(){if(active_){active_->command_thread_.store(GetCurrentThreadId());active_->callbacks_.fetch_add(1);active_->print_(0,"COD2 CO-OP ALPHA 0.1.0 — Developed by Heperkoo. Native engine command callback verified. Player bridge unavailable.\n");}}
bool EngineBridge::install_on_game_thread(){if(!prepared_ || registered_)return false;active_=this;for(auto* name:{"coop_status","coop_host","coop_join","coop_start","coop_disconnect","coop_soldier"})remove_(name);add_("coop_status",status_command);registered_=true;execute_(0,"coop_status");for(auto* command:{"logfile 2","mapname"})execute_(0,command);
 add_("coop_host",host_command);add_("coop_join",join_command);add_("coop_start",start_command);add_("coop_disconnect",disconnect_command);add_("coop_soldier",soldier_command);
 for(auto* command:{"set coop_address 127.0.0.1","set coop_bind 127.0.0.1","set coop_port 28961","set coop_soldier_index 0","set coop_ui_status \"Native runtime ready; player 2 integration pending\"","set coop_role off"})execute_(0,command);
 // Signature from observed SP initialization: it clears exactly one client object
 // and writes level.maxclients=1. This is a read-only observer, never an allocator.
 const unsigned char signature[]={0x33,0xc0,0xb9,0xae,0x21,0,0,0xbf,0,0x7e,0xc5,0x01,0xc7,0x05,0x04,0x05,0xc6,0x01,0,0x5f,0xb1,0x01,0x89,0x2d,0x2c,0x05,0xc6,0x01,0xf3,0xab,0xc7,0x05,0,0x05,0xc6,0x01,0,0x7e,0xc5,0x01};
 auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));unsigned char actual[sizeof(signature)]{};SIZE_T got=0;
 native_layout_verified_=base==0x400000 && ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(base+0xd9b29),actual,sizeof(actual),&got) && got==sizeof(actual) && memcmp(actual,signature,sizeof(actual))==0;
 if(experimental_enabled_)experiment_.install_on_game_thread([this](const std::string& text){print_(0,"COD2 CO-OP experiment: %s\n",text.c_str());});
 sample_on_game_thread();return callbacks_.load()>0;}
std::string EngineBridge::observed_mission()const{std::lock_guard<std::mutex> lock(observation_mutex_);return observed_mission_;}
namespace {template<class T>bool read(uintptr_t address,T& value){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&got)&&got==sizeof(value);}}
void EngineBridge::sample_on_game_thread(){if(!native_layout_verified_)return;NativePlayerObservation sample;uint32_t entities=0,clients=0,count=0,capacity=0;
 auto now=GetTickCount64();if(last_observation_ && now-last_observation_<50)return;last_observation_=now;
 {auto* map=get_string_("mapname");std::lock_guard<std::mutex> lock(observation_mutex_);if(map)strncpy_s(observed_mission_,map,_TRUNCATE);observation_={};}
 if(!read(0x1c60504,entities)||!read(0x1c60500,clients)||!read(0x1c6050c,count)||!read(0x1c6052c,capacity)||entities!=0x1b15f00||count>2046)return;
 if(count==0){auto empty=world_tracker_.observe(nullptr,0,0,capacity,++world_tick_);std::lock_guard<std::mutex> lock(observation_mutex_);world_=std::move(empty);return;}
 if(!((capacity==1&&clients==0x1c57e00)||(capacity==2&&experiment_.arrays_prepared()&&clients==experiment_.clients_base())))return;
 sample.entities=count;sample.client_capacity=capacity;entity_buffer_.resize(size_t(count)*0x1fc);SIZE_T copied=0;if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(entities),entity_buffer_.data(),entity_buffer_.size(),&copied)||copied!=entity_buffer_.size())return;for(unsigned i=0;i<count;++i){uint32_t client=0;std::memcpy(&client,entity_buffer_.data()+i*0x1fc+0xfc,4);if(entity_buffer_[i*0x1fc+0xa4]&&client)++sample.player_entities;}
 uint32_t protagonist_client=0;std::memcpy(&protagonist_client,entity_buffer_.data()+0xfc,4);if(!entity_buffer_[0xa4]||protagonist_client!=clients)return;
 uint32_t level_time=0;if(read(0x1c60534,level_time)){if(level_time<last_level_time_)world_tracker_.observe(nullptr,0,0,capacity,++world_tick_);last_level_time_=level_time;}
 auto world=world_tracker_.observe(entity_buffer_.data(),entity_buffer_.size(),count,capacity,++world_tick_);{std::lock_guard<std::mutex> lock(observation_mutex_);world_=std::move(world);}
 float origin[3]{};if(!read(clients+0x14,origin)||!std::isfinite(origin[0])||!std::isfinite(origin[1])||!std::isfinite(origin[2]))return;sample.x=origin[0];sample.y=origin[1];sample.z=origin[2];sample.valid=sample.player_entities>0;auto second=experiment_.observation_on_game_thread();sample.second_valid=second.valid;sample.second_x=second.x;sample.second_y=second.y;sample.second_z=second.z;sample.second_health=second.health;std::lock_guard<std::mutex> lock(observation_mutex_);observation_=sample;
}
NativePlayerObservation EngineBridge::observation()const{std::lock_guard<std::mutex> lock(observation_mutex_);return observation_;}
cod2coop::NativeWorldObservation EngineBridge::world_observation()const{std::lock_guard<std::mutex> lock(observation_mutex_);return world_;}
void EngineBridge::remove_on_game_thread(){experiment_.uninstall_on_game_thread();if(registered_){for(auto* name:{"coop_status","coop_host","coop_join","coop_start","coop_disconnect","coop_soldier"})remove_(name);registered_=false;active_=nullptr;}}
void __cdecl EngineBridge::host_command(){if(active_)active_->request(UiAction::Host);}void __cdecl EngineBridge::join_command(){if(active_)active_->request(UiAction::Join);}void __cdecl EngineBridge::start_command(){if(active_)active_->request(UiAction::Start);}void __cdecl EngineBridge::disconnect_command(){if(active_)active_->request(UiAction::Disconnect);}void __cdecl EngineBridge::soldier_command(){if(active_)active_->request(UiAction::Soldier);}
void EngineBridge::request(UiAction action){
 UiRequest request;request.action=action;auto read_setting=[this](const char* name){auto* text=get_variant_(name);return text?std::string(text, strnlen_s(text,128)):std::string();};
 request.address=read_setting("coop_address");request.bind=read_setting("coop_bind");try{request.port=std::stoul(read_setting("coop_port"));request.soldier=std::stoul(read_setting("coop_soldier_index"));if(action==UiAction::Soldier && argc_()>1)request.soldier=std::stoul(argv_(1));}catch(...){print_(0,"COD2 CO-OP: invalid port or soldier selection\n");return;}
 if(request.port<1024||request.port>65535||request.soldier>1){print_(0,"COD2 CO-OP: port must be1024..65535; soldier0 or1\n");return;}std::lock_guard<std::mutex> lock(ui_mutex_);if(requests_.size()<16)requests_.push_back(std::move(request));
}
std::vector<UiRequest> EngineBridge::take_ui_requests(){std::lock_guard<std::mutex> lock(ui_mutex_);std::vector<UiRequest> result(requests_.begin(),requests_.end());requests_.clear();return result;}
void EngineBridge::publish_session_status(const std::string& role,const std::string& status){std::lock_guard<std::mutex> lock(ui_mutex_);ui_role_=role;ui_status_=status.substr(0,180);}
void EngineBridge::queue_moscow(){start_map_.store(true);}
void EngineBridge::queue_peer_input(float forward,float right,float yaw,float pitch){std::lock_guard<std::mutex> lock(ui_mutex_);peer_input_={forward,right,yaw,pitch};peer_input_pending_=true;}
void EngineBridge::service_on_game_thread(){
 {PeerInput input;bool pending=false;{std::lock_guard<std::mutex> lock(ui_mutex_);pending=peer_input_pending_;input=peer_input_;peer_input_pending_=false;}if(pending)experiment_.apply_input_on_game_thread(static_cast<int>(input.forward*127),static_cast<int>(input.right*127),input.yaw,input.pitch);}
 switch(native_action_.exchange(0)){case 1:execute_(2,"set coop_page main\nopenmenu coop_main\n");break;case 2:execute_(2,"screenshot\n");break;case 3:execute_(2,"quit\n");break;case 4:execute_(2,"coop_host\n");break;case 5:execute_(2,"coop_start\n");break;case 6:execute_(2,"set coop_page soldier\nopenmenu coop_main\n");break;case 7:execute_(2,"coop_disconnect\n");break;case 8:execute_(2,"openmenu main\nopenmenu main_text\n");break;case 9:execute_(2,"vid_restart\nset coop_page main\nopenmenu coop_main\nwait 10\nscreenshot\n");break;default:break;}
 if(start_map_.exchange(false))execute_(2,"closemenu coop_main\nclosemenu main_text\nmap moscow\n");auto now=GetTickCount64();if(now-last_ui_<500)return;last_ui_=now;std::string role,status;{std::lock_guard<std::mutex> lock(ui_mutex_);role=ui_role_;status=ui_status_;}
 // Only locally generated status text reaches the command buffer; strip all command delimiters.
 for(auto& c:status)if(!(isalnum(static_cast<unsigned char>(c))||strchr(" .,:/_-()",c)))c=' ';for(auto& c:role)if(c<'a'||c>'z')c='_';
 if(role==published_role_&&status==published_status_)return;published_role_=role;published_status_=status;
 auto role_command="set coop_role "+role;execute_(0,role_command.c_str());auto status_command="set coop_ui_status \""+status+"\"";execute_(0,status_command.c_str());
}
}
