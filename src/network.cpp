#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "network.hpp"
#include <cstring>
#include <cstdio>
#include <sstream>
#include <algorithm>

namespace ilands {

namespace {

enum MsgType : uint8_t {
    MSG_HELLO = 1,
    MSG_WELCOME = 2,
    MSG_PLAYER_SPAWN = 3,
    MSG_PLAYER_LEAVE = 4,
    MSG_TRANSFORM = 5,
    MSG_BLOCK_UPDATE = 6,
    MSG_CHAT = 7,
    MSG_TIME_SYNC = 8
};

struct ByteWriter {
    std::vector<uint8_t> data;
    void u8(uint8_t v) { data.push_back(v); }
    void u16(uint16_t v) { size_t p = data.size(); data.resize(p + 2); std::memcpy(data.data() + p, &v, 2); }
    void i16(int16_t v) { size_t p = data.size(); data.resize(p + 2); std::memcpy(data.data() + p, &v, 2); }
    void i32(int32_t v) { size_t p = data.size(); data.resize(p + 4); std::memcpy(data.data() + p, &v, 4); }
    void u32(uint32_t v) { size_t p = data.size(); data.resize(p + 4); std::memcpy(data.data() + p, &v, 4); }
    void f32(float v) { size_t p = data.size(); data.resize(p + 4); std::memcpy(data.data() + p, &v, 4); }
    void str(const std::string& s) {
        uint16_t len = static_cast<uint16_t>(std::min<size_t>(s.size(), 512));
        u16(len);
        if (len > 0) {
            size_t p = data.size();
            data.resize(p + len);
            std::memcpy(data.data() + p, s.data(), len);
        }
    }
};

struct ByteReader {
    const uint8_t* ptr = nullptr;
    size_t len = 0;
    size_t pos = 0;

    bool has(size_t n) const { return pos + n <= len; }
    uint8_t u8() { if (!has(1)) return 0; return ptr[pos++]; }
    uint16_t u16() { uint16_t v = 0; if (has(2)) { std::memcpy(&v, ptr + pos, 2); pos += 2; } return v; }
    int16_t i16() { int16_t v = 0; if (has(2)) { std::memcpy(&v, ptr + pos, 2); pos += 2; } return v; }
    int32_t i32() { int32_t v = 0; if (has(4)) { std::memcpy(&v, ptr + pos, 4); pos += 4; } return v; }
    uint32_t u32() { uint32_t v = 0; if (has(4)) { std::memcpy(&v, ptr + pos, 4); pos += 4; } return v; }
    float f32() { float v = 0; if (has(4)) { std::memcpy(&v, ptr + pos, 4); pos += 4; } return v; }
    std::string str() {
        uint16_t l = u16();
        if (!has(l)) return {};
        std::string s(reinterpret_cast<const char*>(ptr + pos), l);
        pos += l;
        return s;
    }
};

void SetNonBlocking(SOCKET s) {
    u_long mode = 1;
    ioctlsocket(s, FIONBIO, &mode);
}

void CloseSock(uintptr_t& s) {
    if (s != INVALID_SOCKET) {
        closesocket(static_cast<SOCKET>(s));
        s = INVALID_SOCKET;
    }
}

}

LanNetwork::LanNetwork() = default;

LanNetwork::~LanNetwork() {
    Shutdown();
}

bool LanNetwork::InitWinsock() {
    if (wsaReady_) return true;
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
    wsaReady_ = true;
    localIp_ = DetectLocalIPv4();
    return true;
}

void LanNetwork::Shutdown() {
    StopHosting();
    Disconnect();
    StopDiscovery();
    if (wsaReady_) {
        WSACleanup();
        wsaReady_ = false;
    }
}

std::string LanNetwork::DetectLocalIPv4() {
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s != INVALID_SOCKET) {
        sockaddr_in dst{};
        dst.sin_family = AF_INET;
        dst.sin_port = htons(53);
        inet_pton(AF_INET, "8.8.8.8", &dst.sin_addr);
        if (connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst)) == 0) {
            sockaddr_in local{};
            int len = sizeof(local);
            if (getsockname(s, reinterpret_cast<sockaddr*>(&local), &len) == 0) {
                char buf[64] = {};
                if (inet_ntop(AF_INET, &local.sin_addr, buf, sizeof(buf))) {
                    closesocket(s);
                    return std::string(buf);
                }
            }
        }
        closesocket(s);
    }
    return "127.0.0.1";
}

