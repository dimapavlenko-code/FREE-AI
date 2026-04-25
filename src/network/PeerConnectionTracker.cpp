#include "network/PeerConnectionTracker.hpp"
#include <algorithm>
#include <iostream>

namespace FreeAI {
    namespace Network {

        PeerConnectionTracker::PeerConnectionTracker() = default;

        void PeerConnectionTracker::Initialize(const std::string& seed_address, const std::string& ip, int port) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_info.seed_address = seed_address;
            m_info.ip = ip;
            m_info.port = port;
            m_info.state = PeerConnectionState::Disconnected;
            m_info.retry_count = 0;
            m_info.next_retry_delay_sec = INITIAL_RETRY_DELAY_SEC;
            m_info.first_attempt_ts = 0;
            m_info.last_attempt_ts = 0;
            m_info.last_success_ts = 0;
        }

        PeerConnectionTracker::TrackerInfo PeerConnectionTracker::GetInfo() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_info;
        }

        void PeerConnectionTracker::SetState(PeerConnectionState state) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_info.state = state;
        }

        void PeerConnectionTracker::SetLastSuccessTime(uint32_t timestamp) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_info.last_success_ts = timestamp;
            m_info.last_attempt_ts = timestamp;
        }

        bool PeerConnectionTracker::ShouldRetry(uint32_t current_time) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            switch (m_info.state) {
                case PeerConnectionState::Disconnected:
                case PeerConnectionState::Connecting:
                    return (current_time >= m_info.last_attempt_ts + m_info.next_retry_delay_sec);
                    
                case PeerConnectionState::Failed:
                    return (current_time >= m_info.last_attempt_ts + RECOVERY_DELAY_SEC);
                    
                case PeerConnectionState::Connected:
                case PeerConnectionState::Reset:
                    return false;
            }
            return false;
        }

        bool PeerConnectionTracker::RecordRetryAttempt() {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            m_info.retry_count++;
            m_info.last_attempt_ts = static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());
            
            if (m_info.first_attempt_ts == 0) {
                m_info.first_attempt_ts = m_info.last_attempt_ts;
            }
            
            // Exponential backoff: 2, 4, 8, 16, 32, 60, 60, ...
            m_info.next_retry_delay_sec = std::min(
                INITIAL_RETRY_DELAY_SEC * (1 << (m_info.retry_count - 1)),
                MAX_BACKOFF_DELAY_SEC);
            
            return m_info.retry_count <= MAX_RETRY_COUNT;
        }

        bool PeerConnectionTracker::IsConnected() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_info.state == PeerConnectionState::Connected;
        }

        bool PeerConnectionTracker::IsFailed() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_info.state == PeerConnectionState::Failed;
        }

        bool PeerConnectionTracker::TryRecover(uint32_t current_time) {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (m_info.state != PeerConnectionState::Failed) {
                return false;
            }
            
            if (current_time < m_info.last_attempt_ts + RECOVERY_DELAY_SEC) {
                return false;
            }
            
            m_info.state = PeerConnectionState::Disconnected;
            m_info.retry_count = 0;
            m_info.next_retry_delay_sec = INITIAL_RETRY_DELAY_SEC;
            
            std::cout << "[PEER-TRACKER] Recovered from failed state for " 
                      << m_info.seed_address << std::endl;
            return true;
        }

        int PeerConnectionTracker::GetRetryDelay(uint32_t current_time) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (current_time >= m_info.last_attempt_ts + m_info.next_retry_delay_sec) {
                return 0;  // Ready to retry immediately
            }
            
            return static_cast<int>(
                m_info.last_attempt_ts + m_info.next_retry_delay_sec - current_time);
        }

        bool PeerConnectionTracker::AllConnected(const std::vector<std::shared_ptr<PeerConnectionTracker>>& trackers) {
            if (trackers.empty()) {
                return false;
            }
            
            for (const auto& tracker : trackers) {
                if (!tracker || !tracker->IsConnected()) {
                    return false;
                }
            }
            return true;
        }

    }
}
