#pragma once
#include "types.hpp"
#include "world.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>

namespace ilands {

constexpr uint16_t LAN_DEFAULT_TCP_PORT = 25575;
constexpr uint16_t LAN_DISCOVERY_UDP_PORT = 25574;

struct DiscoveredLanGame {
    std::string ip;
    uint16_t port = LAN_DEFAULT_TCP_PORT;
    std::string worldTitle;
    std::string hostName;
    int playerCount = 1;
    WorldType worldType = WorldType::Normal;
    float lastSeenTime = 0.0f;
};

struct RemotePlayer {
    uint32_t id = 0;
    std::string name;
    Vec3 prevPos;
    Vec3 targetPos;
    Vec3 renderPos;
    float yaw = 0.0f;
    float pitch = 0.0f;
    float interpAlpha = 1.0f;
};

struct NetChatEvent {
    std::string sender;
    std::string message;
};

struct NetBlockEvent {
    int x = 0, y = 0, z = 0;
    BlockType type = BlockType::Air;
    bool isDestroy = false;
    uint32_t senderId = 0;
};

struct NetWelcomeData {
    bool received = false;
    uint32_t localPlayerId = 0;
    int seed = 1;
    WorldType worldType = WorldType::Normal;
    float hourOfDay = 12.0f;
    std::string worldTitle;
    std::vector<WorldUpdateEntry> updates;
};

enum class NetRole {
    Offline,
    Host,
    Client
};

class LanNetwork {
public:
    LanNetwork();
    ~LanNetwork();

    bool InitWinsock();
    void Shutdown();

    void StartDiscovery();
    void StopDiscovery();
    void PollDiscovery(float nowSec);
    const std::vector<DiscoveredLanGame>& GetDiscoveredGames() const { return discovered_; }

    bool StartHosting(const std::string& worldTitle, const std::string& hostName,
                      int seed, WorldType worldType, float hourOfDay,
                      const std::vector<WorldUpdateEntry>& currentUpdates,
                      const Vec3& hostPos);
    void StopHosting();

    bool ConnectToHost(const std::string& ip, uint16_t port, const std::string& playerName, std::string& outError);
    void Disconnect();

    void Update(float dt, float nowSec, const Vec3& localPos, float localYaw, float localPitch, float hourOfDay);

    void SendBlockChange(int x, int y, int z, BlockType type, bool isDestroy);
    void SendChatMessage(const std::string& text);

    void HostRecordWorldUpdate(const WorldUpdateEntry& entry);

    bool PopWelcome(NetWelcomeData& out);
    std::vector<NetChatEvent> TakeChatEvents();
    std::vector<NetBlockEvent> TakeBlockEvents();
    bool ConsumeTimeSync(float& outHour);
    bool ConsumeDisconnected(std::string& outReason);

    NetRole role() const { return role_; }
    bool isHosting() const { return role_ == NetRole::Host; }
    bool isClient() const { return role_ == NetRole::Client; }
    bool isMultiplayer() const { return role_ != NetRole::Offline; }
    uint16_t hostPort() const { return hostPort_; }
    std::string localIp() const { return localIp_; }
    int totalPlayerCount() const { return 1 + static_cast<int>(remotePlayers_.size()); }
    const std::unordered_map<uint32_t, RemotePlayer>& remotePlayers() const { return remotePlayers_; }

    static std::string DetectLocalIPv4();
    static std::string GetDefaultPlayerName();

private:
    struct ClientPeer {
        uintptr_t sock = ~static_cast<uintptr_t>(0);
        uint32_t id = 0;
        std::string name;
        bool handshakeDone = false;
        Vec3 pos{128.0f, 40.0f, 128.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        std::vector<uint8_t> recvBuf;
    };

    void HostAcceptClients(const Vec3& hostPos, float hourOfDay);
    void HostBroadcastBeacon(float nowSec);
    void HostHandlePacket(ClientPeer& peer, uint8_t msgType, const uint8_t* payload, uint32_t len, const Vec3& hostPos, float hourOfDay);
    void ClientHandlePacket(uint8_t msgType, const uint8_t* payload, uint32_t len);

    bool SendFrame(uintptr_t sock, uint8_t msgType, const std::vector<uint8_t>& payload);
    void BroadcastFrameToClients(uint8_t msgType, const std::vector<uint8_t>& payload, uint32_t excludeId = 0);
    bool DrainSocket(uintptr_t sock, std::vector<uint8_t>& buf, bool& outClosed);

    bool wsaReady_ = false;
    NetRole role_ = NetRole::Offline;
    std::string localIp_ = "127.0.0.1";

    uintptr_t discoverySock_ = ~static_cast<uintptr_t>(0);
    std::vector<DiscoveredLanGame> discovered_;

    uintptr_t listenSock_ = ~static_cast<uintptr_t>(0);
    uintptr_t beaconSock_ = ~static_cast<uintptr_t>(0);
    uint16_t hostPort_ = LAN_DEFAULT_TCP_PORT;
    std::string hostWorldTitle_;
    std::string hostName_;
    int hostSeed_ = 1;
    WorldType hostWorldType_ = WorldType::Normal;
    std::vector<WorldUpdateEntry> hostUpdates_;
    std::vector<ClientPeer> peers_;
    uint32_t nextClientId_ = 2;
    float lastBeaconTime_ = -10.0f;
    float lastTimeSync_ = 0.0f;

    uintptr_t clientSock_ = ~static_cast<uintptr_t>(0);
    uint32_t localPlayerId_ = 1;
    std::string clientName_;
    std::vector<uint8_t> clientRecvBuf_;
    NetWelcomeData welcome_;
    bool welcomePending_ = false;
    bool disconnectedFlag_ = false;
    std::string disconnectReason_;
    bool hasTimeSync_ = false;
    float syncedHour_ = 12.0f;

    float transformSendAccum_ = 0.0f;
    std::unordered_map<uint32_t, RemotePlayer> remotePlayers_;
    std::vector<NetChatEvent> chatQueue_;
    std::vector<NetBlockEvent> blockQueue_;
};

}