std::string LanNetwork::GetDefaultPlayerName() {
    char buf[128] = {};
    DWORD sz = sizeof(buf);
    if (GetUserNameA(buf, &sz) && buf[0] != '\0') {
        std::string s(buf);
        if (s.size() > 16) s.resize(16);
        return s;
    }
    return "Player";
}

void LanNetwork::StartDiscovery() {
    if (!InitWinsock()) return;
    if (discoverySock_ != INVALID_SOCKET) return;

    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET) return;

    BOOL reuse = TRUE;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(LAN_DISCOVERY_UDP_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closesocket(s);
        return;
    }
    SetNonBlocking(s);
    discoverySock_ = static_cast<uintptr_t>(s);
    discovered_.clear();
}

void LanNetwork::StopDiscovery() {
    CloseSock(discoverySock_);
}

void LanNetwork::PollDiscovery(float nowSec) {
    if (discoverySock_ != INVALID_SOCKET) {
        char buf[512];
        sockaddr_in from{};
        int fromLen = sizeof(from);
        while (true) {
            int n = recvfrom(static_cast<SOCKET>(discoverySock_), buf, sizeof(buf) - 1, 0,
                             reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (n <= 0) break;
            buf[n] = '\0';
            std::string pkt(buf, n);
            if (pkt.rfind("ILANDS_LAN_V1|", 0) != 0) continue;

            std::vector<std::string> parts;
            std::istringstream iss(pkt);
            std::string tok;
            while (std::getline(iss, tok, '|')) parts.push_back(tok);
            if (parts.size() < 6) continue;

            char ipStr[64] = {};
            inet_ntop(AF_INET, &from.sin_addr, ipStr, sizeof(ipStr));
            DiscoveredLanGame g;
            g.ip = ipStr;
            g.port = static_cast<uint16_t>(std::atoi(parts[1].c_str()));
            g.playerCount = std::max(1, std::atoi(parts[2].c_str()));
            g.worldType = (std::atoi(parts[3].c_str()) == 1) ? WorldType::Flat : WorldType::Normal;
            g.hostName = parts[4];
            g.worldTitle = parts[5];
            g.lastSeenTime = nowSec;

            bool updated = false;
            for (auto& ex : discovered_) {
                bool sameEndpoint = (ex.ip == g.ip && ex.port == g.port);
                bool sameLocalHost = (ex.port == g.port && ex.hostName == g.hostName && ex.worldTitle == g.worldTitle &&
                                      (ex.ip == "127.0.0.1" || g.ip == "127.0.0.1"));
                if (sameEndpoint || sameLocalHost) {
                    if (g.ip == "127.0.0.1" && ex.ip != "127.0.0.1") {
                        g.ip = ex.ip;
                    }
                    ex = g;
                    updated = true;
                    break;
                }
            }
            if (!updated) discovered_.push_back(g);
        }
    }

    discovered_.erase(std::remove_if(discovered_.begin(), discovered_.end(),
                                     [nowSec](const DiscoveredLanGame& g) { return (nowSec - g.lastSeenTime) > 4.5f; }),
                      discovered_.end());
}

bool LanNetwork::StartHosting(const std::string& worldTitle, const std::string& hostName,
                              int seed, WorldType worldType, float hourOfDay,
                              const std::vector<WorldUpdateEntry>& currentUpdates,
                              const Vec3& hostPos) {
    (void)hourOfDay;
    (void)hostPos;
    if (!InitWinsock()) return false;
    StopHosting();

    SOCKET ls = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ls == INVALID_SOCKET) return false;

    BOOL reuse = TRUE;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    uint16_t boundPort = 0;
    for (uint16_t p = LAN_DEFAULT_TCP_PORT; p < LAN_DEFAULT_TCP_PORT + 12; ++p) {
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(p);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (bind(ls, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) {
            boundPort = p;
            break;
        }
    }
    if (boundPort == 0 || listen(ls, 8) != 0) {
        closesocket(ls);
        return false;
    }
    SetNonBlocking(ls);

    SOCKET bs = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (bs != INVALID_SOCKET) {
        BOOL bcast = TRUE;
        setsockopt(bs, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<const char*>(&bcast), sizeof(bcast));
        SetNonBlocking(bs);
    }

    listenSock_ = static_cast<uintptr_t>(ls);
    beaconSock_ = static_cast<uintptr_t>(bs);
    hostPort_ = boundPort;
    hostWorldTitle_ = worldTitle;
    hostName_ = hostName;
    hostSeed_ = seed;
    hostWorldType_ = worldType;
    hostUpdates_ = currentUpdates;
    peers_.clear();
    remotePlayers_.clear();
    nextClientId_ = 2;
    localPlayerId_ = 1;
    localIp_ = DetectLocalIPv4();
    lastBeaconTime_ = -10.0f;
    role_ = NetRole::Host;
    return true;
}

void LanNetwork::StopHosting() {
    for (auto& p : peers_) {
        CloseSock(p.sock);
    }
    peers_.clear();
    CloseSock(listenSock_);
    CloseSock(beaconSock_);
    if (role_ == NetRole::Host) {
        remotePlayers_.clear();
        role_ = NetRole::Offline;
    }
}

void LanNetwork::HostRecordWorldUpdate(const WorldUpdateEntry& entry) {
    if (role_ == NetRole::Host) {
        hostUpdates_.push_back(entry);
    }
}

void LanNetwork::HostBroadcastBeacon(float nowSec) {
    if (beaconSock_ == INVALID_SOCKET || (nowSec - lastBeaconTime_) < 0.9f) return;
    lastBeaconTime_ = nowSec;

    auto sanitize = [](std::string s) {
        for (char& c : s) if (c == '|' || c == '\n' || c == '\r') c = ' ';
        return s;
    };

    std::ostringstream oss;
    oss << "ILANDS_LAN_V1|" << hostPort_ << "|" << totalPlayerCount() << "|"
        << static_cast<int>(hostWorldType_) << "|" << sanitize(hostName_) << "|" << sanitize(hostWorldTitle_);
    std::string msg = oss.str();

    sockaddr_in bcast{};
    bcast.sin_family = AF_INET;
    bcast.sin_port = htons(LAN_DISCOVERY_UDP_PORT);
    bcast.sin_addr.s_addr = INADDR_BROADCAST;
    sendto(static_cast<SOCKET>(beaconSock_), msg.data(), static_cast<int>(msg.size()), 0,
           reinterpret_cast<sockaddr*>(&bcast), sizeof(bcast));

    sockaddr_in lo{};
    lo.sin_family = AF_INET;
    lo.sin_port = htons(LAN_DISCOVERY_UDP_PORT);
    inet_pton(AF_INET, "127.0.0.1", &lo.sin_addr);
    sendto(static_cast<SOCKET>(beaconSock_), msg.data(), static_cast<int>(msg.size()), 0,
           reinterpret_cast<sockaddr*>(&lo), sizeof(lo));
}

void LanNetwork::HostAcceptClients(const Vec3& hostPos, float hourOfDay) {
    (void)hostPos;
    (void)hourOfDay;
    if (listenSock_ == INVALID_SOCKET) return;
    while (true) {
        sockaddr_in ca{};
        int clen = sizeof(ca);
        SOCKET cs = accept(static_cast<SOCKET>(listenSock_), reinterpret_cast<sockaddr*>(&ca), &clen);
        if (cs == INVALID_SOCKET) break;

        BOOL nodelay = TRUE;
        setsockopt(cs, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));
        SetNonBlocking(cs);

        ClientPeer peer;
        peer.sock = static_cast<uintptr_t>(cs);
        peer.id = nextClientId_++;
        peer.name = "Player";
        peers_.push_back(std::move(peer));
    }
}

