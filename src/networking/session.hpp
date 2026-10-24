#pragma once
#include "networking/protocol.hpp"
#include <memory>
#include <optional>

namespace cod2coop::net {
enum class State { Idle, Listening, Connecting, Connected, Closed, Failed };
struct Config {
 std::string bind_address="127.0.0.1";
 uint16_t port=28961;
 std::string executable_sha256;
 uint8_t soldier=0;
 uint32_t timeout_ms=5000;
};
struct Statistics {
 uint64_t sent_packets=0, received_packets=0, sent_bytes=0, received_bytes=0;
 uint64_t rejected_packets=0, stale_packets=0, duplicate_events=0, retries=0, rate_limited=0;
 uint64_t snapshots=0, inputs=0, events=0, world_updates=0, world_entities=0;
 uint32_t round_trip_ms=0;
};
// Tick and every other member must be called by one owning thread. No engine
// pointers cross this interface; callers apply drained inputs/events on the game thread.
class Session {
public:
 Session(); ~Session();
 Session(const Session&)=delete; Session& operator=(const Session&)=delete;
 bool host(const Config&);
 bool join(const Config&,const std::string& host_address);
 void tick(uint64_t monotonic_ms);
 bool send_input(const Input&);
 bool send_snapshot(const Snapshot&);
 bool send_world_update(const WorldUpdate&); // Host-only transient paged transforms.
 bool send_event(Event); // Host only. IDs allocated internally, strictly ordered.
 bool request_resync();
 void disconnect();
 State state()const;
 bool is_host()const;
 uint16_t local_port()const;
 std::string peer_address()const;
 std::string status()const;
 Statistics statistics()const;
 std::vector<Input> drain_inputs();
 std::vector<Event> drain_events();
 std::optional<Snapshot> take_snapshot();
 std::vector<WorldUpdate> drain_world_updates();
 bool take_resync_request();
private:
 struct Impl;std::unique_ptr<Impl> p_;
};
uint64_t monotonic_milliseconds();
const char* state_name(State);
}
