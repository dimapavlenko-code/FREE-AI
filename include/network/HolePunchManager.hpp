#pragma once
#include "network/Protocol.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <chrono>

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

        class HolePunchManager {
        public:
            HolePunchManager();
            ~HolePunchManager();

            // Start a hole punch attempt to a target peer
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

        private:
            mutable std::mutex m_mutex;
            std::vector<PunchSession> m_sessions;
        };

    }
}