void LanNetwork::HostHandlePacket(ClientPeer& peer, uint8_t msgType, const uint8_t* payload, uint32_t len, const Vec3& hostPos, float hourOfDay) {
    ByteReader r{payload, len, 0};

    if (msgType == MSG_HELLO && !peer.handshakeDone) {
        peer.name = r.str();
        if (peer.name.empty()) peer.name = "Player " + std::to_string(peer.id);
        peer.handshakeDone = true;
        peer.pos = {128.5f, 45.0f, 128.5f};

        ByteWriter w;
        w.u32(peer.id);
        w.i32(hostSeed_);
        w.u8(static_cast<uint8_t>(hostWorldType_));
        w.f32(hourOfDay);
        w.str(hostWorldTitle_);
        w.u32(static_cast<uint32_t>(hostUpdates_.size()));
        for (const auto& u : hostUpdates_) {
            w.i16(static_cast<int16_t>(u.x));
            w.i16(static_cast<int16_t>(u.y));
            w.i16(static_cast<int16_t>(u.z));
            w.u8(static_cast<uint8_t>(u.type));
        }
        SendFrame(peer.sock, MSG_WELCOME, w.data);

        {
            ByteWriter sw;
            sw.u32(localPlayerId_);
            sw.str(hostName_);
            sw.f32(hostPos.x); sw.f32(hostPos.y); sw.f32(hostPos.z);
            sw.f32(0.0f); sw.f32(0.0f);
            sw.u8(0);
            SendFrame(peer.sock, MSG_PLAYER_SPAWN, sw.data);
        }

        for (const auto& other : peers_) {
            if (other.id == peer.id || !other.handshakeDone) continue;
            ByteWriter sw;
            sw.u32(other.id);
            sw.str(other.name);
            sw.f32(other.pos.x); sw.f32(other.pos.y); sw.f32(other.pos.z);
            sw.f32(other.yaw); sw.f32(other.pitch);
            sw.u8(0);
            SendFrame(peer.sock, MSG_PLAYER_SPAWN, sw.data);
        }

        RemotePlayer rp;
        rp.id = peer.id;
        rp.name = peer.name;
        rp.prevPos = rp.targetPos = rp.renderPos = peer.pos;
        remotePlayers_[peer.id] = rp;
        chatQueue_.push_back({"", peer.name + " has joined."});

        ByteWriter joinW;
        joinW.u32(peer.id);
        joinW.str(peer.name);
        joinW.f32(peer.pos.x); joinW.f32(peer.pos.y); joinW.f32(peer.pos.z);
        joinW.f32(0.0f); joinW.f32(0.0f);
        joinW.u8(1);
        BroadcastFrameToClients(MSG_PLAYER_SPAWN, joinW.data, peer.id);
        return;
    }

    if (!peer.handshakeDone) return;

    if (msgType == MSG_TRANSFORM) {
        (void)r.u32();
        float x = r.f32(), y = r.f32(), z = r.f32(), yaw = r.f32(), pitch = r.f32();
        peer.pos = {x, y, z};
        peer.yaw = yaw;
        peer.pitch = pitch;

        auto it = remotePlayers_.find(peer.id);
        if (it != remotePlayers_.end()) {
            it->second.prevPos = it->second.renderPos;
            it->second.targetPos = peer.pos;
            it->second.yaw = yaw;
            it->second.pitch = pitch;
            it->second.interpAlpha = 0.0f;
        }

        ByteWriter fwd;
        fwd.u32(peer.id);
        fwd.f32(x); fwd.f32(y); fwd.f32(z); fwd.f32(yaw); fwd.f32(pitch);
        BroadcastFrameToClients(MSG_TRANSFORM, fwd.data, peer.id);
    } else if (msgType == MSG_BLOCK_UPDATE) {
        int x = r.i16(), y = r.i16(), z = r.i16();
        BlockType bt = static_cast<BlockType>(r.u8());
        bool isDestroy = (r.u8() != 0);
        blockQueue_.push_back({x, y, z, bt, isDestroy, peer.id});

        ByteWriter fwd;
        fwd.i16(static_cast<int16_t>(x));
        fwd.i16(static_cast<int16_t>(y));
        fwd.i16(static_cast<int16_t>(z));
        fwd.u8(static_cast<uint8_t>(bt));
        fwd.u8(isDestroy ? 1 : 0);
        BroadcastFrameToClients(MSG_BLOCK_UPDATE, fwd.data, peer.id);
    } else if (msgType == MSG_CHAT) {
        std::string text = r.str();
        if (!text.empty()) {
            chatQueue_.push_back({peer.name, text});
            ByteWriter fwd;
            fwd.str(peer.name);
            fwd.str(text);
            BroadcastFrameToClients(MSG_CHAT, fwd.data, peer.id);
        }
    }
}

