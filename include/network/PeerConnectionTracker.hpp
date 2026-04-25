#pragma once
#include <string>
#include <chrono>
#include <mutex>
#include <cstdint>
#include <memory>
#include <vector>

namespace FreeAI {
    namespace Network {

        enum class PeerConnectionState {
            Disconnected,      // Never attempted or recovered to initial state
            Connecting,        // REGISTER sent, awaiting response
            Connected,         // Handshake complete (ers_accepted received)
            Failed,            // Registration failed after max attempts
            Reset              // Received ers_failed, clearing state
        };

        // Tracks seed node connection state with exponential backoff
        class PeerConnectionTracker {
        public:
            static constexpr int MAX_RETRY_COUNT = 10;
            static constexpr int MAX_BACKOFF_DELAY_SEC = 60;
            static constexpr int RECOVERY_DELAY_SEC = 300;  // 5 minutes before retrying failed connections
            static constexpr int INITIAL_RETRY_DELAY_SEC = 2;

            struct TrackerInfo {
                std::string ip;
                int port;
                std::string seed_address;  // "ip:port" for lookup
                uint32_t first_attempt_ts;
                uint32_t last_attempt_ts;
                uint32_t last_success_ts;
                PeerConnectionState state;
                int retry_count;
                int next_retry_delay_sec;

                TrackerInfo() 
                    : ip(""), port(0), seed_address(""),
                      first_attempt_ts(0), last_attempt_ts(0), last_success_ts(0),
                      state(PeerConnectionState::Disconnected),
                      retry_count(0), next_retry_delay_sec(INITIAL_RETRY_DELAY_SEC) {}
            };

            PeerConnectionTracker();

            // Initialize tracker for a seed node
            void Initialize(const std::string& seed_address, const std::string& ip, int port);

            // Get current state (thread-safe)
            TrackerInfo GetInfo() const;

            // Update state (thread-safe)
            void SetState(PeerConnectionState state);
            void SetLastSuccessTime(uint32_t timestamp);

            // Check if connection should be attempted
            bool ShouldRetry(uint32_t current_time) const;

            // Record a retry attempt, returns true if retry should proceed
            bool RecordRetryAttempt();

            // Check if connection is verified (connected)
            bool IsConnected() const;

            // Check if connection is in terminal failure state
            bool IsFailed() const;

            // Recover from failed state (reset for retry)
            bool TryRecover(uint32_t current_time);

            // Get delay until next retry in seconds
            int GetRetryDelay(uint32_t current_time) const;

            // Check if all trackers are connected
            static bool AllConnected(const std::vector<std::shared_ptr<PeerConnectionTracker>>& trackers);

        private:
            mutable std::mutex m_mutex;
            TrackerInfo m_info;
        };

    }
}
