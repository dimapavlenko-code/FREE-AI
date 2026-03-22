#include "network/PeerManager.hpp"
#include "network/Protocol.hpp"
#include "network/PacketSecurity.hpp"
#include "utils/ThreadUtils.hpp"
#include <mbedtls/sha1.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <ctime>
#include <algorithm>

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

            // CRITICAL: Ensure identity is loaded BEFORE starting threads
            if (!m_identity || !m_identity->IsValid()) {
                std::cerr << "[PEER] FATAL: Identity not loaded before Start()!" << std::endl;
                return;
            }

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

            // DHT initialization
            m_dht.Initialize(m_identity);
            m_dht.Start(&m_socket);

            // Start all background threads
            m_listenerThread = std::thread(&PeerManager::ListenLoop, this);
            m_punchThread = std::thread(&PeerManager::PunchLoop, this);
            m_seedThread = std::thread(&PeerManager::SeedRegistrationLoop, this);

            // Start registration (non-blocking)
            std::thread(&PeerManager::ConnectToSeeds, this).detach();
        }

        void PeerManager::Stop() {
            m_running = false;

            if (m_listenerThread.joinable()) m_listenerThread.join();
            if (m_punchThread.joinable()) m_punchThread.join();
            if (m_seedThread.joinable()) m_seedThread.join();

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
            case PT_REGISTER_ACK:
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
            FreeAI::Utils::SetThreadPriorityLow();

            std::vector<char> buffer(MAX_PACKET_SIZE);
            auto pbuf = buffer.data();
            std::string sender_ip;
            int sender_port;

            while (m_running) {
                int bytes = m_socket.ReceiveFrom(pbuf, MAX_PACKET_SIZE, sender_ip, sender_port);
                if (bytes > 0) {
                    HandleBootstrapPacket(m_socket, pbuf, bytes, sender_ip, sender_port);
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
                if (payload.size() >= sizeof(RegisterPayload)) {
                    const RegisterPayload* reg = reinterpret_cast<const RegisterPayload*>(payload.data());

                    std::string peer_id = std::string(reg->peer_id);
                    const uint8_t* pubkey_data = payload.data() + sizeof(RegisterPayload);
                    std::string pubkey_pem = std::string(
                        reinterpret_cast<const char*>(pubkey_data),
                        reg->pubkey_size);

                    // NEW: Check if already registered (prevent duplicates)
                    {
                        std::lock_guard<std::mutex> lock(m_networkMutex);
                        if (m_peerPublicKeys.count(peer_id) > 0) {
                            std::cout << "[PEER] Peer already registered: " << peer_id << std::endl;
                            return;  // Skip duplicate processing
                        }
                    }

                    std::cout << "[PEER] Registered: " << peer_id << " @ " << ip << ":" << port << std::endl;

                    // Store public key
                    StorePeerPublicKey(peer_id, pubkey_pem);
                    AddPeer({ ip, port, peer_id, pubkey_pem, std::time(nullptr), true, false });

                    // Add to DHT routing table (SHA1 of pubkey = DHT node ID)
                    uint8_t dht_node_id[20];
                    mbedtls_sha1_context ctx;
                    mbedtls_sha1_init(&ctx);
                    mbedtls_sha1_starts(&ctx);
                    mbedtls_sha1_update(&ctx,
                        reinterpret_cast<const unsigned char*>(pubkey_pem.c_str()),
                        pubkey_pem.size());
                    mbedtls_sha1_finish(&ctx, dht_node_id);
                    mbedtls_sha1_free(&ctx);

                    m_dht.AddNode(dht_node_id, ip, port);
                    std::cout << "[DHT] Added peer to routing table: " << peer_id << std::endl;

                    // Send peer list to newly registered node
                    std::string peerList = BuildPeerList();
                    if (!peerList.empty()) {
                        SendSecurePacket(sock, ip, port, PT_PEER_LIST,
                            peerList.c_str(), peerList.size());
                    }

                    // Send REGISTER_ACK (NEW)
                    RegisterAckPayload ack;
                    std::memset(&ack, 0, sizeof(ack));
                    strncpy(ack.peer_id, m_identity->GetShortID().c_str(), sizeof(ack.peer_id) - 1);
                    ack.status = 0;  // Accepted

                    SendSecurePacket(sock, ip, port, PT_REGISTER_ACK, &ack, sizeof(ack));

                    // Mark this seed as registered (they know about us now)
                    {
                        std::lock_guard<std::mutex> lock(m_networkMutex);
                        for (auto& sreg : m_seedRegistrations) {
                            if (sreg.ip == ip && sreg.port == port) {
                                sreg.state = PeerConnectionState::Connected;
                                sreg.last_success_ts = static_cast<uint32_t>(std::time(nullptr));
                                break;
                            }
                        }
                    }
                }
            }
            else if (header.type == PT_REGISTER_ACK) {
                // NEW: Handle registration acknowledgment
                if (payload.size() >= sizeof(RegisterAckPayload)) {
                    const RegisterAckPayload* ack = reinterpret_cast<const RegisterAckPayload*>(payload.data());

                    {
                        std::lock_guard<std::mutex> lock(m_networkMutex);
                        for (auto& reg : m_seedRegistrations) {
                            if (reg.ip == ip && reg.port == port) {
                                if (ack->status == 0) {
                                    reg.state = PeerConnectionState::Connected;
                                    reg.last_success_ts = static_cast<uint32_t>(std::time(nullptr));
                                    std::cout << "[PEER] Registration ACK received from " << ip << ":" << port << std::endl;
                                }
                                else {
                                    reg.state = PeerConnectionState::Failed;
                                    std::cerr << "[PEER] Registration rejected by " << ip << ":" << port << std::endl;
                                }
                                break;
                            }
                        }
                    }
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
                    // Skip self
                    if (p.ip == "127.0.0.1" && p.port == m_listenPort) {
                        continue;
                    }

                    AddPeer(p);

                    // Also add to DHT routing table
                    if (!p.public_key_pem.empty()) {
                        uint8_t dht_node_id[20];
                        mbedtls_sha1_context ctx;
                        mbedtls_sha1_init(&ctx);
                        mbedtls_sha1_starts(&ctx);
                        mbedtls_sha1_update(&ctx,
                            reinterpret_cast<const unsigned char*>(p.public_key_pem.c_str()),
                            p.public_key_pem.size());
                        mbedtls_sha1_finish(&ctx, dht_node_id);
                        mbedtls_sha1_free(&ctx);

                        m_dht.AddNode(dht_node_id, p.ip, p.port);
                    }
                }
                std::cout << "[PEER] Received " << newPeers.size() << " peers from seed." << std::endl;
                std::cout << "[DHT] Routing table has " << m_dht.GetNodeCount() << " nodes" << std::endl;
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
                    m_punchManager.MarkSuccess(punch->sender_id);
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
                m_dht.SendDHTPacket(&m_socket, ip, port, PT_DHT_PING, nullptr, 0);
                m_dht.ProcessIncoming(payload.data(), payload.size(), ip, port);
            }
        }

        void PeerManager::HandleDHTFindNode(UDPSocket& sock, const std::string& ip, int port,
            const DHTFindNodePayload* payload) {
            std::cout << "[DHT] FIND_NODE request from " << ip << ":" << port << std::endl;

            auto closestNodes = m_dht.FindNodes(
                reinterpret_cast<const uint8_t*>(payload->target_id));

            DHTFindNodeResponsePayload response;
            memset(&response, 0, sizeof(response));
            response.node_count = static_cast<uint8_t>(std::min(closestNodes.size(), size_t(20)));

            std::vector<uint8_t> responsePacket;
            responsePacket.resize(sizeof(DHTFindNodeResponsePayload) +
                response.node_count * sizeof(DHTNodeInfo));

            memcpy(responsePacket.data(), &response, sizeof(DHTFindNodeResponsePayload));

            for (size_t i = 0; i < closestNodes.size() && i < 20; ++i) {
                DHTNodeInfo nodeInfo;
                memset(&nodeInfo, 0, sizeof(nodeInfo));

                memcpy(nodeInfo.node_id, closestNodes[i].node_id, DHT_NODE_ID_SIZE);
                strncpy(nodeInfo.ip, closestNodes[i].ip.c_str(), sizeof(nodeInfo.ip) - 1);
                nodeInfo.ip[sizeof(nodeInfo.ip) - 1] = '\0';
                nodeInfo.port = static_cast<uint16_t>(closestNodes[i].port);
                nodeInfo.last_seen = closestNodes[i].last_seen;

                memcpy(responsePacket.data() + sizeof(DHTFindNodeResponsePayload) +
                    i * sizeof(DHTNodeInfo), &nodeInfo, sizeof(DHTNodeInfo));
            }

            std::cout << "[DEBUG] Sending DHT response: " << responsePacket.size() << " bytes, "
                << (int)response.node_count << " nodes" << std::endl;

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

                m_dht.AddNode(
                    reinterpret_cast<const uint8_t*>(nodeInfo->node_id),
                    nodeInfo->ip, nodeInfo->port);

                std::cout << "[DHT] Learned about node: ";
                for (int j = 0; j < 8; ++j) {
                    printf("%02x", static_cast<unsigned int>(nodeInfo->node_id[j]));
                }
                std::cout << " @ " << nodeInfo->ip << ":" << nodeInfo->port << std::endl;
            }
        }

        void PeerManager::ConnectToSeeds() {
            // Wait for system to stabilize (no hardcoded timing dependencies, just safety)
            std::this_thread::sleep_for(std::chrono::seconds(2));

            // Initialize registration state for all seeds
            {
                std::lock_guard<std::mutex> lock(m_networkMutex);
                for (const auto& seed : m_seedNodes) {
                    size_t pos = seed.find(':');
                    if (pos == std::string::npos) continue;

                    SeedRegistration reg;
                    reg.seed_address = seed;
                    reg.ip = seed.substr(0, pos);
                    reg.port = std::stoi(seed.substr(pos + 1));
                    reg.first_attempt_ts = 0;
                    reg.last_attempt_ts = 0;
                    reg.last_success_ts = 0;
                    reg.state = PeerConnectionState::Disconnected;
                    reg.retry_count = 0;
                    reg.next_retry_delay_sec = 2;  // Start with 2 seconds

                    m_seedRegistrations.push_back(reg);
                }
            }

            // Send initial REGISTER to all seeds (no blocking wait)
            SendInitialRegistrations();
        }

        void PeerManager::SendInitialRegistrations() {
            std::lock_guard<std::mutex> lock(m_networkMutex);

            for (auto& reg : m_seedRegistrations) {
                if (reg.state == PeerConnectionState::Disconnected) {
                    SendRegistration(reg);
                    reg.state = PeerConnectionState::Connecting;
                    reg.first_attempt_ts = static_cast<uint32_t>(std::time(nullptr));
                    reg.last_attempt_ts = reg.first_attempt_ts;
                }
            }
        }

        void PeerManager::SendRegistration(SeedRegistration& reg) {
            // NOTE: Called while m_networkMutex is already held!
            // Do NOT acquire any locks here

            // CRITICAL: Verify identity is ready before sending
            if (!m_identity || !m_identity->IsValid()) {
                std::cerr << "[PEER] Cannot register: identity not valid!" << std::endl;
                return;
            }

            std::string peer_id = m_identity->GetShortID();
            std::string pubkey_pem = m_identity->GetPublicKeyPEM();

            RegisterPayload regPayload;
            std::memset(&regPayload, 0, sizeof(regPayload));

            strncpy(regPayload.peer_id, peer_id.c_str(), sizeof(regPayload.peer_id) - 1);
            regPayload.peer_id[sizeof(regPayload.peer_id) - 1] = '\0';
            regPayload.pubkey_size = static_cast<uint16_t>(pubkey_pem.size());

            std::vector<uint8_t> regPacket;
            regPacket.resize(sizeof(RegisterPayload) + pubkey_pem.size());
            std::memcpy(regPacket.data(), &regPayload, sizeof(RegisterPayload));
            std::memcpy(regPacket.data() + sizeof(RegisterPayload),
                pubkey_pem.c_str(), pubkey_pem.size());

            SendSecurePacket(m_socket, reg.ip, reg.port, PT_REGISTER,
                regPacket.data(), regPacket.size());

            reg.last_attempt_ts = static_cast<uint32_t>(std::time(nullptr));
            reg.retry_count++;

            std::cout << "[PEER] Sent REGISTER to " << reg.ip << ":" << reg.port
                << " (attempt " << reg.retry_count << ", delay " << reg.next_retry_delay_sec << "s)" << std::endl;
        }

        void PeerManager::SeedRegistrationLoop() {
            FreeAI::Utils::SetThreadPriorityLow();

            while (m_running) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));

                uint32_t curTs = static_cast<uint32_t>(std::time(nullptr));
                bool allVerified = true;

                std::lock_guard<std::mutex> lock(m_networkMutex);

                for (auto& reg : m_seedRegistrations) {
                    // Skip if already verified
                    if (reg.state == PeerConnectionState::Verified) {
                        continue;
                    }

                    // Check if identity is ready before attempting
                    if (!m_identity || !m_identity->IsValid()) {
                        allVerified = false;
                        continue;
                    }

                    // State machine handling
                    switch (reg.state) {
                    case PeerConnectionState::Disconnected:
                        // First attempt - send REGISTER
                        SendRegistration(reg);
                        reg.state = PeerConnectionState::Connecting;
                        reg.retry_count = 1;
                        reg.next_retry_delay_sec = 2;  // Start with 2 seconds
                        allVerified = false;
                        break;

                    case PeerConnectionState::Connecting:
                        // Check for timeout (no ACK received)
                        if (curTs > reg.last_attempt_ts + static_cast<uint32_t>(reg.next_retry_delay_sec)) {
                            // Exponential backoff with max 60 seconds
                            reg.next_retry_delay_sec = std::min(reg.next_retry_delay_sec * 2, 60);
                            reg.retry_count++;

                            if (reg.retry_count <= 10) {  // More retries for resilience
                                SendRegistration(reg);
                                std::cout << "[PEER] Retrying registration with " << reg.ip
                                    << ":" << reg.port << " (attempt " << reg.retry_count
                                    << ", delay " << reg.next_retry_delay_sec << "s)" << std::endl;
                            }
                            else {
                                reg.state = PeerConnectionState::Failed;
                                std::cerr << "[PEER] Registration failed with " << reg.ip
                                    << ":" << reg.port << " after " << reg.retry_count << " attempts" << std::endl;
                            }
                        }
                        allVerified = false;
                        break;

                    case PeerConnectionState::Connected:
                        // REGISTER_ACK received, verify signature by checking public key
                        if (m_peerPublicKeys.count(reg.seed_address) > 0 ||
                            m_peerPublicKeys.count(reg.ip + ":" + std::to_string(reg.port)) > 0) {
                            reg.state = PeerConnectionState::Verified;
                            reg.last_success_ts = curTs;
                            std::cout << "[PEER] Connection verified with " << reg.ip << ":" << reg.port << std::endl;
                        }
                        else {
                            allVerified = false;
                        }
                        break;

                    case PeerConnectionState::Failed:
                        // Periodic recovery attempt (every 5 minutes)
                        if (curTs > reg.last_attempt_ts + 300) {
                            reg.state = PeerConnectionState::Disconnected;
                            reg.retry_count = 0;
                            reg.next_retry_delay_sec = 2;
                            std::cout << "[PEER] Recovering failed connection to " << reg.ip << ":" << reg.port << std::endl;
                        }
                        allVerified = false;
                        break;

                    case PeerConnectionState::Verified:
                        // Already verified
                        break;
                    }
                }

                // Trigger DHT discovery when ALL seeds are verified
                static bool dhtDiscoverySent = false;
                if (!dhtDiscoverySent && allVerified && !m_seedRegistrations.empty()) {
                    dhtDiscoverySent = true;

                    // Inline DHT discovery - no second lock!
                    uint8_t random_target[20];
                    memset(random_target, 0, 20);

                    for (const auto& reg : m_seedRegistrations) {
                        DHTFindNodePayload findPayload;
                        memset(&findPayload, 0, sizeof(findPayload));
                        memcpy(findPayload.target_id, random_target, 20);

                        m_dht.SendDHTPacket(&m_socket, reg.ip, reg.port, PT_DHT_FIND_NODE,
                            &findPayload, sizeof(findPayload));

                        std::cout << "[DHT] Sent FIND_NODE to " << reg.ip << ":" << reg.port << std::endl;
                    }

                    std::cout << "[DHT] Final routing table has " << m_dht.GetNodeCount() << " nodes" << std::endl;
                }
            }
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
                size_t pos3 = line.find(':', pos2 + 1);

                if (pos1 != std::string::npos && pos2 != std::string::npos) {
                    PeerInfo p;
                    p.ip = line.substr(0, pos1);
                    p.port = std::stoi(line.substr(pos1 + 1, pos2 - pos1 - 1));

                    if (pos3 != std::string::npos) {
                        p.peer_id = line.substr(pos2 + 1, pos3 - pos2 - 1);
                        p.public_key_pem = line.substr(pos3 + 1);
                    }
                    else {
                        p.peer_id = line.substr(pos2 + 1);
                        p.public_key_pem = "";
                    }

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
            for (size_t i = 0; i < peers.size() && i < 10; ++i) {
                if (i > 0) ss << ";";
                ss << peers[i].ip << ":" << peers[i].port << ":"
                    << peers[i].peer_id << ":" << peers[i].public_key_pem;
            }
            return ss.str();
        }

        void PeerManager::AddPeer(const PeerInfo& peer) {
            std::lock_guard<std::mutex> lock(m_peerMutex);
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

        void PeerManager::RequestIntroduction(const std::string& ip, int port, const std::string& target_peer_id) {
            std::cout << "[PEER] Requesting introduction to " << target_peer_id << std::endl;
            SendSecurePacket(m_socket, ip, port, PT_INTRO_REQUEST,
                target_peer_id.c_str(), target_peer_id.size());
        }

        void PeerManager::SendPunchPacket(const std::string& ip, int port, const std::string& peer_id) {
            PunchPayload payload;
            std::memset(&payload, 0, sizeof(payload));

            std::string short_id = m_identity ? m_identity->GetShortID() : "unknown";
            std::strncpy(payload.sender_id, short_id.c_str(), sizeof(payload.sender_id) - 1);
            payload.timestamp = std::time(nullptr);
            payload.attempt_num = 1;

            std::cout << "[PUNCH] Sending punch to " << ip << ":" << port << std::endl;
            SendSecurePacket(m_socket, ip, port, PT_PUNCH, &payload, sizeof(payload));
        }

        void PeerManager::HandlePunchPacket(const std::string& ip, int port, const PunchPayload* payload) {
            std::cout << "[PUNCH] Received punch from " << payload->sender_id
                << " @ " << ip << ":" << port << std::endl;

            SendSecurePacket(m_socket, ip, port, PT_PUNCH_ACK, payload, sizeof(PunchPayload));

            auto peers = GetKnownPeers();
            for (const auto& peer : peers) {
                if (peer.peer_id == payload->sender_id) {
                    break;
                }
            }

            m_punchManager.MarkSuccess(payload->sender_id);
        }

        void PeerManager::StorePeerPublicKey(const std::string& peer_id, const std::string& pem) {
            std::lock_guard<std::mutex> lock(m_networkMutex);
            m_peerPublicKeys[peer_id] = pem;
            std::cout << "[KEYS] Stored public key for peer: " << peer_id << std::endl;
        }

        std::string PeerManager::GetPeerPublicKey(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_networkMutex);
            auto it = m_peerPublicKeys.find(peer_id);
            if (it != m_peerPublicKeys.end()) {
                return it->second;
            }
            return "";
        }

    }
}