bool LanNetwork::ConnectToHost(const std::string& ip, uint16_t port, const std::string& playerName, std::string& outError) {
    if (!InitWinsock()) {
        outError = "Winsock error";
        return false;
    }
    Disconnect();
    StopHosting();

    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s == INVALID_SOCKET) {
        outError = "Cannot create socket";
        return false;
    }

    sockaddr_in dst{};
    dst.sin_family = AF_INET;
    dst.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &dst.sin_addr) != 1) {
        closesocket(s);
        outError = "Invalid IP address";
        return false;
    }

    SetNonBlocking(s);
    int res = connect(s, reinterpret_cast<sockaddr*>(&dst), sizeof(dst));
    if (res == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSAEWOULDBLOCK && err != WSAEINPROGRESS) {
            closesocket(s);
            outError = "Connection refused";
            return false;
        }
        fd_set wfds, efds;
        FD_ZERO(&wfds); FD_ZERO(&efds);
        FD_SET(s, &wfds); FD_SET(s, &efds);
        timeval tv{2, 500000};
        int sel = select(0, nullptr, &wfds, &efds, &tv);
        if (sel <= 0 || FD_ISSET(s, &efds)) {
            closesocket(s);
            outError = "Host unreachable (check Wi-Fi/Firewall)";
            return false;
        }
        int soErr = 0;
        int soLen = sizeof(soErr);
        getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soErr), &soLen);
        if (soErr != 0) {
            closesocket(s);
            outError = "Connection refused";
            return false;
        }
    }

    BOOL nodelay = TRUE;
    setsockopt(s, IPPROTO_TCP, TCP_NODELAY, reinterpret_cast<const char*>(&nodelay), sizeof(nodelay));

    clientSock_ = static_cast<uintptr_t>(s);
    clientName_ = playerName.empty() ? GetDefaultPlayerName() : playerName;
    clientRecvBuf_.clear();
    remotePlayers_.clear();
    welcome_ = NetWelcomeData{};
    welcomePending_ = false;
    disconnectedFlag_ = false;
    disconnectReason_.clear();
    role_ = NetRole::Client;

    ByteWriter hello;
    hello.str(clientName_);
    SendFrame(clientSock_, MSG_HELLO, hello.data);
    return true;
}

