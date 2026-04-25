#pragma once
#include "network/Protocol.hpp"
#include "network/UDPSocket.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>
#include <map>
#include <unordered_map>

namespace FreeAI {
    namespace Network {

        struct PunchSession {
            std::string target_ip;
            int target_port;
            std::string target_id;
            std::chrono::steady_clock::time_point start_time;
            int attempt_count;
            bool success;
        };

        // Multi-port punch session for tracking port ranges
        struct MultiPortPunchSession {
            std::string target_ip;
            int target_base_port;       // Base port of the range
            uint8_t  target_port_range; // Number of ports in range
            std::string target_id;
            std::chrono::steady_clock::time_point start_time;
            int attempt_count;
            bool success;
            // Track which ports have been successfully punched
            bool punched_ports[MAX_PORT_RANGE];
        };

        // External address discovered via STUN
        struct ExternalAddress {
            std::string ip;
            int port;
            bool discovered;
        };

        // Punch attempt tracking for automatic strategy selection
        struct PunchAttemptTracker {
            std::string peer_id;
            std::string peer_ip;
            int peer_port;
            int single_port_attempts;       // Count of single-port punch attempts
            int multi_port_attempts;        // Count of multi-port punch attempts
            bool single_port_failed;        // Whether single-port punching failed
            bool multi_port_failed;         // Whether multi-port punching failed
            bool reported_single_failure;   // Whether we reported single-port failure to STUN
            bool reported_multi_failure;    // Whether we reported multi-port failure to STUN
            std::chrono::steady_clock::time_point last_attempt_time;
            std::chrono::steady_clock::time_point start_time;
            enum class Phase {
                SinglePort,       // Currently trying single-port
                SwitchingToMulti, // Switching to multi-port
                MultiPort,        // Currently trying multi-port
                Failed,           // All methods failed
                Success           // Connection succeeded, stop tracking
            } current_phase;
        };

        // Failure report tracking for STUN server coordination
        struct FailureReport {
            std::string peer_id;
            std::string peer_ip;
            int peer_port;
            uint8_t phase;              // 0=single-port, 1=multi-port
            std::chrono::steady_clock::time_point report_time;
        };

        class HolePunchManager {
        public:
            HolePunchManager();
            ~HolePunchManager();

            // Initialize STUN server on the given port
            bool InitializeSTUNServer(int stunPort = 3478);

            // Shutdown STUN server
            void ShutdownSTUNServer();

            // Start STUN server thread
            void StartSTUNServer();

            // Handle incoming STUN packet (call from packet handler)
            // Stores the discovered external address in m_peerExternalAddress
            bool HandleSTUNPacket(const char* data, int length);

            // Send STUN Binding Request to a peer's STUN server
            ExternalAddress QuerySTUNServer(const std::string& stun_server_ip, int stun_server_port);

            // Get external address as seen by a specific STUN server
            ExternalAddress GetExternalAddress(const std::string& stun_server_ip, int stun_server_port);

            // Coordinate hole punch via a coordinator peer
            bool RequestHolePunch(const std::string& coordinator_ip, int coordinator_port,
                                  const std::string& coordinator_stun_ip, int coordinator_stun_port,
                                  const std::string& target_id,
                                  const std::string& target_stun_ip, int target_stun_port);

            // Handle hole punch info from coordinator
            bool HandleHolePunchInfo(const std::string& sender_ip, int sender_port,
                                    const char* data, int length);

            // Handle hole punch start signal
            bool HandleHolePunchStart(const std::string& sender_ip, int sender_port,
                                     const char* data, int length);

            // Start a hole punch attempt to a target
            bool StartPunch(const std::string& ip, int port, const std::string& peer_id);

            // Check if a punch session is still active
            bool IsPunchActive(const std::string& peer_id) const;

            // Record a successful punch
            void MarkSuccess(const std::string& peer_id);

            // Record a punch attempt (for rate limiting)
            void RecordAttempt(const std::string& peer_id);

            // Get active punch sessions (for cleanup)
            std::vector<PunchSession> GetActiveSessions() const;

            // Cleanup old/failed sessions
            void Cleanup();

            // Check if we should send another punch packet
            bool ShouldSendPunch(const std::string& peer_id) const;

            // Get the current attempt count for a peer's session
            int GetAttemptCount(const std::string& peer_id) const;

            // Get our external address via our own STUN server
            ExternalAddress GetMyExternalAddress() const;

            // =====================================================================
            // Multi-port hole punching methods
            // =====================================================================

            // Start a multi-port hole punch to a target (punches a range of ports)
            bool StartMultiPortPunch(const std::string& ip, int base_port,
                                     uint8_t port_range, const std::string& peer_id);

            // Handle multi-port hole punch info from coordinator
            bool HandleMultiPortHolePunchInfo(const std::string& sender_ip, int sender_port,
                                               const char* data, int length);

            // Send punch packets across a port range
            bool SendMultiPortPunch(const std::string& ip, int base_port,
                                    uint8_t port_range, const std::string& peer_id);

            // Check if a multi-port punch session is active
            bool IsMultiPortPunchActive(const std::string& peer_id) const;

            // Record a successful port punch in multi-port session
            void MarkMultiPortSuccess(const std::string& peer_id, uint8_t port_index);

            // Get active multi-port punch sessions
            std::vector<MultiPortPunchSession> GetActiveMultiPortSessions() const;

            // Cleanup failed multi-port sessions
            void CleanupMultiPortSessions();

            // =====================================================================
            // Automatic strategy selection
            // =====================================================================

