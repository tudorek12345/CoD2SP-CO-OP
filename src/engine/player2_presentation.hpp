#pragma once
#include <windows.h>
#include <atomic>
#include <array>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
namespace coop {
// Presentation attaches only to actual client-backed engine entities. It does
// not allocate an NPC, substitute a visual clone, or advance mission scripts.
class Player2Presentation {
public:
 using Log=std::function<void(const std::string&)>;
 bool prepare_on_game_thread(Log logger);
 bool assign_body_on_game_thread(uintptr_t entity,unsigned soldier);
 bool install_nametag_on_game_thread(uintptr_t entity,const std::string& name);
 void uninstall_on_game_thread();
 void set_name(const std::string& name);
 bool head_on_game_thread(uintptr_t entity,float output[3])const;
 std::string state_report()const;
 std::string diagnostic_report_on_game_thread()const;
private:
 static Player2Presentation* active_;
 static void __cdecl render_scene_gate(const void* refdef);
 void render_tag(const void* refdef);
 void render_body();
 void update_animation();
 void report(const std::string& text);
 bool ready_=false,installed_=false;
 DWORD thread_=0;
 HMODULE renderer_=nullptr;
 void* trampoline_=nullptr;
 uintptr_t actor_=0;
 uint16_t head_tag_=0;
 uintptr_t animation_tree_=0;
 unsigned idle_anim_=0,walk_anim_=0,run_anim_=0,last_anim_=0;
 bool animation_interfaces_=false;
 bool client_head_interface_=false;
 std::array<float,12> last_view_{};
 std::array<int32_t,4> last_viewport_{};
 std::array<float,2> last_fov_{};
 DWORD last_view_time_=0;
 Log log_;
 mutable std::mutex mutex_;
 std::string name_="Player 2",state_="disabled";
 std::atomic<uint32_t> draw_commands_{0};
 std::atomic<uint32_t> body_commands_{0};
};
}