void LanNetwork::Disconnect() {
    CloseSock(clientSock_);
    clientRecvBuf_.clear();
    if (role_ == NetRole::Client) {
        remotePlayers_.clear();
        role_ = NetRole::Offline;
    }
}

void LanNetwork::ClientHandlePacket(uint8_t msgType, const uint8_t* payload, uint32_t len) {
    ByteReader r{payload, len, 0};

    if (msgType == MSG_WELCOME) {
        welcome_ = NetWelcomeData{};
        welcome_.received = true;
        welcome_.localPlayerId = r.u32();
        localPlayerId_ = welcome_.localPlayerId;
        welcome_.seed = r.i32();
        welcome_.worldType = (r.u8() == 1) ? WorldType::Flat : WorldType::Normal;
        welcome_.hourOfDay = r.f32();
        welcome_.worldTitle = r.str();
        uint32_t count = r.u32();
        welcome_.updates.reserve(std::min<uint32_t>(count, 200000));
        for (uint32_t i = 0; i < count && r.has(7); ++i) {
            int x = r.i16(), y = r.i16(), z = r.i16();
            BlockType bt = static_cast<BlockType>(r.u8());
            welcome_.updates.push_back({x, y, z, bt});
        }
        welcomePending_ = true;
    } else if (msgType == MSG_PLAYER_SPAWN) {
        uint32_t id = r.u32();
        std::string name = r.str();
        Vec3 pos{r.f32(), r.f32(), r.f32()};
        float yaw = r.f32(), pitch = r.f32();
        bool announce = (r.u8() != 0);
        if (id != localPlayerId_) {
            RemotePlayer rp;
            rp.id = id;
            rp.name = name;
            rp.prevPos = rp.targetPos = rp.renderPos = pos;
            rp.yaw = yaw;
            rp.pitch = pitch;
            remotePlayers_[id] = rp;
            if (announce) {
                chatQueue_.push_back({"", name + " has joined."});
            }
        }
    } else if (msgType == MSG_PLAYER_LEAVE) {
        uint32_t id = r.u32();
        auto it = remotePlayers_.find(id);
        if (it != remotePlayers_.end()) {
            chatQueue_.push_back({"", it->second.name + " has left."});
            remotePlayers_.erase(it);
        }
    } else if (msgType == MSG_TRANSFORM) {
        uint32_t id = r.u32();
        Vec3 pos{r.f32(), r.f32(), r.f32()};
        float yaw = r.f32(), pitch = r.f32();
        auto it = remotePlayers_.find(id);
        if (it != remotePlayers_.end()) {
            it->second.prevPos = it->second.renderPos;
            it->second.targetPos = pos;
            it->second.yaw = yaw;
            it->second.pitch = pitch;
            it->second.interpAlpha = 0.0f;
        }
    } else if (msgType == MSG_BLOCK_UPDATE) {
        int x = r.i16(), y = r.i16(), z = r.i16();
        BlockType bt = static_cast<BlockType>(r.u8());
        bool isDestroy = (r.u8() != 0);
        blockQueue_.push_back({x, y, z, bt, isDestroy, 0});
    } else if (msgType == MSG_CHAT) {
        std::string sender = r.str();
        std::string text = r.str();
        chatQueue_.push_back({sender, text});
    } else if (msgType == MSG_TIME_SYNC) {
        syncedHour_ = r.f32();
        hasTimeSync_ = true;
    }
}

