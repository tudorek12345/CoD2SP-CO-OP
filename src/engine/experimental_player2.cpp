#include "engine/experimental_player2.hpp"
#include "compatibility/platform.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <vector>

#if !defined(_MSC_VER) || !defined(_M_IX86)
#error This exact-build experiment requires MSVC x86 register-ABI wrappers.
#endif

namespace coop {
ExperimentalPlayer2* ExperimentalPlayer2::active_=nullptr;
void __cdecl experimental_fix_spawn_index(unsigned index);
bool __cdecl experimental_block_checkpoint();
namespace {
struct NativePlacementTrace {
 float fraction=1.0f,normal[3]{};
 uint32_t surface_flags=0,contents=0,material=0;
 uint16_t entity=0x7ff,part_name=0,part_group=0;
 unsigned char all_solid=0,start_solid=0;
};
static_assert(sizeof(NativePlacementTrace)==36);
using PlacementTrace=void(__cdecl*)(NativePlacementTrace*,const float*,const float*,const float*,const float*,int,int);
__declspec(naked) void native_player_teleport(void*,const float*){
 __asm {
  mov eax,[esp+4]
  mov edx,[esp+8]
  mov ecx,04e8890h
  call ecx
  ret
 }
}
constexpr uintptr_t entity_base=0x1b15f00,level_clients=0x1c60500;
constexpr uintptr_t level_entities=0x1c60504,level_count=0x1c6050c;
constexpr uintptr_t level_initializing=0x1c60528,level_capacity=0x1c6052c;
constexpr uintptr_t level_time=0x1c60534,level_save_mode=0x1c61048;
constexpr uintptr_t server_global=0x825508,original_clients=0x1c57e00;
constexpr uint32_t entity_stride=0x1fc,client_stride=0x86b8,server_stride=0xaa08;
constexpr uintptr_t bsp_call=0x4d9c39,spawn_call=0x4da4ca,spawn_think_call=0x4e92b9;
constexpr std::array<unsigned char,5> bsp_original{{0xe8,0x12,0x09,0x02,0x00}};
constexpr std::array<unsigned char,5> spawn_original{{0xe8,0x41,0xeb,0x00,0x00}};
constexpr std::array<unsigned char,5> spawn_think_original{{0xe8,0x22,0xec,0xff,0xff}};
constexpr uintptr_t checkpoint_entry=0x44a140;
constexpr std::array<unsigned char,5> checkpoint_original{{0x68,0x04,0x26,0x57,0x00}};
uintptr_t checkpoint_trampoline=0;
using Alloc=void*(__cdecl*)(size_t);
using Free=void(__cdecl*)(void*);
using Parse=void(__cdecl*)();
using Spawn=void(__cdecl*)(void*);
using Connect=int(__cdecl*)(unsigned);
using GetString=const char*(__cdecl*)(const char*);
const auto game_alloc=reinterpret_cast<Alloc>(0x55439d);
const auto game_free=reinterpret_cast<Free>(0x554285);
const auto parse_original=reinterpret_cast<Parse>(0x4fa550);
const auto spawn_host_original=reinterpret_cast<Spawn>(0x4e9010);
const auto connect_original=reinterpret_cast<Connect>(0x4e8f40);

template<class T>bool read(uintptr_t address,T& value){
 SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&got)&&got==sizeof(value);
}
template<class T>bool put(uintptr_t address,const T& value){
 SIZE_T wrote=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&wrote)&&wrote==sizeof(value);
}
bool write_bytes(uintptr_t address,const void* bytes,size_t size){
 SIZE_T wrote=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes,size,&wrote)&&wrote==size;
}
bool matches(uintptr_t address,const char* hex){
 std::vector<unsigned char> expected;
 for(size_t i=0;hex[i];i+=2){unsigned v=0;if(sscanf_s(hex+i,"%2x",&v)!=1)return false;expected.push_back(static_cast<unsigned char>(v));}
 std::vector<unsigned char> actual(expected.size());SIZE_T got=0;
 return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),actual.data(),actual.size(),&got)&&got==actual.size()&&actual==expected;
}
bool replace_call(uintptr_t address,const void* target){
 std::array<unsigned char,5> bytes{};bytes[0]=0xe8;
 const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-(address+5));
 memcpy(bytes.data()+1,&displacement,sizeof(displacement));
 DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),PAGE_EXECUTE_READWRITE,&old))return false;
 const bool ok=write_bytes(address,bytes.data(),bytes.size());DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),old,&ignored)!=FALSE;
 FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.size());return ok&&restored;
}
bool replace_jump(uintptr_t address,const void* target){
 std::array<unsigned char,5> bytes{};bytes[0]=0xe9;
 const auto displacement=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target)-(address+5));
 memcpy(bytes.data()+1,&displacement,sizeof(displacement));DWORD old=0;
 if(!VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),PAGE_EXECUTE_READWRITE,&old))return false;
 const bool ok=write_bytes(address,bytes.data(),bytes.size());DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),old,&ignored)!=FALSE;
 FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.size());return ok&&restored;
}
bool restore_call(uintptr_t address,const std::array<unsigned char,5>& bytes){
 DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),PAGE_EXECUTE_READWRITE,&old))return false;
 const bool ok=write_bytes(address,bytes.data(),bytes.size());DWORD ignored=0;
 const bool restored=VirtualProtect(reinterpret_cast<void*>(address),bytes.size(),old,&ignored)!=FALSE;
 FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes.size());return ok&&restored;
}
__declspec(naked) void begin_client(unsigned){
 __asm {
  mov eax,[esp+4]
  mov edx,04e8fe0h
  call edx
  ret
 }
}
__declspec(naked) void think_client(unsigned){
 __asm {
  mov eax,[esp+4]
  mov edx,04e7ee0h
  call edx
  ret
 }
}
__declspec(naked) void spawn_think_gate(){
 __asm {
  push eax
  call experimental_fix_spawn_index
  pop eax
  mov edx,04e7ee0h
  jmp edx
 }
}
__declspec(naked) void checkpoint_gate(){
 __asm {
  pushad
  call experimental_block_checkpoint
  test al,al
  popad
  jnz suppressed
  jmp dword ptr [checkpoint_trampoline]
 suppressed:
  mov eax,-3
  ret
 }
}
__declspec(naked) void set_script_string(uint16_t*,const char*){
 __asm {
  push esi
  push edi
  mov edi,[esp+12]
  mov esi,[esp+16]
  mov eax,0463500h
  call eax
  pop edi
  pop esi
  ret
 }
}
bool signatures_valid(){
 // Exact initialized bytes from the legitimate installed SP build; no offsets
 // from the MP reconstruction are accepted as a compatibility substitute.
 return matches(bsp_call,"e812090200")&&matches(spawn_call,"e841eb0000")&&matches(spawn_think_call,"e822ecffff")&&
  matches(checkpoint_entry,"6804265700e81671feff83c40485c07433")&&
  matches(0x4d9b29,"33c0b9ae210000bf007ec501c7050405c601005fb101892d2c05c601f3abc7050005c601007ec501")&&
  matches(0x4e8f40,"53558b6c240c5657e873dc02008b0d0005c6018bdd69dbb886000003d98bf569f6fc01000033c0b9ae2100008bfbf3ab")&&
  matches(0x4e8fe0,"518b150005c6018bc869c0fc01000069c9b886000005005fb10150c78411c085000002000000e80500000083c40459c3")&&
  matches(0x4e7ee0,"538bd869c008aa000069dbfc010000565781c3005fb1018b8bfc0000008db1c48500008db9f4850000b90c000000f3a5")&&
  matches(0x55439d,"ff35149e5a00ff742408e8c5ffffff5959c3")&&
  matches(0x554285,"6a0c6870a55600e8034200008b750885f67458")&&
  matches(0x463500,"51668b076685c0740c0fb7c050e8cefeffff83c4048bc68d50018d9b000000008a084084c975f92bc26a0640506a0056e8bbf7ffff83c41066890759c3");
}
bool moscow(){
 auto module=GetModuleHandleW(L"gfx_d3d_x86_s.dll");if(!module)return false;
 uintptr_t getter=0;if(!read(reinterpret_cast<uintptr_t>(module)+0x1d49d8+62*4,getter)||getter!=0x431470)return false;
 const char* name=reinterpret_cast<GetString>(getter)("mapname");char copy[16]{};SIZE_T got=0;
 return name&&ReadProcessMemory(GetCurrentProcess(),name,copy,sizeof(copy)-1,&got)&&strncmp(copy,"moscow",sizeof(copy))==0;
}
}

