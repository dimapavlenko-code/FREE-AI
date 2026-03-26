#include "network/HolePunchManager.hpp"
#include <algorithm>
#include <iostream>

namespace FreeAI {
    namespace Network {

        HolePunchManager::HolePunchManager() {}

        HolePunchManager::~HolePunchManager() {}

        bool HolePunchManager::StartPunch(const std::string& ip, int port, const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Check if already active
            for (const auto& session : m_sessions) {
                if (session.target_id == peer_id && !session.success) {
                    return false; // Already punching
                }
            }

            PunchSession newSession;
            newSession.target_ip = ip;
            newSession.target_port = port;
            newSession.target_id = peer_id;
            newSession.start_time = std::chrono::steady_clock::now();
            newSession.attempt_count = 0;
            newSession.success = false;

            m_sessions.push_back(newSession);
            std::cout << "[PUNCH] Starting hole punch to " << ip << ":" << port 
                      << " (Peer: " << peer_id << ")" << std::endl;
            return true;
        }

        bool HolePunchManager::IsPunchActive(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& session : m_sessions) {
                if (session.target_id == peer_id && !session.success) {
                    // Check timeout
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - session.start_time).count();
                    if (elapsed < PUNCH_TIMEOUT_MS) {
                        return true;
                    }
                }
            }
            return false;
        }

        void HolePunchManager::MarkSuccess(const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& session : m_sessions) {
                if (session.target_id == peer_id) {
                    session.success = true;
                    std::cout << "[PUNCH] SUCCESS! Direct connection to " << peer_id << std::endl;
                    break;
                }
            }
        }

        void HolePunchManager::RecordAttempt(const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& session : m_sessions) {
                if (session.target_id == peer_id) {
                    session.attempt_count++;
                    std::cout << "[PUNCH] Attempt " << session.attempt_count 
                              << " to " << session.target_id << std::endl;
                    break;
                }
            }
        }

        std::vector<PunchSession> HolePunchManager::GetActiveSessions() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<PunchSession> active;
            auto now = std::chrono::steady_clock::now();

            for (const auto& session : m_sessions) {
                if (!session.success) {
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - session.start_time).count();
                    if (elapsed < PUNCH_TIMEOUT_MS) {
                        active.push_back(session);
                    }
                }
            }
            return active;
        }

        void HolePunchManager::Cleanup() {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto now = std::chrono::steady_clock::now();

            m_sessions.erase(
                std::remove_if(m_sessions.begin(), m_sessions.end(),
                    [now](const PunchSession& session) {
                        if (session.success) {
                            return true; // Remove completed sessions
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - session.start_time).count();
                        return elapsed >= PUNCH_TIMEOUT_MS; // Remove timed out sessions
                    }),
                m_sessions.end());
        }

        bool HolePunchManager::ShouldSendPunch(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& session : m_sessions) {
                if (session.target_id == peer_id && !session.success) {
                    // Check if enough time has passed since last attempt
                    auto now = std::chrono::steady_clock::now();
                    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                        now - session.start_time).count();
                    
                    // Calculate expected attempt number
                    int expected_attempt = int(elapsed / PUNCH_INTERVAL_MS) + 1;
                    
                    if (expected_attempt > session.attempt_count && 
                        expected_attempt <= MAX_PUNCH_ATTEMPTS) {
                        return true;
                    }
                }
            }
            return false;
        }

    }
}