bool LanNetwork::SendFrame(uintptr_t sock, uint8_t msgType, const std::vector<uint8_t>& payload) {
    if (sock == INVALID_SOCKET) return false;
    uint32_t frameLen = static_cast<uint32_t>(1 + payload.size());
    std::vector<uint8_t> out(4 + frameLen);
    std::memcpy(out.data(), &frameLen, 4);
    out[4] = msgType;
    if (!payload.empty()) std::memcpy(out.data() + 5, payload.data(), payload.size());

    size_t sent = 0;
    int retries = 0;
    while (sent < out.size()) {
        int n = send(static_cast<SOCKET>(sock), reinterpret_cast<const char*>(out.data() + sent),
                     static_cast<int>(out.size() - sent), 0);
        if (n > 0) {
            sent += static_cast<size_t>(n);
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK && retries++ < 50) {
                Sleep(1);
                continue;
            }
            return false;
        }
    }
    return true;
}

void LanNetwork::BroadcastFrameToClients(uint8_t msgType, const std::vector<uint8_t>& payload, uint32_t excludeId) {
    for (auto& p : peers_) {
        if (p.handshakeDone && p.id != excludeId) {
            SendFrame(p.sock, msgType, payload);
        }
    }
}

bool LanNetwork::DrainSocket(uintptr_t sock, std::vector<uint8_t>& buf, bool& outClosed) {
    outClosed = false;
    if (sock == INVALID_SOCKET) { outClosed = true; return false; }
    uint8_t tmp[4096];
    while (true) {
        int n = recv(static_cast<SOCKET>(sock), reinterpret_cast<char*>(tmp), sizeof(tmp), 0);
        if (n > 0) {
            buf.insert(buf.end(), tmp, tmp + n);
        } else if (n == 0) {
            outClosed = true;
            break;
        } else {
            int err = WSAGetLastError();
            if (err == WSAEWOULDBLOCK) break;
            outClosed = true;
            break;
        }
    }
    return !buf.empty();
}