void ExperimentalPlayer2::state(const std::string& value){
 {std::lock_guard<std::mutex> lock(mutex_);state_=value;}
 if(log_)log_(value);
}
bool ExperimentalPlayer2::install_on_game_thread(Log logger){
 if(installed_)return true;log_=std::move(logger);game_thread_=GetCurrentThreadId();
 if(active_&&active_!=this){state("rejected: another player experiment is active");return false;}
 if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000||sha256(module_path())!=sp_hash||!signatures_valid()){
  state("rejected: executable hash, base or native ABI signatures differ");return false;
 }
 auto renderer=GetModuleHandleW(L"gfx_d3d_x86_s.dll");
 if(!renderer||sha256(module_path(renderer))!="5b364adca51d945e3af2ec321be0646b38cb61ad129849939aca548d4eefa584"){
  state("rejected: renderer version differs");return false;
 }
 presentation_.prepare_on_game_thread(log_);
 active_=this;
 // The native save format serializes exactly one client and rejects a second
 // pointer. Suppress requests with its existing nonfatal "not queued" code -3.
 // No two-player checkpoint is emitted or advertised as valid.
 auto* trampoline=static_cast<unsigned char*>(VirtualAlloc(nullptr,10,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
 if(!trampoline){active_=nullptr;state("rejected: checkpoint trampoline allocation failed");return false;}
 memcpy(trampoline,checkpoint_original.data(),5);trampoline[5]=0xe9;
 const auto back=static_cast<uint32_t>((checkpoint_entry+5)-(reinterpret_cast<uintptr_t>(trampoline)+10));
 memcpy(trampoline+6,&back,4);FlushInstructionCache(GetCurrentProcess(),trampoline,10);checkpoint_trampoline=reinterpret_cast<uintptr_t>(trampoline);
 if(!replace_call(bsp_call,reinterpret_cast<const void*>(&bsp_gate))){restore_call(bsp_call,bsp_original);active_=nullptr;state("rejected: pre-BSP call patch failed");return false;}
 if(!replace_call(spawn_call,reinterpret_cast<const void*>(&host_spawn_gate))){restore_call(bsp_call,bsp_original);active_=nullptr;state("rejected: host-spawn call patch failed");return false;}
 if(!replace_call(spawn_think_call,reinterpret_cast<const void*>(&spawn_think_gate))){restore_call(bsp_call,bsp_original);restore_call(spawn_call,spawn_original);active_=nullptr;state("rejected: spawn-think call patch failed");return false;}
 if(!replace_jump(checkpoint_entry,reinterpret_cast<const void*>(&checkpoint_gate))){restore_call(bsp_call,bsp_original);restore_call(spawn_call,spawn_original);restore_call(spawn_think_call,spawn_think_original);restore_call(checkpoint_entry,checkpoint_original);active_=nullptr;state("rejected: checkpoint guard patch failed");return false;}
 installed_=true;state("armed: opt-in one-shot fresh Moscow native player2 experiment; checkpoints disabled for experimental two-player state; no actor yet");return true;
}
void ExperimentalPlayer2::uninstall_on_game_thread(){
 if(!installed_)return;
 if(GetCurrentThreadId()!=game_thread_){state("cleanup deferred: wrong thread; keep DLL resident");return;}
 if(storage_valid()){state("cleanup deferred: end the two-player mission before removing its checkpoint guard; keep DLL resident");return;}
 const bool a=restore_call(bsp_call,bsp_original),b=restore_call(spawn_call,spawn_original),c=restore_call(spawn_think_call,spawn_think_original),d=restore_call(checkpoint_entry,checkpoint_original);
 if(a&&b&&c&&d){presentation_.uninstall_on_game_thread();installed_=false;active_=nullptr;if(checkpoint_trampoline){VirtualFree(reinterpret_cast<void*>(checkpoint_trampoline),0,MEM_RELEASE);checkpoint_trampoline=0;}state("hooks restored; published buffers stay valid until process exit; experimental actor cleanup requires ending mission");}
 else state("hook restoration incomplete; keep DLL resident and terminate the isolated game normally");
 // Never free live game-client data or a server array owned by native shutdown.
 // The old one-slot server allocation is retained as a bounded process-lifetime
 // allocation so no cached native pointer becomes dangling during this probe.
}
void __cdecl ExperimentalPlayer2::bsp_gate(){
 if(active_){try{active_->before_bsp();}catch(const std::exception& e){active_->state(std::string("pre-BSP exception: ")+e.what());}catch(...){active_->state("pre-BSP exception");}}
 parse_original();
}
void ExperimentalPlayer2::before_bsp(){
 actor_ready_.store(false);arrays_prepared_.store(false);
 if(GetCurrentThreadId()!=game_thread_||attempted_){state("bypassed: experiment is one-shot and game-thread only; original map initialization continues");return;}
 attempted_=true;uint32_t c=0,e=0,count=0,capacity=0,mode=0,server=0,initializing=0;
 if(!moscow()||!read(level_save_mode,mode)||mode!=1||!read(level_initializing,initializing)||initializing!=1||
    !read(level_clients,c)||c!=original_clients||!read(level_entities,e)||e!=entity_base||
    !read(level_count,count)||count!=1||!read(level_capacity,capacity)||capacity!=1||
    !read(server_global,server)||!server){state("bypassed: requires exact fresh Moscow pre-BSP singleton state, no saved-game load");return;}
 uint32_t host_client=0,extra_client=0;unsigned char extra_inuse=0;
 if(!read(entity_base+0xfc,host_client)||host_client!=c||!read(entity_base+entity_stride+0xfc,extra_client)||extra_client||
    !read(entity_base+entity_stride+0xa4,extra_inuse)||extra_inuse){state("bypassed: entity slot1 already occupied or original client attachment differs");return;}
 state("allocating native-compatible two-slot client/server-client buffers before BSP spawn");
 void* new_clients=game_alloc(client_stride*2);void* new_server=game_alloc(server_stride*2);
 if(!new_clients||!new_server){if(new_clients)game_free(new_clients);if(new_server)game_free(new_server);state("bypassed: native allocator failed");return;}
 memset(new_clients,0,client_stride*2);memset(new_server,0,server_stride*2);SIZE_T copied=0;
 bool copies=ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(c),new_clients,client_stride,&copied)&&copied==client_stride;
 copies=copies&&ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(server),new_server,server_stride,&copied)&&copied==server_stride;
 if(!copies){game_free(new_clients);game_free(new_server);state("bypassed: original storage copy failed");return;}
 clients_=reinterpret_cast<uintptr_t>(new_clients);server_clients_=reinterpret_cast<uintptr_t>(new_server);old_server_clients_=server;
 const uint32_t nc=static_cast<uint32_t>(clients_),ns=static_cast<uint32_t>(server_clients_),second=nc+client_stride,two=2;
 // Both indexes are compact1. Publish all indexed storage before native spawn,
 // which itself immediately executes ClientThink(1).
 const bool published=put(level_clients,nc)&&put(level_capacity,two)&&put(level_count,two)&&
  put(entity_base+0xfc,nc)&&put(entity_base+entity_stride+0xfc,second)&&put(server_global,ns)&&
  put(0x822430,nc)&&put(0x82242c,two);
 if(!published){
  const uint32_t one=1,zero=0;put(level_clients,c);put(level_capacity,one);put(level_count,one);put(entity_base+0xfc,c);
  put(entity_base+entity_stride+0xfc,zero);put(server_global,server);put(0x822430,c);put(0x82242c,one);
  game_free(new_clients);game_free(new_server);clients_=server_clients_=old_server_clients_=0;state("bypassed: storage publication failed; singleton metadata restored");return;
 }
 arrays_prepared_.store(true);state("arrays prepared: capacity2, reserved entity1, aligned game/server client slot1; original BSP parser continues");
}
bool ExperimentalPlayer2::storage_valid()const{
 uint32_t c=0,s=0,capacity=0,p0=0,p1=0;
 return arrays_prepared_.load()&&read(level_clients,c)&&c==clients_&&read(server_global,s)&&s==server_clients_&&
  read(level_capacity,capacity)&&capacity==2&&read(entity_base+0xfc,p0)&&p0==clients_&&
  read(entity_base+entity_stride+0xfc,p1)&&p1==clients_+client_stride;
}
void __cdecl ExperimentalPlayer2::host_spawn_gate(void* host){
 spawn_host_original(host);
 if(active_){try{active_->after_host_spawn(host);}catch(const std::exception& e){active_->state(std::string("player2 exception: ")+e.what());}catch(...){active_->state("player2 exception");}}
}
void __cdecl experimental_fix_spawn_index(unsigned index){
 auto* active=ExperimentalPlayer2::active_;
 if(index==1&&active&&active->storage_valid()){
  // ClientConnect explicitly assigns this player index. Singleton ClientSpawn
  // clears it again. Restore it before Spawn's first native Pmove/Think call.
  const uint32_t value=1;put(active->clients_+client_stride+0xa8,value);
 }
}
bool __cdecl experimental_block_checkpoint(){
 auto* active=ExperimentalPlayer2::active_;
 if(!active||!active->arrays_prepared_.load())return false;
 uint32_t capacity=0,clients=0;
 if(!read(level_capacity,capacity)||capacity!=2||!read(level_clients,clients)||clients!=active->clients_)return false;
 const auto count=active->blocked_saves_.fetch_add(1)+1;
 if(active->log_)active->log_("checkpoint request declined (-3): two-player save/read format is unsupported; no checkpoint written; count="+std::to_string(count));
 return true;
}
void ExperimentalPlayer2::after_host_spawn(void* host){
 if(!arrays_prepared_.load()||actor_ready_.load())return;
 uint32_t mode=0;if(GetCurrentThreadId()!=game_thread_||reinterpret_cast<uintptr_t>(host)!=entity_base||!storage_valid()||
    !read(level_save_mode,mode)||mode!=1){state("player2 rejected: host/fresh-map/storage invariant failed");return;}
 const uint32_t extra=static_cast<uint32_t>(entity_base+entity_stride);
 // Native singleton connection work follows game-compatible storage expansion.
 // It allocates the required sentient and runs before original AI/script startup.
 state("calling verified native ClientConnect(1) before mission main");
 if(connect_original(1)!=0){state("native ClientConnect(1) returned an error; actor not accepted");return;}
 uint32_t sentient=0;if(!storage_valid()||!read(extra+0x104,sentient)||!sentient){state("native client1 has no valid sentient; Begin was not called");return;}
 uint32_t now=0;read(level_time,now);
 put(server_clients_+server_stride+0x2410,now);put(server_clients_+server_stride+0x2440,extra);
 const uint32_t connected=1;put(server_clients_+server_stride,connected);
 state("calling verified native ClientBegin(1); spawn fallback may overlap original start until movement");
 begin_client(1);
 uint32_t player_index=0;
 if(!read(clients_+client_stride+0xa8,player_index)||player_index!=1){state("native client1 index restoration failed");return;}
 // Preserve getent("player","classname") uniqueness before original mission
 // main/_load captures the canonical protagonist. The native setter refcounts.
 set_script_string(reinterpret_cast<uint16_t*>(extra+0x118),"coop_player");
 presentation_.assign_body_on_game_thread(extra,soldier_.load());
 presentation_.install_nametag_on_game_thread(extra,"Player 2");
 unsigned char used=0;uint32_t attached=0;float pos[3]{};
 if(!read(extra+0xa4,used)||!used||!read(extra+0xfc,attached)||attached!=clients_+client_stride||
    !read(attached+0x14,pos)||!std::isfinite(pos[0])||!std::isfinite(pos[1])||!std::isfinite(pos[2])){
  state("native player2 spawned but postconditions failed; gameplay acceptance withheld");return;
 }
 actor_ready_.store(true);last_input_time_=0;
 std::ostringstream detail;detail<<"native player2 exists: entity1 client-backed sentient=0x"<<std::hex<<sentient<<std::dec
  <<" origin="<<pos[0]<<","<<pos[1]<<","<<pos[2]<<" classname=coop_player; presentation="<<presentation_.state_report()<<"; soldier="<<soldier_.load();state(detail.str());
}
bool ExperimentalPlayer2::apply_input_on_game_thread(int forward,int right,float yaw,float pitch){
 uint32_t initializing=1,now=0,index=0;
 if(!installed_||GetCurrentThreadId()!=game_thread_||!actor_ready_.load()||!storage_valid()||!read(level_initializing,initializing)||initializing||
    !read(level_time,now)||now<=last_input_time_||!read(clients_+client_stride+0xa8,index)||index!=1||!std::isfinite(yaw)||!std::isfinite(pitch))return false;
 std::array<unsigned char,48> command{};SIZE_T copied=0;
 if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(clients_+client_stride+0x85c4),command.data(),command.size(),&copied)||copied!=command.size())return false;
 memcpy(command.data(),&now,4);const uint32_t no_buttons=0;memcpy(command.data()+4,&no_buttons,4);
 command[24]=static_cast<unsigned char>(static_cast<signed char>(std::clamp(forward,-127,127)));
 command[25]=static_cast<unsigned char>(static_cast<signed char>(std::clamp(right,-127,127)));
 const float angles[3]={std::clamp(pitch,-85.0f,85.0f),std::remainder(yaw,360.0f),0};
 for(unsigned axis=0;axis<3;++axis){int32_t delta=0;if(!read(clients_+client_stride+0x54+axis*4,delta))return false;
  const uint32_t value=static_cast<uint32_t>(static_cast<int32_t>(angles[axis]*(65536.0f/360.0f))-delta)&0xffff;
  memcpy(command.data()+12+axis*4,&value,4);
 }
 if(!write_bytes(server_clients_+server_stride+0x2410,command.data(),command.size()))return false;
 think_client(1);last_input_time_=now;++input_count_;return true;
}
bool ExperimentalPlayer2::place_near_host_on_game_thread(){
 uint32_t initializing=1;if(!installed_||GetCurrentThreadId()!=game_thread_||!actor_ready()||
  !read(level_initializing,initializing)||initializing)return false;
 if(!matches(0x4db7f0,"8b44241c8b4c24188b542410538b5c240c568b74240c578b7c24206a006a006a")||
  !matches(0x4e8890,"8b88fc00000083c114568b3289318b72048971048b52088951088b88fc000000")||
  !matches(0x44b2d0,"83ec1c53558b6c242885ed5657740866817d080008720f")){
  state("regroup rejected: original trace/teleport/link signatures differ");return false;
 }
 const uintptr_t extra=entity_base+entity_stride;float host[3]{},angles[3]{},mins[3]{},maxs[3]{},velocity[3]{};
 signed char movement[2]{};uint32_t mask=0;
 if(!read(clients_+0x14,host)||!read(clients_+0xc4,angles)||!read(extra+0xa8,mins)||!read(extra+0xb4,maxs)||
  !read(extra+0x12c,mask)||!read(clients_+client_stride+0x85dc,movement)||!read(clients_+client_stride+0x20,velocity))return false;
 if(movement[0]||movement[1]||velocity[0]*velocity[0]+velocity[1]*velocity[1]>25.0f){state("regroup deferred: player2 movement is active");return false;}
 for(unsigned i=0;i<3;++i)if(!std::isfinite(host[i])||!std::isfinite(angles[i])||!std::isfinite(mins[i])||!std::isfinite(maxs[i])||mins[i]>=maxs[i])return false;
 if(mins[0]<-32||mins[0]>-8||mins[1]<-32||maxs[0]>32||maxs[1]>32||mins[2]<-32||maxs[2]<24||maxs[2]>80||mask!=0x281c011){state("regroup rejected: unexpected native capsule bounds/mask");return false;}
 const auto trace=reinterpret_cast<PlacementTrace>(0x4db7f0);
 const float yaw=angles[1]*(3.14159265358979323846f/180.0f),offsets[]={25,-25,0,45,-45,70,-70,180};
 for(float distance:{80.0f,112.0f,64.0f})for(float offset:offsets){
  const float direction=yaw+offset*(3.14159265358979323846f/180.0f);
  float start[3]={host[0],host[1],host[2]+4},candidate[3]={host[0]+std::cos(direction)*distance,host[1]+std::sin(direction)*distance,host[2]+4};
  NativePlacementTrace horizontal;trace(&horizontal,start,mins,maxs,candidate,0,static_cast<int>(mask));
  if(horizontal.all_solid||horizontal.start_solid||horizontal.fraction<1.0f)continue;
  float above[3]={candidate[0],candidate[1],host[2]+32},below[3]={candidate[0],candidate[1],host[2]-96};
  NativePlacementTrace ground;trace(&ground,above,mins,maxs,below,1,static_cast<int>(mask));
  if(ground.all_solid||ground.start_solid||!std::isfinite(ground.fraction)||ground.fraction<=0||ground.fraction>=1||ground.normal[2]<0.7f||ground.entity!=0x7fe)continue;
  float destination[3]={candidate[0],candidate[1],above[2]+(below[2]-above[2])*ground.fraction+0.5f};
  if(std::abs(destination[2]-host[2])>32)continue;
  NativePlacementTrace occupied;trace(&occupied,destination,mins,maxs,destination,1,static_cast<int>(mask));
  if(occupied.all_solid||occupied.start_solid||occupied.fraction<1.0f)continue;
  native_player_teleport(reinterpret_cast<void*>(extra),destination);
  reinterpret_cast<Spawn>(0x44b2d0)(reinterpret_cast<void*>(extra));
  float actual[3]{};if(!read(clients_+client_stride+0x14,actual)||std::abs(actual[0]-destination[0])>0.1f||std::abs(actual[1]-destination[1])>0.1f||std::abs(actual[2]-(destination[2]+1.0f))>0.1f){state("regroup native teleport postcondition failed");return false;}
  std::ostringstream detail;detail<<"regroup placed real player2 through native SetOrigin and LinkEntity; collision-checked origin="<<actual[0]<<","<<actual[1]<<","<<actual[2]<<" distance="<<distance<<" view_offset="<<offset<<" ground_normal_z="<<ground.normal[2];state(detail.str());return true;
 }
 state("regroup found no unobstructed standing capsule on nearby static walkable ground; player2 unchanged");return false;
}
std::string ExperimentalPlayer2::diagnostic_report_on_game_thread()const{return presentation_.diagnostic_report_on_game_thread();}
ExperimentalPlayerObservation ExperimentalPlayer2::observation_on_game_thread()const{
 ExperimentalPlayerObservation result;if(GetCurrentThreadId()!=game_thread_||!actor_ready_.load()||!storage_valid())return result;
 float origin[3]{};if(!read(clients_+client_stride+0x14,origin)||!read(entity_base+entity_stride+0x13c,result.health)||
    !read(entity_base+entity_stride+0x104,result.sentient))return result;
 result.x=origin[0];result.y=origin[1];result.z=origin[2];result.valid=std::isfinite(result.x)&&std::isfinite(result.y)&&std::isfinite(result.z)&&result.sentient!=0;return result;
}
std::string ExperimentalPlayer2::state_report()const{
 const bool current=storage_valid();std::lock_guard<std::mutex> lock(mutex_);
 return std::string(current?"current_native_buffers_valid; ":"current_mission_inactive_or_buffers_invalid; last_event=")+state_+" inputs="+std::to_string(input_count_.load())+" checkpoints_declined="+std::to_string(blocked_saves_.load())+"; checkpoint restore unsupported; "+presentation_.state_report();
}
bool ExperimentalPlayer2::actor_ready()const{
 if(!actor_ready_.load()||!storage_valid())return false;
 unsigned char inuse=0;return read(entity_base+entity_stride+0xa4,inuse)&&inuse!=0;
}
}
