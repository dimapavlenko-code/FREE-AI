#pragma once
#include "network/UDPSocket.hpp"
#include "network/PeerConnectionTracker.hpp"
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
#include <memory>

namespace FreeAI {
    namespace Network {

        struct PeerInfo {
            std::string ip;
            int port;
            std::string peer_id;
            std::string public_key_pem;
            int64_t last_seen;
            bool is_super_node;
            bool is_direct;  // Can we connect directly (hole punch successful)?
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
            std::unordered_map<std::string, std::string> m_peerPublicKeys;  // peer_id → PEM

            DHT m_dht;  // DHT engine

            // Track seed registration state using PeerConnectionTracker
            std::thread m_seedThread;
            std::vector<std::shared_ptr<PeerConnectionTracker>> m_seedTrackers;

            mutable std::mutex m_networkMutex;  // Single mutex for seeds + keys

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

            // STUN/Coordination helper methods
            ExternalAddress QueryPeerExternalAddress(const std::string& peer_ip, int peer_port);
            void SendHolePunchInfo(const std::string& ip, int port, const std::string& peer_id,
                                  const ExternalAddress& peerAddr, uint64_t punchStartTime,
                                  uint8_t port_range = 0);
            void SendHolePunchStart(const std::string& ip, int port, uint64_t punchStartTime);
            void SendCoordHolePunchFailed(const std::string& ip, int port,
                                         const std::string& requester_id, const std::string& target_id,
                                         const char* reason);
            
            // Send punch failure report to coordinator
            void SendPunchFailureReport(const std::string& coordinator_ip, int coordinator_port,
                                       const std::string& peer_id, const std::string& peer_ip,
                                       int peer_port, uint8_t phase);
            
            // Handle punch failure report from peer (only on super node)
            void HandlePunchFailureReport(const std::string& sender_ip, int sender_port,
                                         const CoordHolePunchFailedPayload* payload);

        private:
            void PunchLoop();  // Background thread for punch attempts
            void ListenLoop();
            void HandleBootstrapPacket(UDPSocket& sock, char* buffer, int bytes, const std::string& ip, int port);
            void ConnectToSeeds();

            void SendInitialRegistrations();
            void SendRegistration(PeerConnectionTracker::TrackerInfo& reg);
            void SeedRegistrationLoop();

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