void LanNetwork::Update(float dt, float nowSec, const Vec3& localPos, float localYaw, float localPitch, float hourOfDay) {
    for (auto& kv : remotePlayers_) {
        auto& rp = kv.second;
        rp.interpAlpha = std::min(1.0f, rp.interpAlpha + dt * 16.0f);
        rp.renderPos = rp.prevPos + (rp.targetPos - rp.prevPos) * rp.interpAlpha;
    }

    if (role_ == NetRole::Host) {
        HostBroadcastBeacon(nowSec);
        HostAcceptClients(localPos, hourOfDay);

        std::vector<uint32_t> droppedIds;
        for (auto& peer : peers_) {
            bool closed = false;
            DrainSocket(peer.sock, peer.recvBuf, closed);

            size_t offset = 0;
            while (peer.recvBuf.size() - offset >= 4) {
                uint32_t flen = 0;
                std::memcpy(&flen, peer.recvBuf.data() + offset, 4);
                if (flen == 0 || flen > 4 * 1024 * 1024) { closed = true; break; }
                if (peer.recvBuf.size() - offset < 4 + flen) break;
                uint8_t mtype = peer.recvBuf[offset + 4];
                HostHandlePacket(peer, mtype, peer.recvBuf.data() + offset + 5, flen - 1, localPos, hourOfDay);
                offset += 4 + flen;
            }
            if (offset > 0) peer.recvBuf.erase(peer.recvBuf.begin(), peer.recvBuf.begin() + offset);
            if (closed) {
                CloseSock(peer.sock);
                if (peer.handshakeDone) droppedIds.push_back(peer.id);
            }
        }

        peers_.erase(std::remove_if(peers_.begin(), peers_.end(),
                                    [](const ClientPeer& p) { return p.sock == INVALID_SOCKET; }),
                     peers_.end());
        for (uint32_t id : droppedIds) {
            auto it = remotePlayers_.find(id);
            if (it != remotePlayers_.end()) {
                chatQueue_.push_back({"", it->second.name + " has left."});
                remotePlayers_.erase(it);
            }
            ByteWriter lw;
            lw.u32(id);
            BroadcastFrameToClients(MSG_PLAYER_LEAVE, lw.data);
        }

        transformSendAccum_ += dt;
        if (transformSendAccum_ >= 0.05f && !peers_.empty()) {
            transformSendAccum_ = 0.0f;
            ByteWriter tw;
            tw.u32(localPlayerId_);
            tw.f32(localPos.x); tw.f32(localPos.y); tw.f32(localPos.z);
            tw.f32(localYaw); tw.f32(localPitch);
            BroadcastFrameToClients(MSG_TRANSFORM, tw.data);
        }

        lastTimeSync_ += dt;
        if (lastTimeSync_ >= 2.0f && !peers_.empty()) {
            lastTimeSync_ = 0.0f;
            ByteWriter ts;
            ts.f32(hourOfDay);
            BroadcastFrameToClients(MSG_TIME_SYNC, ts.data);
        }
    } else if (role_ == NetRole::Client) {
        bool closed = false;
        DrainSocket(clientSock_, clientRecvBuf_, closed);

        size_t offset = 0;
        while (clientRecvBuf_.size() - offset >= 4) {
            uint32_t flen = 0;
            std::memcpy(&flen, clientRecvBuf_.data() + offset, 4);
            if (flen == 0 || flen > 8 * 1024 * 1024) { closed = true; break; }
            if (clientRecvBuf_.size() - offset < 4 + flen) break;
            uint8_t mtype = clientRecvBuf_[offset + 4];
            ClientHandlePacket(mtype, clientRecvBuf_.data() + offset + 5, flen - 1);
            offset += 4 + flen;
        }
        if (offset > 0) clientRecvBuf_.erase(clientRecvBuf_.begin(), clientRecvBuf_.begin() + offset);

        if (closed) {
            Disconnect();
            disconnectedFlag_ = true;
            disconnectReason_ = "Connection to host was lost.";
            return;
        }

        transformSendAccum_ += dt;
        if (transformSendAccum_ >= 0.05f) {
            transformSendAccum_ = 0.0f;
            ByteWriter tw;
            tw.u32(localPlayerId_);
            tw.f32(localPos.x); tw.f32(localPos.y); tw.f32(localPos.z);
            tw.f32(localYaw); tw.f32(localPitch);
            SendFrame(clientSock_, MSG_TRANSFORM, tw.data);
        }
    }
}

