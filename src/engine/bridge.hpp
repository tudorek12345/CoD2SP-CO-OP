#pragma once
#include <windows.h>
#include <atomic>
#include <filesystem>
#include <string>
#include <mutex>
#include <deque>
#include <vector>
#include "engine/experimental_player2.hpp"
#include "replication/native_world.hpp"
namespace coop {
enum class UiAction { Host, Join, Start, Disconnect, Soldier };
struct UiRequest {UiAction action{};std::string address,bind;unsigned port=28961,soldier=0;};
struct NativePlayerObservation {bool valid=false;unsigned entities=0,client_capacity=0,player_entities=0;float x=0,y=0,z=0;bool second_valid=false;float second_x=0,second_y=0,second_z=0;int second_health=0;};
class EngineBridge {
public:
 bool prepare(std::string& evidence); // worker thread, read-only hash and import validation
 bool install_on_game_thread();
 void remove_on_game_thread();
 unsigned callbacks()const{return callbacks_.load();}
 bool ready()const{return prepared_;}
 DWORD command_thread()const{return command_thread_.load();}
 std::string observed_mission()const;
 void sample_on_game_thread();
 NativePlayerObservation observation()const;
 std::vector<UiRequest> take_ui_requests();
 void publish_session_status(const std::string& role,const std::string& status);
 void queue_moscow();
 void service_on_game_thread();
 void queue_native_action(unsigned action){native_action_.store(action);}
 void configure_experimental_player2(bool enabled){experimental_enabled_=enabled;}
 std::string experimental_state()const{return experiment_.state_report();}
 void queue_peer_input(float forward,float right,float yaw,float pitch);
 void select_soldier(unsigned selected){experiment_.set_soldier(selected);}
 cod2coop::NativeWorldObservation world_observation()const;
private:
 static void __cdecl status_command();
 static void __cdecl host_command();static void __cdecl join_command();static void __cdecl start_command();static void __cdecl disconnect_command();static void __cdecl soldier_command();
 void request(UiAction);
 using Add=void(__cdecl*)(const char*,void(__cdecl*)());
 using Remove=void(__cdecl*)(const char*);
 using Execute=void(__cdecl*)(int,const char*);
 using Print=void(__cdecl*)(int,const char*,...);
 using GetString=const char*(__cdecl*)(const char*);
 using Argc=int(__cdecl*)();using Argv=const char*(__cdecl*)(int);
 Add add_=nullptr;Remove remove_=nullptr;Execute execute_=nullptr;Print print_=nullptr;GetString get_string_=nullptr;GetString get_variant_=nullptr;Argc argc_=nullptr;Argv argv_=nullptr;
 char observed_mission_[64]{};
 mutable std::mutex observation_mutex_;NativePlayerObservation observation_{};bool native_layout_verified_=false;
 ULONGLONG last_observation_=0;
 std::vector<unsigned char> entity_buffer_;
 cod2coop::NativeWorldTracker world_tracker_;cod2coop::NativeWorldObservation world_{};uint32_t world_tick_=0,last_level_time_=0;
 std::mutex ui_mutex_;std::deque<UiRequest> requests_;std::string ui_status_,ui_role_="off";std::atomic<bool> start_map_{false};ULONGLONG last_ui_=0;
 std::string published_role_,published_status_;
 std::atomic<unsigned> native_action_{0};
 ExperimentalPlayer2 experiment_;bool experimental_enabled_=false;struct PeerInput {float forward=0,right=0,yaw=0,pitch=0;};PeerInput peer_input_{};bool peer_input_pending_=false;
 bool prepared_=false,registered_=false;std::atomic<unsigned> callbacks_{0};std::atomic<DWORD> command_thread_{0};
 static EngineBridge* active_;
};
}
