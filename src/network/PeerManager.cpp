#include "network/PeerManager.hpp"
#include "network/Protocol.hpp"
#include "network/PacketSecurity.hpp"
#include "utils/ThreadUtils.hpp"
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>

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

            m_listenerThread = std::thread(&PeerManager::ListenLoop, this);

            // Connect to seeds in background
            std::thread(&PeerManager::ConnectToSeeds, this).detach();
        }

        void PeerManager::Stop() {
            m_running = false;
            if (m_listenerThread.joinable()) {
                m_listenerThread.join();
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
            // Process incoming secure packet
            SecurePacketHeader header;
            std::vector<uint8_t> payload;

            if (!PacketSecurity::ProcessIncoming(
                reinterpret_cast<uint8_t*>(buffer), bytes, header, payload, m_identity)) {
                std::cerr << "[PEER] Invalid packet from " << ip << ":" << port << std::endl;
                return;
            }

            // Verify signature if present
            if (header.flags & FLAG_SIGNED) {
                // TODO: Verify against sender's public key (requires key exchange)
                // For now, accept signed packets from known peers
                std::cout << "[PEER] Received signed packet from " << ip << ":" << port << std::endl;
            }

            if (payload.empty() || payload.size() < sizeof(SecurePacketHeader)) {
                return;
            }

            // Parse inner packet header
            SecurePacketHeader* hdr = reinterpret_cast<SecurePacketHeader*>(payload.data());
            size_t payloadOffset = sizeof(SecurePacketHeader);
            size_t payloadSize = payload.size() - payloadOffset;

            // Handle Packet Types
            if (hdr->type == PT_REGISTER && m_isSuperNode) {
                std::string peer_id = std::string(
                    reinterpret_cast<const char*>(payload.data() + payloadOffset),
                    payloadSize);
                std::cout << "[PEER] Registered: " << peer_id << " @ " << ip << ":" << port << std::endl;

                // Add to known peers
                AddPeer({ ip, port, peer_id, std::time(nullptr), true });
            }
            else if (hdr->type == PT_INTRO_REQUEST && m_isSuperNode) {
                // Return a random known peer
                auto peers = GetKnownPeers();
                if (!peers.empty()) {
                    const auto& target = peers[0];
                    std::string response_data = target.ip + ":" + std::to_string(target.port);

                    SendSecurePacket(sock, ip, port, PT_INTRO_RESPONSE,
                        response_data.c_str(), response_data.size());
                    std::cout << "[PEER] Introduced " << ip << " to " << target.ip << std::endl;
                }
            }
            else if (hdr->type == PT_INTRO_RESPONSE) {
                std::string target_info = std::string(
                    reinterpret_cast<const char*>(payload.data() + payloadOffset),
                    payloadSize);
                std::cout << "[PEER] Received intro: " << target_info << std::endl;
                // Parse and connect to introduced peer
            }
            else if (hdr->type == PT_PEER_LIST) {
                auto newPeers = ParsePeerList(
                    reinterpret_cast<const char*>(payload.data() + payloadOffset),
                    payloadSize);
                for (const auto& p : newPeers) {
                    AddPeer(p);
                }
                std::cout << "[PEER] Received " << newPeers.size() << " peers from seed." << std::endl;
            }
            else if (hdr->type == PT_PUNCH) {
                std::cout << "[PEER] Received punch from " << ip << ":" << port << std::endl;
                // Hole punching successful - can now communicate directly
            }
        }

        void PeerManager::ConnectToSeeds() {
            std::this_thread::sleep_for(std::chrono::seconds(2)); // Wait for socket to be ready

            for (const auto& seed : m_seedNodes) {
                // Parse seed IP:Port
                size_t pos = seed.find(':');
                if (pos == std::string::npos) continue;

                std::string ip = seed.substr(0, pos);
                int port = std::stoi(seed.substr(pos + 1));

                std::cout << "[PEER] Connecting to seed: " << ip << ":" << port << std::endl;

                // Send registration (SIGNED - critical packet)
                std::string peer_id = m_identity ? m_identity->GetShortID() : "unknown";
                SendSecurePacket(m_socket, ip, port, PT_REGISTER,
                    peer_id.c_str(), peer_id.size());

                // Request peer list (SIGNED - prevents poisoning)
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

    }
}