#include "compatibility/platform.hpp"
#include "networking/session.hpp"
#include "engine/bridge.hpp"
#include <tlhelp32.h>
#include <atomic>
#include <fstream>
#include <mutex>
#include <sstream>
#include <chrono>
#include <algorithm>
namespace {
HMODULE module_handle=nullptr; HHOOK message_hook=nullptr; HWND game_window=nullptr;
std::atomic<unsigned long> callbacks{0}; std::atomic<DWORD> callback_thread{0};
std::mutex log_mutex; std::filesystem::path log_path; DWORD window_thread=0;
coop::EngineBridge engine;std::atomic<int> engine_action{0};std::atomic<bool> engine_callback_verified{false};
void log(const std::string& type,const std::string& detail){std::lock_guard<std::mutex> lock(log_mutex);std::ofstream f(log_path,std::ios::app);f<<"{\"tick_ms\":"<<GetTickCount64()<<",\"pid\":"<<GetCurrentProcessId()<<",\"thread\":"<<GetCurrentThreadId()<<",\"event\":"<<coop::json(type)<<",\"detail\":"<<coop::json(detail)<<"}\n";}
LRESULT CALLBACK message_callback(int code,WPARAM w,LPARAM l){if(code>=0){callback_thread.store(GetCurrentThreadId(),std::memory_order_relaxed);callbacks.fetch_add(1,std::memory_order_relaxed);auto* msg=reinterpret_cast<MSG*>(l);if(w==PM_REMOVE && msg->message==WM_NULL && msg->wParam==0xC002){int expected=1;if(engine_action.compare_exchange_strong(expected,0))engine_callback_verified.store(engine.install_on_game_thread());if(engine_callback_verified.load()){engine.sample_on_game_thread();engine.service_on_game_thread();}expected=2;if(engine_action.compare_exchange_strong(expected,0)){engine.remove_on_game_thread();engine_action.store(3);}}}return CallNextHookEx(nullptr,code,w,l);}
BOOL CALLBACK find_window(HWND window,LPARAM){DWORD pid=0;DWORD thread=GetWindowThreadProcessId(window,&pid);if(pid==GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window,GW_OWNER)){game_window=window;window_thread=thread;return FALSE;}return TRUE;}
void modules_report(const std::filesystem::path& dir){coop::Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId()));MODULEENTRY32W e{};e.dwSize=sizeof(e);std::ofstream out(dir/L"modules.jsonl");if(Module32FirstW(snap,&e))do{out<<"{\"name\":"<<coop::json(coop::utf8(e.szModule))<<",\"path\":"<<coop::json(coop::utf8(e.szExePath))<<",\"base\":"<<reinterpret_cast<uintptr_t>(e.modBaseAddr)<<",\"size\":"<<e.modBaseSize<<"}\n";}while(Module32NextW(snap,&e));}
bool readable(DWORD p){return !(p&(PAGE_GUARD|PAGE_NOACCESS)) && (p&(PAGE_READONLY|PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
void live_index(const std::filesystem::path& dir){
 // Observe the normally loaded process; never emit a rebuilt executable or modify protected code.
 auto* base=reinterpret_cast<unsigned char*>(GetModuleHandleW(nullptr));auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(base+dos->e_lfanew);
 const size_t size=nt->OptionalHeader.SizeOfImage;size_t readable_bytes=0,executable_bytes=0,string_count=0;
 std::ofstream regions(dir/L"live_regions.jsonl"),strings(dir/L"live_strings.jsonl");
 for(size_t offset=0;offset<size;){MEMORY_BASIC_INFORMATION info{};if(!VirtualQuery(base+offset,&info,sizeof(info)))break;auto begin=std::max(reinterpret_cast<uintptr_t>(base+offset),reinterpret_cast<uintptr_t>(info.BaseAddress));auto end=std::min(reinterpret_cast<uintptr_t>(base+size),reinterpret_cast<uintptr_t>(info.BaseAddress)+static_cast<uintptr_t>(info.RegionSize));if(end<=begin)break;
 regions<<"{\"rva\":"<<(begin-reinterpret_cast<uintptr_t>(base))<<",\"size\":"<<(end-begin)<<",\"state\":"<<info.State<<",\"protection\":"<<info.Protect<<"}\n";
 if(info.State==MEM_COMMIT && readable(info.Protect)){readable_bytes+=end-begin;if(info.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))executable_bytes+=end-begin;
 std::vector<unsigned char> bytes(end-begin);SIZE_T copied=0;if(ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(begin),bytes.data(),bytes.size(),&copied)){size_t start=0;for(size_t i=0;i<=copied;++i){if(i<copied && bytes[i]>=32 && bytes[i]<127)continue;if(i-start>=6 && i-start<=1024 && i<copied && bytes[i]==0){std::string s(reinterpret_cast<char*>(bytes.data()+start),i-start);strings<<"{\"rva\":"<<(begin-reinterpret_cast<uintptr_t>(base)+start)<<",\"value\":"<<coop::json(s)<<"}\n";++string_count;}start=i+1;}}}offset=end-reinterpret_cast<uintptr_t>(base);
 }
 log("live_index","readable_bytes="+std::to_string(readable_bytes)+" executable_bytes="+std::to_string(executable_bytes)+" strings="+std::to_string(string_count));
}
void remove_hooks(){
 if(engine.ready() && window_thread){engine_action.store(2);for(unsigned i=0;i<100 && engine_action.load()!=3;++i){PostThreadMessageW(window_thread,WM_NULL,0xC002,0);Sleep(10);}log("engine_command_cleanup",engine_action.load()==3?"removed on game thread":"callback did not run; DLL remains resident, do not unload");}
 if(message_hook){UnhookWindowsHookEx(message_hook);message_hook=nullptr;}
}
struct HookScope {bool active=true;~HookScope(){if(!active)return;try{remove_hooks();}catch(...){if(message_hook)UnhookWindowsHookEx(message_hook);}}};
DWORD WINAPI initialize(void*){
 try{
 auto dll=coop::module_path(module_handle);auto config=dll.parent_path()/L"runtime-config"/(std::to_wstring(GetCurrentProcessId())+L".ini");wchar_t target[32768]{};auto fallback=dll.parent_path()/L"runtime";
 GetPrivateProfileStringW(L"runtime",L"logs",fallback.c_str(),target,32768,config.c_str());auto dir=std::filesystem::path(target)/std::to_wstring(GetCurrentProcessId());std::filesystem::create_directories(dir);log_path=dir/L"runtime.jsonl";
 log("bootstrap","COD2 CO-OP — ALPHA 0.1.0; Developed by Heperkoo");
 auto exe=coop::module_path();if(!coop::supported_sp(exe)){log("unsupported_executable","No hooks installed");return 1;}
 log("compatibility",coop::sha256(exe));modules_report(dir);
 const bool experimental=GetPrivateProfileIntW(L"runtime",L"experimental_player2",0,config.c_str())==1;engine.configure_experimental_player2(experimental);if(experimental){HMODULE pinned=nullptr;GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&initialize),&pinned);log("experimental_player2","enabled; DLL pinned; fresh-map experiment only");}
 cod2coop::net::Session session;wchar_t role[32]{},host[80]{},bind[80]{};
 GetPrivateProfileStringW(L"network",L"role",L"off",role,32,config.c_str());
 GetPrivateProfileStringW(L"network",L"host",L"127.0.0.1",host,80,config.c_str());
 GetPrivateProfileStringW(L"network",L"bind",L"127.0.0.1",bind,80,config.c_str());
 cod2coop::net::Config network;network.bind_address=coop::utf8(bind);network.port=static_cast<uint16_t>(GetPrivateProfileIntW(L"network",L"port",28961,config.c_str()));network.executable_sha256=coop::sp_hash;network.soldier=static_cast<uint8_t>(GetPrivateProfileIntW(L"network",L"soldier",0,config.c_str()));
 engine.select_soldier(network.soldier);
 if(wcscmp(role,L"host")==0)session.host(network);else if(wcscmp(role,L"client")==0)session.join(network,coop::utf8(host));
 log("network",session.status());log("engine_bridge","UNAVAILABLE: no validated SP player allocation/usercmd/physics/script interfaces. No entities or simulated gameplay emitted.");
 // The sole callback is a reversible documented Win32 message hook. It is NOT a recovered simulation frame function.
 for(unsigned attempt=0;attempt<120 && !game_window;++attempt){EnumWindows(find_window,0);if(!game_window)Sleep(250);}
 if(!game_window){log("hook_failed","No game window found within 30 seconds");return 2;}
 message_hook=SetWindowsHookExW(WH_GETMESSAGE,message_callback,module_handle,window_thread);
 if(!message_hook){log("hook_failed","SetWindowsHookEx error="+std::to_string(GetLastError()));return 3;}
 HookScope hook_scope;
 log("hook_installed","WH_GETMESSAGE; window_thread="+std::to_string(window_thread)+"; no-op thread messages, no game timers replaced");
 std::string engine_evidence;if(engine.prepare(engine_evidence)){log("engine_imports_verified",engine_evidence);engine_action.store(1);}else log("engine_imports_unavailable",engine_evidence);
 live_index(dir);
 wchar_t event_name[80]{};swprintf_s(event_name,L"Local\\COD2CoopStop_%lu",GetCurrentProcessId());coop::Handle stop(CreateEventW(nullptr,TRUE,FALSE,event_name));
 swprintf_s(event_name,L"Local\\COD2CoopStopped_%lu",GetCurrentProcessId());coop::Handle completed(CreateEventW(nullptr,TRUE,FALSE,event_name));if(completed)ResetEvent(completed);
 std::vector<HANDLE> action_events;for(unsigned i=1;i<=9;++i){swprintf_s(event_name,L"Local\\COD2CoopAction%u_%lu",i,GetCurrentProcessId());action_events.push_back(CreateEventW(nullptr,FALSE,FALSE,event_name));}
 uint64_t last_report=0,last_pulse=0,last_snapshot=0,last_world=0,world_interval=200,window_missing_since=0;uint32_t snapshot_tick=0;std::string displayed_error,remote_position;bool was_connected=false;
 for(;;){
  auto result=stop?WaitForSingleObject(stop,10):WAIT_FAILED;
  if(result==WAIT_OBJECT_0 || result==WAIT_FAILED)break;
  if(!IsWindow(game_window)){if(!window_missing_since)window_missing_since=GetTickCount64();HWND previous=game_window;EnumWindows(find_window,0);if(game_window==previous && GetTickCount64()-window_missing_since>30000)break;}else window_missing_since=0;
  auto now=cod2coop::net::monotonic_milliseconds();session.tick(now);
  const bool connected=session.state()==cod2coop::net::State::Connected;if(was_connected&&!connected){engine.queue_peer_input(0,0,0,0);remote_position.clear();}was_connected=connected;
  for(const auto& input:session.drain_inputs())engine.queue_peer_input(input.forward,input.right,input.yaw,input.pitch);
  for(unsigned i=0;i<action_events.size();++i)if(action_events[i]&&WaitForSingleObject(action_events[i],0)==WAIT_OBJECT_0){engine.queue_native_action(i+1);log("native_action","queued action="+std::to_string(i+1));}
  for(const auto& request:engine.take_ui_requests()){
   displayed_error.clear();network.bind_address=request.bind;network.port=static_cast<uint16_t>(request.port);network.soldier=static_cast<uint8_t>(request.soldier);
   switch(request.action){
    case coop::UiAction::Host:session.disconnect();session.host(network);log("native_menu","host requested: "+session.status());break;
    case coop::UiAction::Join:if(!engine.observed_mission().empty()){displayed_error="Leave the campaign before joining; client world mirror pending";break;}session.disconnect();session.join(network,request.address);log("native_menu","join requested: "+session.status());break;
    case coop::UiAction::Start:if(session.is_host()&&(session.state()==cod2coop::net::State::Listening||session.state()==cod2coop::net::State::Connected)){engine.queue_moscow();log("native_menu","queued original map moscow");}else displayed_error="Create the host session before starting Moscow";break;
    case coop::UiAction::Disconnect:session.disconnect();log("native_menu","disconnect requested");break;
    case coop::UiAction::Soldier:engine.select_soldier(request.soldier);log("native_menu","soldier preference="+std::to_string(request.soldier)+" for next native spawn");break;
   }
  }
  if(session.is_host()&&session.state()==cod2coop::net::State::Connected&&now-last_snapshot>=50){
   auto native=engine.observation();if(native.valid){cod2coop::net::Snapshot snapshot;snapshot.tick=++snapshot_tick;snapshot.players[0].entity=1;snapshot.players[0].slot=1;snapshot.players[0].health=0;snapshot.players[0].x=native.x;snapshot.players[0].y=native.y;snapshot.players[0].z=native.z;snapshot.players[1]={};snapshot.players[1].health=0;if(native.second_valid){snapshot.players[1].entity=2;snapshot.players[1].slot=2;snapshot.players[1].health=static_cast<int16_t>(std::clamp(native.second_health,0,10000));snapshot.players[1].x=native.second_x;snapshot.players[1].y=native.second_y;snapshot.players[1].z=native.second_z;}session.send_snapshot(snapshot);}last_snapshot=now;
  }
  if(auto snapshot=session.take_snapshot()){remote_position=" host position "+std::to_string(snapshot->players[0].x)+","+std::to_string(snapshot->players[0].y)+","+std::to_string(snapshot->players[0].z);if(now-last_report>=1000)log("native_snapshot_received",remote_position+"; not applied to client world");}
  if(session.is_host()&&session.state()==cod2coop::net::State::Connected&&now-last_world>=world_interval&&engine.observation().valid){auto world=engine.world_observation();if(world.valid){auto pages=cod2coop::native_world_pages(world);world_interval=std::max<uint64_t>(200,(pages.size()*1000+179)/180);for(const auto& page:pages)session.send_world_update(page);}last_world=now;}
  auto received_world=session.drain_world_updates();if(!received_world.empty()&&now-last_report>=1000)log("native_world_received","pages="+std::to_string(received_world.size())+"; native client application pending");
  engine.publish_session_status(session.is_host()?"host":"client",displayed_error.empty()?session.status()+remote_position:displayed_error);
  if(now-last_pulse>=50){PostThreadMessageW(window_thread,WM_NULL,0xC002,0);last_pulse=now;}
  if(now-last_report<1000)continue;
  last_report=now;
  log("message_callback","count="+std::to_string(callbacks.load())+" callback_thread="+std::to_string(callback_thread.load())+" expected_thread="+std::to_string(window_thread));
  bool verified=engine_callback_verified.load();
  log("engine_command_callback","verified="+std::to_string(verified)+" count="+std::to_string(engine.callbacks())+" command_thread="+std::to_string(engine.command_thread()));
  if(verified){
   if(experimental)log("experimental_player2",engine.experimental_state());
   log("observed_mission",engine.observed_mission());
   auto observed=engine.observation();
   log("native_player_observation","valid="+std::to_string(observed.valid)+" entities="+std::to_string(observed.entities)+" client_capacity="+std::to_string(observed.client_capacity)+" client_backed_entities="+std::to_string(observed.player_entities)+" origin="+std::to_string(observed.x)+","+std::to_string(observed.y)+","+std::to_string(observed.z));
  }
  auto stats=session.statistics();
  log("network",std::string(cod2coop::net::state_name(session.state()))+" "+session.status()+" packets="+std::to_string(stats.received_packets)+" rtt_ms="+std::to_string(stats.round_trip_ms));
 }
 session.disconnect();
 for(auto event:action_events)if(event)CloseHandle(event);
 remove_hooks();hook_scope.active=false;log("shutdown",experimental?"Message hook removed; experiment restore result logged; original disk EXE unchanged":"Hook removed; no engine memory patches applied");if(completed)SetEvent(completed);
 }catch(const std::exception& e){if(!log_path.empty())log("exception",e.what());return 4;}return 0;
}
}
extern "C" __declspec(dllexport) unsigned __cdecl COD2CoopRuntimeVersion(){return 0x000100;}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID){if(reason==DLL_PROCESS_ATTACH){module_handle=instance;DisableThreadLibraryCalls(instance);HANDLE thread=CreateThread(nullptr,2*1024*1024,initialize,nullptr,STACK_SIZE_PARAM_IS_A_RESERVATION,nullptr);if(thread)CloseHandle(thread);}return TRUE;}
