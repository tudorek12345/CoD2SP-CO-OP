#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cod2coop::net {
constexpr char kProtocolName[] = "COD2COOP_PROTOCOL_1";
constexpr uint16_t kProtocolVersion = 1;
constexpr size_t kHeaderBytes = 32;
constexpr size_t kMaxPacketBytes = 1200;
constexpr uint32_t kMaxEventData = 512;
constexpr size_t kMaxWorldEntities = 20;
constexpr uint16_t kMaxWorldPages = 64;
using Bytes = std::vector<uint8_t>;
enum class Kind : uint16_t { Hello=1, Welcome, Reject, Input, Snapshot, Event, Ack, Heartbeat, Disconnect, Resync, WorldUpdate };
enum class EventType : uint16_t { Spawn=1, Despawn, Weapon, Damage, AI, Objective, Script, Checkpoint };
struct Header { Kind kind{}; uint64_t session=0; uint32_t sequence=0; uint32_t ack=0; };
struct Packet { Header header; Bytes payload; };
struct Hello { std::string executable_sha256; std::string mission="moscow"; uint8_t soldier=0; };
struct Welcome { uint64_t nonce=0; uint8_t slot=2; std::string mission="moscow"; };
struct Input { uint32_t command=0, milliseconds=0; float forward=0, right=0, up=0, yaw=0, pitch=0; uint16_t buttons=0; };
struct Player { uint32_t entity=0; uint8_t slot=0, stance=0; uint16_t weapon=0; float x=0,y=0,z=0,yaw=0,pitch=0,vx=0,vy=0,vz=0; int16_t health=100; };
struct Snapshot { uint32_t tick=0, last_command=0; std::array<Player,2> players{}; };
struct Event { uint32_t id=0, entity=0; EventType type{}; Bytes data; };
struct WorldEntity {
 uint32_t entity=0; // session-stable network ID, never a process pointer
 uint16_t native_slot=0xffff; // bounded initial-template hint, not network identity
 uint16_t native_type=0;
 uint16_t fields=1; // bit 0: observed position, bit 1: observed rotation
 float x=0,y=0,z=0,pitch=0,yaw=0,roll=0;
};
struct WorldUpdate {
 uint32_t tick=0;
 uint32_t map_epoch=1;
 uint16_t page=0,pages=1;
 std::vector<WorldEntity> entities;
};
Bytes encode(const Packet&);
bool decode(const uint8_t*, size_t, Packet&, std::string& error);
Bytes serialize(const Hello&); bool deserialize(const Bytes&, Hello&);
Bytes serialize(const Welcome&); bool deserialize(const Bytes&, Welcome&);
Bytes serialize(const Input&); bool deserialize(const Bytes&, Input&);
Bytes serialize(const Snapshot&); bool deserialize(const Bytes&, Snapshot&);
Bytes serialize(const Event&); bool deserialize(const Bytes&, Event&);
Bytes serialize(const WorldUpdate&); bool deserialize(const Bytes&, WorldUpdate&);
bool valid_fingerprint(const std::string&);
bool newer(uint32_t candidate, uint32_t previous);
}