void LanNetwork::SendBlockChange(int x, int y, int z, BlockType type, bool isDestroy) {
    if (role_ == NetRole::Offline) return;
    ByteWriter w;
    w.i16(static_cast<int16_t>(x));
    w.i16(static_cast<int16_t>(y));
    w.i16(static_cast<int16_t>(z));
    w.u8(static_cast<uint8_t>(type));
    w.u8(isDestroy ? 1 : 0);
    if (role_ == NetRole::Host) {
        BroadcastFrameToClients(MSG_BLOCK_UPDATE, w.data);
    } else {
        SendFrame(clientSock_, MSG_BLOCK_UPDATE, w.data);
    }
}

void LanNetwork::SendChatMessage(const std::string& text) {
    if (text.empty()) return;
    if (role_ == NetRole::Host) {
        ByteWriter w;
        w.str(hostName_);
        w.str(text);
        BroadcastFrameToClients(MSG_CHAT, w.data);
    } else if (role_ == NetRole::Client) {
        ByteWriter w;
        w.str(text);
        SendFrame(clientSock_, MSG_CHAT, w.data);
    }
}

bool LanNetwork::PopWelcome(NetWelcomeData& out) {
    if (!welcomePending_) return false;
    welcomePending_ = false;
    out = std::move(welcome_);
    return true;
}

std::vector<NetChatEvent> LanNetwork::TakeChatEvents() {
    auto out = std::move(chatQueue_);
    chatQueue_.clear();
    return out;
}

std::vector<NetBlockEvent> LanNetwork::TakeBlockEvents() {
    auto out = std::move(blockQueue_);
    blockQueue_.clear();
    return out;
}

bool LanNetwork::ConsumeTimeSync(float& outHour) {
    if (!hasTimeSync_) return false;
    hasTimeSync_ = false;
    outHour = syncedHour_;
    return true;
}

bool LanNetwork::ConsumeDisconnected(std::string& outReason) {
    if (!disconnectedFlag_) return false;
    disconnectedFlag_ = false;
    outReason = disconnectReason_;
    return true;
}

}
