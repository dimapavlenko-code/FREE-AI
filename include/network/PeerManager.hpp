#pragma once
#include "network/UDPSocket.hpp"
#include "utils/Config.hpp"
#include "crypto/Identity.hpp"
#include "network/PacketSecurity.hpp"
#include "network/HolePunchManager.hpp"
#include "network/DHT.hpp"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_map>

namespace FreeAI {
    namespace Network {

        struct PeerInfo {
            std::string ip;
            int port;
            std::string peer_id;
            std::string public_key_pem;
            int64_t last_seen;
            bool is_super_node;
            bool is_direct;  // NEW: Can we connect directly (hole punch successful)?
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

            HolePunchManager m_punchManager;
            std::thread m_punchThread;

            // Store peer public keys for signature verification
            mutable std::mutex m_keyMutex;
            std::unordered_map<std::string, std::string> m_peerPublicKeys;  // peer_id → PEM

            DHT m_dht;  // DHT engine

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

            // Hole punching methods
            void RequestIntroduction(const std::string& ip, int port, const std::string& target_peer_id);
            void SendPunchPacket(const std::string& ip, int port, const std::string& peer_id);
            void HandlePunchPacket(const std::string& ip, int port, const PunchPayload* payload);    

            // Key exchange methods
            void StorePeerPublicKey(const std::string& peer_id, const std::string& pem);
            std::string GetPeerPublicKey(const std::string& peer_id) const;

            // DHT access
            DHT& GetDHT() { return m_dht; }
            size_t GetDHTNodeCount() const { return m_dht.GetNodeCount(); }

        private:
            void PunchLoop();  // Background thread for punch attempts
            void ListenLoop();
            void HandleBootstrapPacket(UDPSocket& sock, char* buffer, int bytes, const std::string& ip, int port);
            void ConnectToSeeds();
            void RequestPeerList(const std::string& ip, int port);

            // DHT methods
            void ProcessDHTPacket(UDPSocket& sock, const std::string& ip, int port,
                uint8_t type, const std::vector<uint8_t>& payload);
            void HandleDHTFindNode(UDPSocket& sock, const std::string& ip, int port,
                const DHTFindNodePayload* payload);
            void HandleDHTFindNodeResponse(const std::string& ip, int port,
                const DHTFindNodeResponsePayload* payload);
            
        };

    }
}