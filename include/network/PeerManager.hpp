#pragma once
#include "network/UDPSocket.hpp"
#include "utils/Config.hpp"
#include "crypto/Identity.hpp"
#include "network/PacketSecurity.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>

namespace FreeAI {
    namespace Network {

        struct PeerInfo {
            std::string ip;
            int port;
            std::string peer_id;
            int64_t last_seen;
            bool is_super_node;
        };

        class PeerManager {
            std::vector<PeerInfo> ParsePeerList(const char* data, size_t size);
            std::string BuildPeerList();

            UDPSocket m_socket;
            std::thread m_listenerThread;
            std::atomic<bool> m_running;
            mutable std::mutex m_peerMutex;
            std::vector<PeerInfo> m_peers;

            int m_listenPort;
            bool m_isSuperNode;
            std::vector<std::string> m_seedNodes;

            const Crypto::Identity* m_identity = nullptr; // Reference to main identity
            bool m_enableSigning = true;

        public:
            
            PeerManager();
            ~PeerManager();

            // Initialize with config
            bool Initialize(const Utils::Config& config);

            void SetIdentity(Crypto::Identity* identity);

            void SetSigningEnabled(bool enableSigning);

            // Start background threads
            void Start();

            // Stop all threads
            void Stop();

            // Get list of known peers (thread-safe)
            std::vector<PeerInfo> GetKnownPeers() const;

            // Add a peer to the list
            void AddPeer(const PeerInfo& peer);

            // Check if we are a Super Node (can accept incoming)
            bool IsSuperNode() const;

            // Helper to send signed packet
            bool SendSecurePacket(UDPSocket& socket, const std::string& ip, int port,
                uint8_t type, const void* payload, size_t size);

        private:
            void ListenLoop();
            void HandleBootstrapPacket(UDPSocket& sock, char* buffer, int bytes, const std::string& ip, int port);
            void ConnectToSeeds();
            void RequestPeerList(const std::string& ip, int port);
            
        };

    }
}