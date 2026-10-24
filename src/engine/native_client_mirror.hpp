#pragma once
#include "networking/protocol.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace coop {
struct ClientMirrorStorage {
 uintptr_t clients=0,server_clients=0;
 bool arrays_prepared=false,second_actor_ready=false;
};
struct ClientMirrorTemplate {
 uint32_t network_entity=0,map_epoch=0;
 uint16_t native_slot=0xffff,native_type=0;
 uint64_t model_key=0; // verified asset/template key from an explicit bootstrap
};
// Opt-in original-SP thin-client experiment. Install/service/remove and template
// authorization run on the game thread. Queue/take/status methods are worker-safe.
// Requires the separately version-checked two-client pre-map storage experiment.
class NativeClientMirror {
public:
 using StorageProvider=std::function<ClientMirrorStorage()>;
 using ExecuteCommand=std::function<void(const char*)>;
 using ReadDvar=std::function<std::string(const char*)>;
 using Log=std::function<void(const std::string&)>;
 using ValidateTemplate=std::function<bool(const ClientMirrorTemplate&,const uint8_t*,size_t)>;
 NativeClientMirror();~NativeClientMirror();
 NativeClientMirror(const NativeClientMirror&)=delete;
 NativeClientMirror& operator=(const NativeClientMirror&)=delete;
 bool install_on_game_thread(StorageProvider,ExecuteCommand,ReadDvar,Log,ValidateTemplate={});
 bool remove_on_game_thread(); // fails closed while any original map is active
 void service_on_game_thread();
 void queue_snapshot(const cod2coop::net::Snapshot&);
 void queue_world_updates(const std::vector<cod2coop::net::WorldUpdate>&);
 void set_connected(bool);
 std::vector<cod2coop::net::Input> take_inputs();
 bool authorize_template_on_game_thread(const ClientMirrorTemplate&);
 bool frozen()const;
 std::string status()const;
private:
 struct Impl;std::unique_ptr<Impl> p_;
};
}
