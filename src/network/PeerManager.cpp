#include "network/PeerManager.hpp"
#include "network/Protocol.hpp"
#include "network/PacketSecurity.hpp"
#include "utils/ThreadUtils.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <ctime>

namespace FreeAI {
    namespace Network {

        PeerManager::PeerManager()
            : m_running(false), m_listenPort(9090), m_isSuperNode(false),
            m_identity(nullptr), m_enableSigning(true) {
        }

        PeerManager::~PeerManager() {
            Stop();
        }

        bool PeerManager::Initialize(const Utils::Config& config) {
            m_listenPort = config.GetInt("network", "bootstrap_port", 9090);
            m_enableSigning = config.GetBool("security", "enable_signing", true);

            // Load seed nodes from config
            std::string seeds = config.Get("network", "seed_nodes", "");
            std::stringstream ss(seeds);
            std::string seed;
            while (std::getline(ss, seed, ',')) {
                seed = Utils::Config::Trim(seed);
                if (!seed.empty()) {
                    m_seedNodes.push_back(seed);
                }
            }

            // Fallback hardcoded seeds if config is empty
            if (m_seedNodes.empty()) {
                m_seedNodes = {
                    "seed1.freeai.network:9090",
                    "seed2.freeai.network:9090"
                };
            }

            std::cout << "[PEER] Loaded " << m_seedNodes.size() << " seed nodes." << std::endl;
            return true;
        }

        void PeerManager::SetIdentity(Crypto::Identity* identity) {
            m_identity = identity;
            std::cout << "[PEER] Identity set. Node ID: " << identity->GetShortID() << std::endl;
        }

        void PeerManager::SetSigningEnabled(bool enableSigning) {
            m_enableSigning = enableSigning;
            std::cout << "[PEER] Packet signing " << (enableSigning ? "enabled" : "disabled") << std::endl;
        }

        void PeerManager::Start() {
            m_running = true;

            // Try to bind - determines if we are Super Node
            if (!m_socket.Create()) {
                std::cerr << "[PEER] Failed to create UDP socket." << std::endl;
                return;
            }

            if (m_socket.Bind(m_listenPort)) {
                m_isSuperNode = true;
                m_socket.SetNonBlocking(true);
                std::cout << "[PEER] Running as Super Node (Port " << m_listenPort << " open)." << std::endl;
            }
            else {
                m_isSuperNode = false;
                std::cout << "[PEER] Running as Leaf Node (NAT detected)." << std::endl;
            }

            // DHT initialization:
            m_dht.Initialize(m_identity);
            m_dht.Start(&m_socket);

            m_listenerThread = std::thread(&PeerManager::ListenLoop, this);
            m_punchThread = std::thread(&PeerManager::PunchLoop, this);

            // Connect to seeds in background
            std::thread(&PeerManager::ConnectToSeeds, this).detach();
        }

        void PeerManager::Stop() {
            m_running = false;
            if (m_listenerThread.joinable()) {
                m_listenerThread.join();
            }
            
            if (m_punchThread.joinable()) {
                m_punchThread.join();
            }

            m_socket.Close();
        }

        bool PeerManager::SendSecurePacket(UDPSocket& socket, const std::string& ip, int port,
            uint8_t type, const void* payload, size_t size)
        {
            // Determine if this packet type needs signing
            bool sign = false;
            bool encrypt = true; // Always encrypt for DPI resistance

            switch (type) {
            case PT_REGISTER:
            case PT_HANDSHAKE:
            case PT_PEER_LIST:
            case PT_INFERENCE_REQUEST:
            case PT_INFERENCE_RESPONSE:
                sign = m_enableSigning && (m_identity != nullptr) && m_identity->IsValid();
                break;
            default:
                // PT_PUNCH, PT_INTRO_* don't need signing (lightweight)
                sign = false;
                break;
            }

            // Prepare secure packet
            std::vector<uint8_t> packet = PacketSecurity::PrepareOutgoing(
                type, payload, size, sign, encrypt, m_identity);

            if (packet.empty()) {
                std::cerr << "[PEER] Failed to prepare secure packet." << std::endl;
                return false;
            }

            int sent = socket.SendTo(packet.data(), packet.size(), ip, port);
            if (sent < 0) {
                std::cerr << "[PEER] Failed to send packet to " << ip << ":" << port << std::endl;
                return false;
            }

            return true;
        }

