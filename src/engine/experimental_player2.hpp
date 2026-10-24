#pragma once
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include "engine/player2_presentation.hpp"

namespace coop {
struct ExperimentalPlayerObservation {
 bool valid=false;
 uint32_t entity_id=1;
 float x=0,y=0,z=0;
 int health=0;
 uint32_t sentient=0;
};

// Deliberately opt-in, fresh-map, one-shot engine experiment. The stable runtime
// must never install this automatically. All entrypoints run on the game thread.
class ExperimentalPlayer2 {
public:
 using Log=std::function<void(const std::string&)>;
 bool install_on_game_thread(Log logger);
 void uninstall_on_game_thread();
 bool apply_input_on_game_thread(int forward,int right,float yaw,float pitch);
 bool place_near_host_on_game_thread();
 std::string diagnostic_report_on_game_thread()const;
 void set_soldier(unsigned selected){soldier_.store(selected<2?selected:0);}
 bool actor_ready()const;
 bool arrays_prepared()const{return arrays_prepared_.load();}
 uintptr_t clients_base()const{return clients_;} // game-thread observation only
 uintptr_t server_clients_base()const{return server_clients_;}
 std::string state_report()const;
 ExperimentalPlayerObservation observation_on_game_thread()const;
private:
 static ExperimentalPlayer2* active_;
 static void __cdecl bsp_gate();
 static void __cdecl host_spawn_gate(void* host);
 friend void __cdecl experimental_fix_spawn_index(unsigned index);
 friend bool __cdecl experimental_block_checkpoint();
 void before_bsp();
 void after_host_spawn(void* host);
 bool storage_valid()const;
 void state(const std::string& value);
 Log log_;
 mutable std::mutex mutex_;
 std::string state_="disabled";
 DWORD game_thread_=0;
 bool installed_=false,attempted_=false;
 std::atomic<bool> arrays_prepared_{false},actor_ready_{false};
 std::atomic<unsigned> soldier_{0};
 std::atomic<uintptr_t> clients_{0},server_clients_{0},old_server_clients_{0};
 uint32_t last_input_time_=0;
 std::atomic<uint32_t> input_count_{0};
 Player2Presentation presentation_;
 std::atomic<uint32_t> blocked_saves_{0};
};
}
