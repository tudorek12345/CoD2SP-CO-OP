#include "engine/native_client_mirror.hpp"
#include "compatibility/platform.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <mutex>
#include <optional>
#include <sstream>
#include <unordered_map>
#include <windows.h>

#if !defined(_MSC_VER)||!defined(_M_IX86)
#error Native client mirror requires the exact Windows MSVC x86 ABI.
#endif

namespace coop {
namespace {
constexpr uintptr_t kEntities=0x1b15f00,kLevelClients=0x1c60500,kLevelEntities=0x1c60504;
constexpr uintptr_t kLevelCount=0x1c6050c,kCapacity=0x1c6052c,kServerClients=0x825508;
constexpr uintptr_t kCommandNumber=0x622190,kCommandRing=0x621590;
constexpr uintptr_t kRunFrame=0x4da970,kLocalThink=0x4475f0;
constexpr size_t kEntityStride=0x1fc,kClientStride=0x86b8;
uintptr_t mirror_input_trampoline=0;
using BlockInput=bool(__cdecl*)();BlockInput mirror_should_block_input=nullptr;
using Frame=int(__cdecl*)(int,int);
Frame mirror_frame_trampoline=nullptr;
__declspec(naked) void mirror_input_gate(){
 __asm {
  pushad
  call dword ptr [mirror_should_block_input]
  test al,al
  popad
  jnz suppress
  jmp dword ptr [mirror_input_trampoline]
 suppress:
  ret
 }
}
template<class T>bool read(uintptr_t address,T& value){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),&value,sizeof(value),&n)&&n==sizeof(value);}
bool read_bytes(uintptr_t address,void* out,size_t size){SIZE_T n=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),out,size,&n)&&n==size;}
template<class T>bool write(uintptr_t address,const T& value){SIZE_T n=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),&value,sizeof(value),&n)&&n==sizeof(value);}
bool write_bytes(uintptr_t address,const void* bytes,size_t size){SIZE_T n=0;return WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(address),bytes,size,&n)&&n==size;}
bool signature(uintptr_t address,const std::initializer_list<uint8_t>& expected){std::vector<uint8_t> actual(expected.size());return read_bytes(address,actual.data(),actual.size())&&std::equal(actual.begin(),actual.end(),expected.begin());}
bool finite_position(float x,float y,float z){return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::abs(x)<=1048576.f&&std::abs(y)<=1048576.f&&std::abs(z)<=1048576.f;}
struct EntryHook {
 uintptr_t address=0;std::vector<uint8_t> original,patch;void* trampoline=nullptr;bool installed=false;
 bool install(uintptr_t entry,size_t length,const void* target){
  if(installed||length<5||length>16)return false;address=entry;original.resize(length);if(!read_bytes(entry,original.data(),length))return false;
  trampoline=VirtualAlloc(nullptr,length+5,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!trampoline)return false;
  auto* memory=static_cast<uint8_t*>(trampoline);std::memcpy(memory,original.data(),length);memory[length]=0xe9;uint32_t back=uint32_t(entry+length-(reinterpret_cast<uintptr_t>(memory)+length+5));std::memcpy(memory+length+1,&back,4);
  DWORD old=0;if(!VirtualProtect(trampoline,length+5,PAGE_EXECUTE_READ,&old)){VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return false;}FlushInstructionCache(GetCurrentProcess(),trampoline,length+5);
  patch.assign(length,0x90);patch[0]=0xe9;uint32_t to=uint32_t(reinterpret_cast<uintptr_t>(target)-(entry+5));std::memcpy(patch.data()+1,&to,4);
  if(!VirtualProtect(reinterpret_cast<void*>(entry),length,PAGE_EXECUTE_READWRITE,&old)){VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return false;}
  bool ok=write_bytes(entry,patch.data(),length);DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(entry),length,old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(entry),length);
  if(!ok){DWORD protection=0;if(VirtualProtect(reinterpret_cast<void*>(entry),length,PAGE_EXECUTE_READWRITE,&protection)){write_bytes(entry,original.data(),length);VirtualProtect(reinterpret_cast<void*>(entry),length,protection,&ignored);}VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return false;}
  installed=true;return true;
 }
 bool remove(){
  if(!installed)return true;std::vector<uint8_t> current(patch.size());if(!read_bytes(address,current.data(),current.size())||current!=patch)return false;
  DWORD old=0;if(!VirtualProtect(reinterpret_cast<void*>(address),original.size(),PAGE_EXECUTE_READWRITE,&old))return false;bool ok=write_bytes(address,original.data(),original.size());DWORD ignored=0;VirtualProtect(reinterpret_cast<void*>(address),original.size(),old,&ignored);FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(address),original.size());if(!ok)return false;installed=false;VirtualFree(trampoline,0,MEM_RELEASE);trampoline=nullptr;return true;
 }
};
}
struct NativeClientMirror::Impl {
 static Impl* active;
 StorageProvider storage;ExecuteCommand execute;ReadDvar dvar;Log logger;ValidateTemplate validate_template;
 DWORD game_thread=0;bool installed=false,servicing=false,seen_map=false,nopredict=false;
 EntryHook frame_hook,input_hook;ClientMirrorStorage current{};
 std::atomic<bool> frozen{false},ready{false},connected{false},epoch_blocked{false};
 std::atomic<uint32_t> authority_epoch{0};
 std::atomic<uint64_t> frozen_frames{0},blocked_thinks{0},captured_inputs{0},applied_snapshots{0},applied_world{0},rejected_world{0};
 mutable std::mutex queue_mutex,state_mutex;
 std::optional<cod2coop::net::Snapshot> pending_snapshot;
 std::deque<cod2coop::net::WorldUpdate> pending_world;
 std::vector<cod2coop::net::Input> outgoing;
 std::string state="disabled";
 struct Binding {ClientMirrorTemplate entry;uint16_t model_index=0,classname=0;};
 std::unordered_map<uint32_t,Binding> bindings;
 uint32_t last_command=0,last_command_time=0,last_snapshot=0,initial_vm_clock=0;bool have_vm_clock=false;
 uint64_t last_authority=0;
 void say(const std::string& text){bool changed=false;{std::lock_guard<std::mutex> lock(state_mutex);if(state!=text){state=text;changed=true;}}if(changed&&logger)logger(text);}
 bool on_owner()const{return GetCurrentThreadId()==game_thread;}
 bool valid_storage(){
  current=storage?storage():ClientMirrorStorage{};uint32_t clients=0,entities=0,count=0,capacity=0,server=0,pointer0=0,pointer1=0;uint8_t inuse0=0,inuse1=0;
  return current.arrays_prepared&&current.second_actor_ready&&current.clients&&current.server_clients&&
   read(kLevelClients,clients)&&clients==current.clients&&read(kServerClients,server)&&server==current.server_clients&&
   read(kLevelEntities,entities)&&entities==kEntities&&read(kLevelCount,count)&&count>=2&&count<=2046&&read(kCapacity,capacity)&&capacity==2&&
   read(kEntities+0xa4,inuse0)&&inuse0&&read(kEntities+kEntityStride+0xa4,inuse1)&&inuse1&&
   read(kEntities+0xfc,pointer0)&&pointer0==clients&&read(kEntities+kEntityStride+0xfc,pointer1)&&pointer1==clients+kClientStride;
 }
 bool frame_gate_impl()noexcept{
  try{
   if(!on_owner())return frozen.load();uint32_t count=0,client0=0;uint8_t inuse=0;
   if(!read(kLevelCount,count))return frozen.load();
   if(!count){if(seen_map){seen_map=false;frozen.store(false);ready.store(false);authority_epoch.store(0);epoch_blocked.store(false);bindings.clear();last_authority=0;last_snapshot=last_command=last_command_time=0;have_vm_clock=false;nopredict=false;std::lock_guard<std::mutex> lock(queue_mutex);pending_snapshot.reset();pending_world.clear();outgoing.clear();}return false;}
   if(!read(kEntities+0xa4,inuse)||!read(kEntities+0xfc,client0))return frozen.load();
   if(!inuse||!client0)return frozen.load();
   seen_map=true;frozen.store(true);++frozen_frames;
   if(!valid_storage()){ready.store(false);say("authority frozen; waiting for verified two-client storage and two real actors");return true;}
   if(!dvar||dvar("mapname")!="moscow"){ready.store(false);say("authority frozen; client mirror accepts moscow only");return true;}
   if(!nopredict){execute("set cg_nopredict 1");nopredict=dvar("cg_nopredict")=="1";if(!nopredict){say("authority frozen; cg_nopredict did not enable, no camera writes");return true;}}
   if(!have_vm_clock){have_vm_clock=read(0x9bef20,initial_vm_clock);if(!have_vm_clock){say("authority frozen; cannot verify native VM scheduler clock");return true;}}
   ready.store(true);service();return true;
  }catch(...){ready.store(false);frozen.store(true);return true;}
 }
 static int __cdecl frame_gate(int budgeted,int deadline){if(active&&active->frame_gate_impl())return 1;return mirror_frame_trampoline?mirror_frame_trampoline(budgeted,deadline):1;}
 static bool __cdecl should_block_input(){if(active&&active->frozen.load()){++active->blocked_thinks;return true;}return false;}
 bool write_transform(uint16_t slot,const cod2coop::net::WorldEntity& entity){
  uintptr_t address=kEntities+size_t(slot)*kEntityStride;std::array<uint8_t,kEntityStride> before{};if(!read_bytes(address,before.data(),before.size())||!before[0xa4])return false;
  const uint32_t interpolate=1;const float position[]={entity.x,entity.y,entity.z};
  if(entity.fields&1){if(!finite_position(entity.x,entity.y,entity.z))return false;if(!write(address+0x14,interpolate)||!write_bytes(address+0x20,position,sizeof(position))||!write_bytes(address+0xdc,position,sizeof(position))){write_bytes(address+0x14,before.data()+0x14,4);write_bytes(address+0x20,before.data()+0x20,12);write_bytes(address+0xdc,before.data()+0xdc,12);return false;}}
  if(entity.fields&2){const float angles[]={entity.pitch,entity.yaw,entity.roll};if(!write(address+0x38,interpolate)||!write_bytes(address+0x44,angles,sizeof(angles))){write_bytes(address+0x38,before.data()+0x38,4);write_bytes(address+0x44,before.data()+0x44,12);return false;}}
  return true;
 }
 void apply_page(const cod2coop::net::WorldUpdate& page){
  uint32_t epoch=authority_epoch.load();if(!epoch){authority_epoch.store(page.map_epoch);epoch=page.map_epoch;}
  if(page.map_epoch!=epoch){epoch_blocked.store(true);bindings.clear();say("authority frozen; host map epoch changed, fresh map/bootstrap required");return;}
  if(epoch_blocked.load())return;
  for(const auto& entity:page.entities){
   // Player transforms arrive at 20 Hz in the typed Snapshot, so slower world
   // pages must not roll them backwards between player snapshots.
   if(entity.entity<=2)continue;
   auto found=bindings.find(entity.entity);if(found==bindings.end()){++rejected_world;continue;}const auto& binding=found->second;
   if(binding.entry.map_epoch!=epoch||entity.native_slot!=binding.entry.native_slot||entity.native_type!=binding.entry.native_type){++rejected_world;continue;}
   std::array<uint8_t,kEntityStride> local{};uintptr_t address=kEntities+size_t(entity.native_slot)*kEntityStride;uint16_t model=0,classname=0;
   if(!read_bytes(address,local.data(),local.size())){++rejected_world;continue;}std::memcpy(&model,local.data()+2,2);std::memcpy(&classname,local.data()+0x118,2);
   if(!local[0xa4]||local[0]!=binding.entry.native_type||model!=binding.model_index||classname!=binding.classname){++rejected_world;continue;}
   if(write_transform(entity.native_slot,entity))++applied_world;else ++rejected_world;
  }
 }
 void apply_snapshot(const cod2coop::net::Snapshot& snapshot){
  using namespace cod2coop::net;if(epoch_blocked.load()||!newer(snapshot.tick,last_snapshot))return;const auto& host=snapshot.players[0];const auto& local=snapshot.players[1];
  if(host.entity!=1||host.slot!=1||local.entity!=2||local.slot!=2||!finite_position(host.x,host.y,host.z)||!finite_position(local.x,local.y,local.z))return;
  WorldEntity host_entity;host_entity.entity=1;host_entity.native_slot=0;host_entity.x=host.x;host_entity.y=host.y;host_entity.z=host.z;
  WorldEntity local_entity;local_entity.entity=2;local_entity.native_slot=1;local_entity.x=local.x;local_entity.y=local.y;local_entity.z=local.z;
  if(!write_transform(0,host_entity)||!write_transform(1,local_entity))return;
  const float origin[]={local.x,local.y,local.z};const float angles[]={std::clamp(local.pitch,-90.f,90.f),std::remainder(local.yaw,360.f),0.f};const uint32_t client_number=1;
  // Native SV snapshot of client0 is the view carrier. Its entity0 presentation
  // remains the remote host; only this view PS is routed to local actor1.
  if(!write_bytes(current.clients+0x14,origin,sizeof(origin))||!write_bytes(current.clients+0xc4,angles,sizeof(angles))||!write(current.clients+0xa8,client_number)||!write_bytes(current.clients+kClientStride+0x14,origin,sizeof(origin)))return;
  last_snapshot=snapshot.tick;last_authority=GetTickCount64();++applied_snapshots;say("client authority frozen; authoritative player2 view and player transforms applied; NPC templates require verified bootstrap");
 }
 void capture_input(){
  if(!connected.load()||!last_authority||GetTickCount64()-last_authority>500||epoch_blocked.load())return;
  uint32_t number=0,after=0;if(!read(kCommandNumber,number)||!number||!cod2coop::net::newer(number,last_command))return;std::array<uint8_t,48> command{};
  if(!read_bytes(kCommandRing+(number&63)*48,command.data(),command.size())||!read(kCommandNumber,after)||after!=number)return;
  uint32_t time=0;std::memcpy(&time,command.data(),4);float pitch=0,yaw=0;
  // The native cg_nopredict view path already applies delta_angles and pitch
  // restrictions. Reading that final native look avoids copying an MP command ABI.
  if(!read(0xf709ac,pitch)||!read(0xf709b0,yaw)||!std::isfinite(pitch)||!std::isfinite(yaw))return;
  cod2coop::net::Input input;input.command=number;input.milliseconds=last_command_time?std::clamp<uint32_t>(time-last_command_time,1,250):50;input.forward=std::clamp(float(static_cast<int8_t>(command[24]))/127.f,-1.f,1.f);input.right=std::clamp(float(static_cast<int8_t>(command[25]))/127.f,-1.f,1.f);input.yaw=std::remainder(yaw,360.f);input.pitch=std::clamp(pitch,-90.f,90.f);
  {std::lock_guard<std::mutex> lock(queue_mutex);if(outgoing.size()>=128)outgoing.erase(outgoing.begin());outgoing.push_back(input);}last_command=number;last_command_time=time;++captured_inputs;
 }
 void service(){
  if(!on_owner()||!ready.load()||servicing||!valid_storage())return;servicing=true;
  struct Clear{bool& value;~Clear(){value=false;}} clear{servicing};std::optional<cod2coop::net::Snapshot> snapshot;std::deque<cod2coop::net::WorldUpdate> pages;
  uint32_t clock=0;if(!have_vm_clock||!read(0x9bef20,clock)||clock!=initial_vm_clock){epoch_blocked.store(true);say("authority frozen; native VM scheduler clock changed outside guarded frame, state application rejected");return;}
  {std::lock_guard<std::mutex> lock(queue_mutex);snapshot=std::move(pending_snapshot);pending_snapshot.reset();pages.swap(pending_world);}
  if(!connected.load())return;for(const auto& page:pages)apply_page(page);if(snapshot)apply_snapshot(*snapshot);capture_input();
 }
};
NativeClientMirror::Impl* NativeClientMirror::Impl::active=nullptr;
NativeClientMirror::NativeClientMirror():p_(std::make_unique<Impl>()){}
NativeClientMirror::~NativeClientMirror(){if(p_&&p_->installed)p_.release();} // active hooks require a resident context until explicit removal/process exit
bool NativeClientMirror::install_on_game_thread(StorageProvider storage,ExecuteCommand execute,ReadDvar dvar,Log log,ValidateTemplate validator){
 auto& p=*p_;if(p.installed||Impl::active||!storage||!execute||!dvar)return false;p.game_thread=GetCurrentThreadId();p.storage=std::move(storage);p.execute=std::move(execute);p.dvar=std::move(dvar);p.logger=std::move(log);p.validate_template=std::move(validator);
 if(reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr))!=0x400000||!supported_sp(module_path())||
  !signature(kRunFrame,{0xa1,0x5c,0x10,0xc6,0x01,0x83,0xec,0x0c,0x85,0xc0,0x53,0x55,0x56,0x57})||
  !signature(kLocalThink,{0x51,0x56,0x57,0x8b,0x3d,0x08,0x55,0x82,0x00,0x8b,0xf0})||
  !signature(0x4b9cf0,{0x83,0xec,0x64,0xa1,0x08,0xad,0xcb,0x01,0x53,0x89,0x44,0x24,0x08,0x05,0xc0,0x85,0x00,0x00})||
  !signature(0x4fb3f0,{0x51,0xba,0x01,0x00,0x00,0x00,0x88,0x10,0x89,0x50,0x14})||
  !signature(0x409e50,{0x8b,0x0d,0x14,0xa9,0x74,0x00,0x83,0xec,0x60,0x56})){p.say("client mirror rejected unsupported executable/native signatures");return false;}
 Impl::active=&p;
 if(!p.frame_hook.install(kRunFrame,5,reinterpret_cast<const void*>(&Impl::frame_gate))){Impl::active=nullptr;p.say("client mirror frame hook failed");return false;}mirror_frame_trampoline=reinterpret_cast<Frame>(p.frame_hook.trampoline);
 mirror_should_block_input=&Impl::should_block_input;
 if(!p.input_hook.install(kLocalThink,9,reinterpret_cast<const void*>(&mirror_input_gate))){p.frame_hook.remove();mirror_frame_trampoline=nullptr;mirror_should_block_input=nullptr;Impl::active=nullptr;p.say("client mirror input hook failed; frame hook rolled back");return false;}mirror_input_trampoline=reinterpret_cast<uintptr_t>(p.input_hook.trampoline);
 p.installed=true;p.say("client mirror hooks installed; waiting for fresh Moscow and two verified native actors");return true;
}
bool NativeClientMirror::remove_on_game_thread(){auto& p=*p_;if(!p.installed)return true;if(!p.on_owner())return false;uint32_t count=0;if(!read(kLevelCount,count)||count){p.say("client mirror remains frozen/resident: unload the map before removing hooks");return false;}if(!p.input_hook.remove()||!p.frame_hook.remove())return false;mirror_input_trampoline=0;mirror_frame_trampoline=nullptr;Impl::active=nullptr;p.installed=false;p.ready.store(false);p.frozen.store(false);p.say("client mirror hooks removed after map shutdown");return true;}
void NativeClientMirror::service_on_game_thread(){p_->service();}
void NativeClientMirror::queue_snapshot(const cod2coop::net::Snapshot& snapshot){cod2coop::net::Snapshot validated;if(!cod2coop::net::deserialize(cod2coop::net::serialize(snapshot),validated))return;std::lock_guard<std::mutex> lock(p_->queue_mutex);if(!p_->pending_snapshot||cod2coop::net::newer(validated.tick,p_->pending_snapshot->tick))p_->pending_snapshot=std::move(validated);}
void NativeClientMirror::queue_world_updates(const std::vector<cod2coop::net::WorldUpdate>& pages){std::lock_guard<std::mutex> lock(p_->queue_mutex);for(const auto& page:pages){cod2coop::net::WorldUpdate validated;if(!cod2coop::net::deserialize(cod2coop::net::serialize(page),validated))continue;if(p_->pending_world.size()>=128)p_->pending_world.pop_front();p_->pending_world.push_back(std::move(validated));}}
void NativeClientMirror::set_connected(bool connected){p_->connected.store(connected);if(!connected){std::lock_guard<std::mutex> lock(p_->queue_mutex);p_->outgoing.clear();p_->pending_snapshot.reset();p_->pending_world.clear();}}
std::vector<cod2coop::net::Input> NativeClientMirror::take_inputs(){std::lock_guard<std::mutex> lock(p_->queue_mutex);auto result=std::move(p_->outgoing);p_->outgoing.clear();return result;}
bool NativeClientMirror::authorize_template_on_game_thread(const ClientMirrorTemplate& entry){auto& p=*p_;if(!p.on_owner()||!p.ready.load()||!p.validate_template||!entry.model_key||entry.network_entity<=2||entry.native_slot<2||entry.native_slot>2045||entry.map_epoch!=p.authority_epoch.load()||!entry.map_epoch||p.bindings.size()>=1280)return false;std::array<uint8_t,kEntityStride> local{};if(!read_bytes(kEntities+size_t(entry.native_slot)*kEntityStride,local.data(),local.size())||!local[0xa4]||local[0]!=entry.native_type||!p.validate_template(entry,local.data(),local.size()))return false;Impl::Binding binding;binding.entry=entry;std::memcpy(&binding.model_index,local.data()+2,2);std::memcpy(&binding.classname,local.data()+0x118,2);p.bindings[entry.network_entity]=binding;return true;}
bool NativeClientMirror::frozen()const{return p_->frozen.load();}
std::string NativeClientMirror::status()const{const auto& p=*p_;std::string state;{std::lock_guard<std::mutex> lock(p.state_mutex);state=p.state;}std::ostringstream out;out<<state<<"; frozen_frames="<<p.frozen_frames.load()<<" local_think_blocks="<<p.blocked_thinks.load()<<" snapshots_applied="<<p.applied_snapshots.load()<<" world_applied="<<p.applied_world.load()<<" world_rejected="<<p.rejected_world.load()<<" inputs_captured="<<p.captured_inputs.load()<<" epoch="<<p.authority_epoch.load();return out.str();}
}