        void PeerManager::ListenLoop() {
            // Set this thread to low priority
            FreeAI::Utils::SetThreadPriorityLow();

            char buffer[MAX_PACKET_SIZE];
            std::string sender_ip;
            int sender_port;

            while (m_running) {
                int bytes = m_socket.ReceiveFrom(buffer, sizeof(buffer), sender_ip, sender_port);
                if (bytes > 0) {
                    HandleBootstrapPacket(m_socket, buffer, bytes, sender_ip, sender_port);
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        void PeerManager::HandleBootstrapPacket(UDPSocket& sock, char* buffer, int bytes,
            const std::string& ip, int port) {

            // Get sender's public key (if known)
            std::string senderPubKey;
            auto peers = GetKnownPeers();
            for (const auto& peer : peers) {
                if (peer.ip == ip && peer.port == port) {
                    senderPubKey = peer.public_key_pem;
                    break;
                }
            }

            // Process incoming secure packet
            SecurePacketHeader header;
            std::vector<uint8_t> payload;

            if (!PacketSecurity::ProcessIncoming(
                reinterpret_cast<uint8_t*>(buffer), bytes, header, payload,
                m_identity, senderPubKey)) {
                std::cerr << "[PEER] Invalid packet from " << ip << ":" << port << std::endl;
                return;
            }

            // Handle Packet Types
            if (header.type == PT_REGISTER && m_isSuperNode) {
                // RegisterPayload: [peer_id:16][pubkey_size:2][reserved:2][pubkey:variable]
                if (payload.size() >= sizeof(RegisterPayload)) {
                    const RegisterPayload* reg = reinterpret_cast<const RegisterPayload*>(payload.data());

                    // Read peer_id from struct (first 16 bytes)
                    std::string peer_id = std::string(reg->peer_id);

                    // Read pubkey after the struct
                    const uint8_t* pubkey_data = payload.data() + sizeof(RegisterPayload);
                    std::string pubkey_pem = std::string(
                        reinterpret_cast<const char*>(pubkey_data),
                        reg->pubkey_size);

                    std::cout << "[PEER] Registered: " << peer_id << " @ " << ip << ":" << port << std::endl;
                    std::cout << "[PEER] Public Key Size: " << reg->pubkey_size << " bytes" << std::endl;

                    // Store public key under the SHORT ID (not the PEM!)
                    StorePeerPublicKey(peer_id, pubkey_pem);
                    AddPeer({ ip, port, peer_id, pubkey_pem, std::time(nullptr), true, false });

                    std::cout << "[DEBUG] Payload size: " << payload.size() << std::endl;
                    std::cout << "[DEBUG] RegisterPayload size: " << sizeof(RegisterPayload) << std::endl;
                    std::cout << "[DEBUG] Peer ID (raw): " << std::string(reg->peer_id, 16) << std::endl;
                    std::cout << "[DEBUG] Peer ID (trimmed): " << peer_id << std::endl;
                    std::cout << "[DEBUG] PubKey size: " << reg->pubkey_size << std::endl;
                }
            }
            else if (header.type == PT_INTRO_REQUEST && m_isSuperNode) {
                std::string target_id = std::string(
                    reinterpret_cast<const char*>(payload.data()),
                    payload.size());

                for (const auto& peer : peers) {
                    if (peer.peer_id == target_id) {
                        IntroResponsePayload response;
                        std::memset(&response, 0, sizeof(response));
                        strncpy(response.target_ip, peer.ip.c_str(), sizeof(response.target_ip) - 1);
                        response.target_port = static_cast<uint16_t>(peer.port);
                        strncpy(response.target_id, peer.peer_id.c_str(), sizeof(response.target_id) - 1);
                        response.target_pubkey_size = static_cast<uint16_t>(peer.public_key_pem.size());

                        std::vector<uint8_t> responsePacket;
                        responsePacket.resize(sizeof(IntroResponsePayload) + peer.public_key_pem.size());
                        std::memcpy(responsePacket.data(), &response, sizeof(IntroResponsePayload));
                        std::memcpy(responsePacket.data() + sizeof(IntroResponsePayload),
                            peer.public_key_pem.c_str(), peer.public_key_pem.size());

                        SendSecurePacket(sock, ip, port, PT_INTRO_RESPONSE,
                            responsePacket.data(), responsePacket.size());

                        std::cout << "[PEER] Introduced " << ip << " to " << peer.ip << std::endl;
                        break;
                    }
                }
            }
            else if (header.type == PT_INTRO_RESPONSE) {
                if (payload.size() >= sizeof(IntroResponsePayload)) {
                    const IntroResponsePayload* intro = reinterpret_cast<const IntroResponsePayload*>(payload.data());

                    std::string target_ip = std::string(intro->target_ip);
                    int target_port = intro->target_port;
                    std::string target_id = std::string(intro->target_id);
                    std::string target_pubkey = std::string(
                        reinterpret_cast<const char*>(payload.data() + sizeof(IntroResponsePayload)),
                        intro->target_pubkey_size);

                    std::cout << "[PEER] Received intro: " << target_id << " @ " << target_ip << ":" << target_port << std::endl;

                    StorePeerPublicKey(target_id, target_pubkey);
                    m_punchManager.StartPunch(target_ip, target_port, target_id);
                    AddPeer({ target_ip, target_port, target_id, target_pubkey, std::time(nullptr), true, false });
                }
            }
            else if (header.type == PT_PEER_LIST) {
                auto newPeers = ParsePeerList(
                    reinterpret_cast<const char*>(payload.data()),
                    payload.size());
                for (const auto& p : newPeers) {
                    AddPeer(p);
                }
                std::cout << "[PEER] Received " << newPeers.size() << " peers from seed." << std::endl;
            }
            else if (header.type == PT_PUNCH) {
                if (payload.size() >= sizeof(PunchPayload)) {
                    const PunchPayload* punch = reinterpret_cast<const PunchPayload*>(payload.data());
                    HandlePunchPacket(ip, port, punch);
                }
            }
            else if (header.type == PT_PUNCH_ACK) {
                std::cout << "[PUNCH] Received punch ACK from " << ip << ":" << port << std::endl;
                if (payload.size() >= sizeof(PunchPayload)) {
                    const PunchPayload* punch = reinterpret_cast<const PunchPayload*>(payload.data());
                    m_punchManager.MarkSuccess(punch->sender_id);  // ✅ Fixed: use actual sender_id
                }
            }
            else if (header.type >= PT_DHT_FIND_NODE && header.type <= PT_DHT_PING) {
                ProcessDHTPacket(sock, ip, port, header.type, payload);
            }
        }

        // Process DHT packets
        void PeerManager::ProcessDHTPacket(UDPSocket& sock, const std::string& ip, int port,
            uint8_t type, const std::vector<uint8_t>& payload) {
            if (payload.size() < sizeof(DHTFindNodePayload)) {
                return;
            }

            if (type == PT_DHT_FIND_NODE) {
                const DHTFindNodePayload* findReq =
                    reinterpret_cast<const DHTFindNodePayload*>(payload.data());
                HandleDHTFindNode(sock, ip, port, findReq);
            }
            else if (type == PT_DHT_FIND_NODE_RESPONSE) {
                const DHTFindNodeResponsePayload* findResp =
                    reinterpret_cast<const DHTFindNodeResponsePayload*>(payload.data());
                HandleDHTFindNodeResponse(ip, port, findResp);
            }
            else if (type == PT_DHT_PING) {
                // Respond with pong (same packet type for simplicity)
                m_dht.SendDHTPacket(&m_socket, ip, port, PT_DHT_PING, nullptr, 0);
                m_dht.ProcessIncoming(payload.data(), payload.size(), ip, port);
            }
        }

        void PeerManager::HandleDHTFindNode(UDPSocket& sock, const std::string& ip, int port,
            const DHTFindNodePayload* payload) {
            std::cout << "[DHT] FIND_NODE request from " << ip << ":" << port << std::endl;

            // Find closest nodes to target
            auto closestNodes = m_dht.FindNodes(
                reinterpret_cast<const uint8_t*>(payload->target_id));

            // Build response
            DHTFindNodeResponsePayload response;
            response.node_count = static_cast<uint8_t>(std::min(closestNodes.size(), size_t(20)));
            response.reserved[0] = 0;
            response.reserved[1] = 0;
            response.reserved[2] = 0;
            response.reserved[3] = 0;

            std::vector<uint8_t> responsePacket;
            responsePacket.resize(sizeof(DHTFindNodeResponsePayload) +
                response.node_count * sizeof(DHTNodeInfo));

            memcpy(responsePacket.data(), &response, sizeof(DHTFindNodeResponsePayload));

            for (size_t i = 0; i < closestNodes.size() && i < 20; ++i) {
                DHTNodeInfo nodeInfo;
                memcpy(nodeInfo.node_id, closestNodes[i].node_id, DHT_NODE_ID_SIZE);
                strncpy(nodeInfo.ip, closestNodes[i].ip.c_str(), sizeof(nodeInfo.ip) - 1);
                nodeInfo.port = static_cast<uint16_t>(closestNodes[i].port);
                nodeInfo.last_seen = closestNodes[i].last_seen;

                memcpy(responsePacket.data() + sizeof(DHTFindNodeResponsePayload) +
                    i * sizeof(DHTNodeInfo), &nodeInfo, sizeof(DHTNodeInfo));
            }

            m_dht.SendDHTPacket(&m_socket, ip, port, PT_DHT_FIND_NODE_RESPONSE,
                responsePacket.data(), responsePacket.size());
        }

        void PeerManager::HandleDHTFindNodeResponse(const std::string& ip, int port,
            const DHTFindNodeResponsePayload* payload) {
            std::cout << "[DHT] FIND_NODE_RESPONSE from " << ip << ":" << port
                << " (" << (int)payload->node_count << " nodes)" << std::endl;

            const uint8_t* ptr = reinterpret_cast<const uint8_t*>(payload) +
                sizeof(DHTFindNodeResponsePayload);

            for (int i = 0; i < payload->node_count; ++i) {
                const DHTNodeInfo* nodeInfo = reinterpret_cast<const DHTNodeInfo*>(
                    ptr + i * sizeof(DHTNodeInfo));

                // Add to routing table
                m_dht.AddNode(
                    reinterpret_cast<const uint8_t*>(nodeInfo->node_id),
                    nodeInfo->ip, nodeInfo->port);

                std::cout << "[DHT] Learned about node: ";
                for (int j = 0; j < 8; ++j) {
                    printf("%02x", nodeInfo->node_id[j]);
                }
                std::cout << " @ " << nodeInfo->ip << ":" << nodeInfo->port << std::endl;
            }
        }

        void PeerManager::ConnectToSeeds() {
            std::this_thread::sleep_for(std::chrono::seconds(2));

            for (const auto& seed : m_seedNodes) {
                size_t pos = seed.find(':');
                if (pos == std::string::npos) continue;

                std::string ip = seed.substr(0, pos);
                int port = std::stoi(seed.substr(pos + 1));

                std::cout << "[PEER] Connecting to seed: " << ip << ":" << port << std::endl;

                // CORRECT: Use short ID (8 chars), not PEM
                std::string peer_id = m_identity ? m_identity->GetShortID() : "unknown";
                std::string pubkey_pem = m_identity ? m_identity->GetPublicKeyPEM() : "";

                RegisterPayload regPayload;
                std::memset(&regPayload, 0, sizeof(regPayload));

                // Ensure null-termination
                strncpy(regPayload.peer_id, peer_id.c_str(), sizeof(regPayload.peer_id) - 1);
                regPayload.peer_id[sizeof(regPayload.peer_id) - 1] = '\0';

                regPayload.pubkey_size = static_cast<uint16_t>(pubkey_pem.size());

                // Build packet: [RegisterPayload][pubkey_pem]
                std::vector<uint8_t> regPacket;
                regPacket.resize(sizeof(RegisterPayload) + pubkey_pem.size());
                std::memcpy(regPacket.data(), &regPayload, sizeof(RegisterPayload));
                std::memcpy(regPacket.data() + sizeof(RegisterPayload),
                    pubkey_pem.c_str(), pubkey_pem.size());

                SendSecurePacket(m_socket, ip, port, PT_REGISTER,
                    regPacket.data(), regPacket.size());

                SendSecurePacket(m_socket, ip, port, PT_PEER_LIST, nullptr, 0);
            }
        }

        void PeerManager::RequestPeerList(const std::string& ip, int port) {
            SendSecurePacket(m_socket, ip, port, PT_PEER_LIST, nullptr, 0);
        }

        std::vector<PeerInfo> PeerManager::ParsePeerList(const char* data, size_t size) {
            std::vector<PeerInfo> peers;
            if (size == 0) return peers;

            std::string str(data, size);
            std::stringstream ss(str);
            std::string line;

            while (std::getline(ss, line, ';')) {
                size_t pos1 = line.find(':');
                size_t pos2 = line.find(':', pos1 + 1);
                if (pos1 != std::string::npos && pos2 != std::string::npos) {
                    PeerInfo p;
                    p.ip = line.substr(0, pos1);
                    p.port = std::stoi(line.substr(pos1 + 1, pos2 - pos1 - 1));
                    p.peer_id = line.substr(pos2 + 1);
                    p.last_seen = std::time(nullptr);
                    p.is_super_node = true;
                    peers.push_back(p);
                }
            }
            return peers;
        }

        std::string PeerManager::BuildPeerList() {
            std::stringstream ss;
            auto peers = GetKnownPeers();
            for (size_t i = 0; i < peers.size() && i < 10; ++i) { // Limit to 10 peers
                if (i > 0) ss << ";";
                ss << peers[i].ip << ":" << peers[i].port << ":" << peers[i].peer_id;
            }
            return ss.str();
        }

        void PeerManager::AddPeer(const PeerInfo& peer) {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            // Check if already exists
            for (const auto& p : m_peers) {
                if (p.ip == peer.ip && p.port == peer.port) {
                    return;
                }
            }
            m_peers.push_back(peer);
        }

        std::vector<PeerInfo> PeerManager::GetKnownPeers() const {
            std::lock_guard<std::mutex> lock(m_peerMutex);
            return m_peers;
        }

        bool PeerManager::IsSuperNode() const {
            return m_isSuperNode;
        }


        // PunchLoop - Background thread for punch attempts
        void PeerManager::PunchLoop() {
            FreeAI::Utils::SetThreadPriorityLow();

            while (m_running) {
                m_punchManager.Cleanup();

                auto activeSessions = m_punchManager.GetActiveSessions();
                for (const auto& session : activeSessions) {
                    if (m_punchManager.ShouldSendPunch(session.target_id)) {
                        SendPunchPacket(session.target_ip, session.target_port, session.target_id);
                        m_punchManager.RecordAttempt(session.target_id);
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(PUNCH_INTERVAL_MS));
            }
        }

        // RequestIntroduction - Ask Super Node to introduce us to a peer
        void PeerManager::RequestIntroduction(const std::string& ip, int port, const std::string& target_peer_id) {
            std::cout << "[PEER] Requesting introduction to " << target_peer_id << std::endl;
            SendSecurePacket(m_socket, ip, port, PT_INTRO_REQUEST,
                target_peer_id.c_str(), target_peer_id.size());
        }

        // SendPunchPacket - Send a hole punch attempt
        void PeerManager::SendPunchPacket(const std::string& ip, int port, const std::string& peer_id) {
            PunchPayload payload;
            std::memset(&payload, 0, sizeof(payload));

            std::string short_id = m_identity ? m_identity->GetShortID() : "unknown";
            std::strncpy(payload.sender_id, short_id.c_str(), sizeof(payload.sender_id) - 1);
            payload.timestamp = std::time(nullptr);
            payload.attempt_num = 1; // Will be updated by PunchManager

            std::cout << "[PUNCH] Sending punch to " << ip << ":" << port << std::endl;
            SendSecurePacket(m_socket, ip, port, PT_PUNCH, &payload, sizeof(payload));
        }

        // HandlePunchPacket - Process incoming punch
        void PeerManager::HandlePunchPacket(const std::string& ip, int port, const PunchPayload* payload) {
            std::cout << "[PUNCH] Received punch from " << payload->sender_id
                << " @ " << ip << ":" << port << std::endl;

            // Send acknowledgment
            SendSecurePacket(m_socket, ip, port, PT_PUNCH_ACK, payload, sizeof(PunchPayload));

            // Mark as direct connection available
            auto peers = GetKnownPeers();
            for (const auto& peer : peers) {
                if (peer.peer_id == payload->sender_id) {
                    // Update peer to mark as direct
                    // (Would need a UpdatePeer method)
                    break;
                }
            }

            m_punchManager.MarkSuccess(payload->sender_id);
        }

        // Store peer public key
        void PeerManager::StorePeerPublicKey(const std::string& peer_id, const std::string& pem) {
            std::lock_guard<std::mutex> lock(m_keyMutex);
            m_peerPublicKeys[peer_id] = pem;
            std::cout << "[KEYS] Stored public key for peer: " << peer_id << std::endl;
        }

        // Get peer public key
        std::string PeerManager::GetPeerPublicKey(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_keyMutex);
            auto it = m_peerPublicKeys.find(peer_id);
            if (it != m_peerPublicKeys.end()) {
                return it->second;
            }
            return "";
        }

    }
}