            // Determine if multi-port punching should be used
            // Returns true if we should use multi-port, false for single-port
            bool ShouldUseMultiPortPunch() const;
            // Peer-specific overload: check if a specific peer should use multi-port
            bool ShouldUseMultiPortPunch(const std::string& peer_id) const;

            // Start punch with automatic strategy selection
            // Automatically chooses single-port or multi-port based on network conditions
            bool StartPunchAuto(const std::string& ip, int port, const std::string& peer_id,
                               uint8_t suggested_port_range = 0);

            // Check if we should send a punch packet (works for both single and multi-port)
            bool ShouldSendPunchAuto(const std::string& peer_id) const;

            // Get the punch strategy for a peer
            enum class PunchStrategy {
                SinglePort,
                MultiPort
            };
            PunchStrategy GetPunchStrategy(const std::string& peer_id) const;

            // Record a successful punch (auto-detects strategy)
            void RecordPunchSuccess(const std::string& peer_id, uint8_t port_index = 0);

            // Report punch failure to the STUN server for coordination
            void ReportPunchFailure(const std::string& coordinator_ip, int coordinator_port,
                                   const std::string& peer_id, const std::string& peer_ip, int peer_port,
                                   uint8_t phase);  // phase: 0=single-port, 1=multi-port

            // Check if both peers have failed single-port punching
            bool ShouldSwitchToMultiPort(const std::string& peer_id) const;

            // Force switch to multi-port punching
            void SwitchToMultiPortPunch(const std::string& peer_id);

            // Get all failed punch trackers (for STUN server coordination)
            std::vector<PunchAttemptTracker> GetFailedTrackers() const;

            // =====================================================================
            // STUN server coordination methods
            // =====================================================================

            // Record a peer's failure report (called by both peers)
            // Returns true when both peers have failed and multi-port should be initiated
            bool RecordFailureReport(const std::string& peer_id, const std::string& peer_ip,
                                    int peer_port, uint8_t phase);
            
            // Check if a specific peer has reported failure for a phase
            bool HasPeerReportedFailure(const std::string& peer_id, uint8_t phase) const;
            
            // Get peers that have failed single-port punching
            std::vector<std::string> GetSinglePortFailedPeers() const;
            
            // Get peers that have failed multi-port punching
            std::vector<std::string> GetMultiPortFailedPeers() const;
            
            // Check if a peer pair has both failed single-port
            bool BothPeersFailedSinglePort(const std::string& peer_a, const std::string& peer_b) const;
            
            // Check if a peer pair has both failed multi-port
            bool BothPeersFailedMultiPort(const std::string& peer_a, const std::string& peer_b) const;
            
            // Clear failure reports for a peer
            void ClearFailureReports(const std::string& peer_id);

        private:
            // STUN server background thread
            void STUNServerLoop();

            // Build a STUN Binding Response
            std::vector<uint8_t> BuildSTUNBindingResponse(const sockaddr_in& clientAddr);

            // Generate a random STUN transaction ID
            void GenerateSTUNTransactionID(char* tid);

            // =====================================================================
            // Multi-port hole punching helpers
            // =====================================================================

            // Find or create a multi-port punch session
            MultiPortPunchSession* FindOrCreateMultiPortSession(const std::string& peer_id);

            // Check if all ports in a multi-port session are punched
            bool IsMultiPortSessionComplete(MultiPortPunchSession& session) const;

            // =====================================================================
            // Automatic strategy helpers
            // =====================================================================

            // Find or create a punch attempt tracker
            PunchAttemptTracker* FindOrCreateTracker(const std::string& peer_id);

            // Find a punch attempt tracker
            PunchAttemptTracker* FindTracker(const std::string& peer_id) const;

            // Update tracker after a punch attempt
            void UpdateTrackerAfterAttempt(const std::string& peer_id, bool success);

            mutable std::mutex m_mutex;
            std::vector<PunchSession> m_sessions;
            
            // Multi-port punch sessions
            std::vector<MultiPortPunchSession> m_multiPortSessions;

            // STUN server state
            UDPSocket m_stunSocket;
            int m_stunPort;
            std::thread m_stunThread;
            std::atomic<bool> m_stunRunning;

            // Cached external addresses
            mutable std::mutex m_stunCacheMutex;
            std::unordered_map<std::string, ExternalAddress> m_stunCache;  // key: "ip:port"
            
            // Hole punch coordination state
            mutable std::mutex m_coordMutex;
            ExternalAddress m_peerExternalAddress;  // External address of the peer we're punching
            std::chrono::steady_clock::time_point m_punchStartTime;
            bool m_holePunchCoordinationActive;
            
            // Local peer ID for hole punch coordination
            std::string m_localPeerID;

            // Punch attempt tracking for automatic strategy selection
            mutable std::mutex m_trackerMutex;
            std::vector<PunchAttemptTracker> m_punchTrackers;

            // Failure report tracking for STUN server coordination
            mutable std::mutex m_failureReportMutex;
            std::vector<FailureReport> m_failureReports;

            // Constants for automatic strategy
            static const int MAX_SINGLE_PORT_ATTEMPTS = 5;
            static const int MAX_MULTI_PORT_ATTEMPTS = 5;
            static const uint8_t DEFAULT_MULTI_PORT_RANGE = 5;

            // Set/get local peer ID for hole punch coordination
            void SetLocalPeerID(const std::string& peer_id) { m_localPeerID = peer_id; }
            std::string GetLocalPeerID() const { return m_localPeerID.empty() ? "unknown" : m_localPeerID; }
        };

    }
}