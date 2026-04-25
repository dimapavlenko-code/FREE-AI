#include "network/HolePunchManager.hpp"
#include "network/Protocol.hpp"
#include "network/STUNParser.hpp"
#include "utils/ThreadUtils.hpp"
#include "utils/Helpers.hpp"
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <set>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

namespace FreeAI {
    namespace Network {

        HolePunchManager::HolePunchManager()
            : m_stunPort(3478), m_stunRunning(false),
              m_peerExternalAddress{"", 0, false},
              m_punchStartTime(std::chrono::steady_clock::now()),
              m_holePunchCoordinationActive(false) {
        }

        HolePunchManager::~HolePunchManager() {
            ShutdownSTUNServer();
        }

        // =====================================================================
        // STUN Server Implementation
        // =====================================================================

        bool HolePunchManager::InitializeSTUNServer(int stunPort) {
            m_stunPort = stunPort;

            if (!m_stunSocket.Create()) {
                std::cerr << "[STUN] Failed to create STUN socket." << std::endl;
                return false;
            }

            if (!m_stunSocket.Bind(stunPort)) {
                std::cerr << "[STUN] Failed to bind STUN socket to port " << stunPort << std::endl;
                m_stunSocket.Close();
                return false;
            }

            // Set non-blocking for the STUN socket
            m_stunSocket.SetNonBlocking(true);

            std::cout << "[STUN] STUN server initialized on port " << stunPort << std::endl;
            return true;
        }

        void HolePunchManager::ShutdownSTUNServer() {
            m_stunRunning = false;

            if (m_stunThread.joinable()) {
                m_stunThread.join();
            }

            if (m_stunSocket.IsValid()) {
                m_stunSocket.Close();
            }
        }

        void HolePunchManager::StartSTUNServer() {
            m_stunRunning = true;
            m_stunThread = std::thread(&HolePunchManager::STUNServerLoop, this);
            std::cout << "[STUN] STUN server thread started on port " << m_stunPort << std::endl;
        }

        void HolePunchManager::STUNServerLoop() {
            FreeAI::Utils::SetThreadPriorityLow();

            char buffer[MAX_PACKET_SIZE];
            std::string sender_ip;
            int sender_port;

            while (m_stunRunning) {
                int bytes = m_stunSocket.ReceiveFrom(buffer, MAX_PACKET_SIZE, sender_ip, sender_port);
                
                if (bytes > 0) {
                    // Check if this is a STUN Binding Request
                    // Minimum STUN message size is 20 bytes (header)
                    if (bytes >= 20) {
                        uint16_t* msgHeader = reinterpret_cast<uint16_t*>(buffer);
                        uint16_t classMethod = msgHeader[0];
                        uint16_t method = msgHeader[1];

                        // Check if it's a Binding Request (class 0x0000, method 0x0001)
                        // STUN header layout: Class (2 bytes) + Method (2 bytes) + Length (2 bytes)
                        // For Binding Request: class = 0x0000 (request class), method = 0x0001 (binding)
                        if (classMethod == 0x0000 && method == 0x0001) {
                            std::cout << "[STUN] Received Binding Request from " << sender_ip << ":" << sender_port << std::endl;

                            // Build sockaddr_in for the client
                            sockaddr_in clientAddr{};
                            clientAddr.sin_family = AF_INET;
                            clientAddr.sin_port = htons(static_cast<uint16_t>(sender_port));
                            inet_pton(AF_INET, sender_ip.c_str(), &clientAddr.sin_addr);

                            // Build and send Binding Response
                            auto response = BuildSTUNBindingResponse(clientAddr);

                            if (!response.empty()) {
                                m_stunSocket.SendTo(response.data(), response.size(), sender_ip, sender_port);
                                std::cout << "[STUN] Sent Binding Response to " << sender_ip << ":" << sender_port << std::endl;
                            }
                        }
                        else {
                            // Not a Binding Request - ignore silently
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        bool HolePunchManager::HandleSTUNPacket(const char* data, int length) {
            // This is called when we receive a packet that might be a STUN response
            // Parse the STUN Binding Response and extract the MAPPED-ADDRESS
            if (!STUNParser::IsValidBindingResponse(data, length)) {
                return false;
            }

            // Parse Mapped-Address attribute
            m_peerExternalAddress = STUNParser::ParseMappedAddress(data, length);
            return m_peerExternalAddress.discovered;
        }

        std::vector<uint8_t> HolePunchManager::BuildSTUNBindingResponse(const sockaddr_in& clientAddr) {
            // Generate transaction ID
            char tid[STUN_TRANSACTION_ID_SIZE];
            GenerateSTUNTransactionID(tid);

            // Use STUNParser to build the response
            return STUNParser::BuildBindingResponse(clientAddr, tid);
        }

        void HolePunchManager::GenerateSTUNTransactionID(char* tid) {
            // Generate random transaction ID (16 bytes) using thread-local PRNG
            // Thread-local to avoid contention and ensure independence between threads
            thread_local std::mt19937 s_rng(std::random_device{}());
            std::uniform_int_distribution<unsigned int> dis(0, 255);

            for (int i = 0; i < STUN_TRANSACTION_ID_SIZE; ++i) {
                tid[i] = static_cast<char>(static_cast<uint8_t>(dis(s_rng)));
            }
        }

        // =====================================================================
        // STUN Client Implementation
        // =====================================================================

        ExternalAddress HolePunchManager::QuerySTUNServer(const std::string& stun_server_ip, int stun_server_port) {
            if (!m_stunSocket.IsValid()) {
                std::cerr << "[STUN] STUN socket not valid for client queries." << std::endl;
                return {"", 0, false};
            }

            // Create a temporary socket for the query
            UDPSocket querySocket;
            if (!querySocket.Create()) {
                std::cerr << "[STUN] Failed to create query socket." << std::endl;
                return {"", 0, false};
            }

            // Bind to any available port
            if (!querySocket.Bind(0)) {
                std::cerr << "[STUN] Failed to bind query socket." << std::endl;
                return {"", 0, false};
            }

            // Build STUN Binding Request
            char tid[STUN_TRANSACTION_ID_SIZE];
            GenerateSTUNTransactionID(tid);

            std::vector<uint8_t> request(24 + STUN_TRANSACTION_ID_SIZE);
            std::memset(request.data(), 0, request.size());

            // Message Type: Binding Request (0x0001)
            uint16_t msgType = htons(PT_STUN_BINDING_REQUEST);
            std::memcpy(request.data(), &msgType, sizeof(msgType));

            // Length: 0 (no attributes)
            uint16_t msgLen = 0;
            std::memcpy(request.data() + 2, &msgLen, sizeof(msgLen));

            // Magic Cookie
            uint32_t magicCookie = htonl(STUN_MAGIC_COOKIE);
            std::memcpy(request.data() + 4, &magicCookie, sizeof(magicCookie));

            // Transaction ID
            std::memcpy(request.data() + 8, tid, STUN_TRANSACTION_ID_SIZE);

            // Send Binding Request
            sockaddr_in targetAddr;
            std::memset(&targetAddr, 0, sizeof(targetAddr));
            targetAddr.sin_family = AF_INET;
            targetAddr.sin_port = htons(static_cast<uint16_t>(stun_server_port));
            inet_pton(AF_INET, stun_server_ip.c_str(), &targetAddr.sin_addr);

            int sent = querySocket.SendTo(request.data(), request.size(), stun_server_ip, stun_server_port);
            if (sent < 0) {
                std::cerr << "[STUN] Failed to send Binding Request to " << stun_server_ip << ":" << stun_server_port << std::endl;
                return {"", 0, false};
            }

            std::cout << "[STUN] Sent Binding Request to " << stun_server_ip << ":" << stun_server_port << std::endl;

            // Wait for response with timeout
            auto startTime = std::chrono::steady_clock::now();
            auto timeout = std::chrono::milliseconds(5000);

            while (std::chrono::steady_clock::now() - startTime < timeout) {
                char responseBuffer[MAX_PACKET_SIZE];
                std::string resp_ip;
                int resp_port;

                // Use timeout on socket
                querySocket.SetNonBlocking(true);
                int bytes = querySocket.ReceiveFrom(responseBuffer, MAX_PACKET_SIZE, resp_ip, resp_port);

                if (bytes > 4) {
                    // Check if response is from the STUN server
                    uint32_t respMagic;
                    std::memcpy(&respMagic, responseBuffer, sizeof(respMagic));
                    respMagic = ntohl(respMagic);

                    if (respMagic == STUN_MAGIC_COOKIE) {
                        // Check transaction ID matches
                        char respTid[STUN_TRANSACTION_ID_SIZE];
                        std::memcpy(respTid, responseBuffer + 4, STUN_TRANSACTION_ID_SIZE);
                        
                        if (std::memcmp(tid, respTid, STUN_TRANSACTION_ID_SIZE) == 0) {
                            // Validate and parse the response using STUNParser
                            if (STUNParser::IsValidBindingResponse(responseBuffer, bytes)) {
                                ExternalAddress addr = STUNParser::ParseMappedAddress(responseBuffer, bytes);
                                
                                if (addr.discovered) {
                                    std::cout << "[STUN] Received Binding Response from STUN server. "
                                              << "Our external address: " << addr.ip << ":" << addr.port << std::endl;
                                    return addr;
                                }
                            }
                        }
                    }
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }

            std::cerr << "[STUN] STUN query to " << stun_server_ip << ":" << stun_server_port << " timed out." << std::endl;
            return {"", 0, false};
        }

        ExternalAddress HolePunchManager::GetExternalAddress(const std::string& stun_server_ip, int stun_server_port) {
            // Check cache first
            std::string cacheKey = stun_server_ip + ":" + std::to_string(stun_server_port);
            
            {
                std::lock_guard<std::mutex> lock(m_stunCacheMutex);
                auto it = m_stunCache.find(cacheKey);
                if (it != m_stunCache.end() && it->second.discovered) {
                    return it->second;
                }
            }

            // Query STUN server
            ExternalAddress addr = QuerySTUNServer(stun_server_ip, stun_server_port);

            // Cache the result
            {
                std::lock_guard<std::mutex> lock(m_stunCacheMutex);
                m_stunCache[cacheKey] = addr;
            }

            return addr;
        }

        ExternalAddress HolePunchManager::GetMyExternalAddress() const {
            // This would need access to the local STUN socket's bound address
            // For now, return empty - the caller should use QuerySTUNServer
            return {"", 0, false};
        }

        // =====================================================================
        // Hole Punch Coordination
        // =====================================================================

        bool HolePunchManager::RequestHolePunch(const std::string& coordinator_ip, int coordinator_port,
                                                const std::string& coordinator_stun_ip, int coordinator_stun_port,
                                                const std::string& target_id,
                                                const std::string& /*target_stun_ip*/, int /*target_stun_port*/) {
            if (!m_stunSocket.IsValid()) {
                std::cerr << "[COORD] STUN socket not valid." << std::endl;
                return false;
            }

            // First, discover our own external address via the coordinator's STUN server
            ExternalAddress myExternal = QuerySTUNServer(coordinator_stun_ip, coordinator_stun_port);
            if (!myExternal.discovered) {
                std::cerr << "[COORD] Failed to discover our external address." << std::endl;
                return false;
            }

            std::cout << "[COORD] Our external address: " << myExternal.ip << ":" << myExternal.port << std::endl;

            // Build CoordHolePunchRequest payload
            std::vector<uint8_t> payload(sizeof(CoordHolePunchPayload));
            std::memset(payload.data(), 0, payload.size());

            CoordHolePunchPayload* req = reinterpret_cast<CoordHolePunchPayload*>(payload.data());
            
            // Get our short ID from the local peer ID (set via SetLocalPeerID)
            std::string myID = m_localPeerID.empty() ? "unknown" : m_localPeerID;
            fai_strncpy(req->requester_id, myID, sizeof(req->requester_id));
            fai_strncpy(req->target_id, target_id, sizeof(req->target_id));

            // Send the request to the coordinator
            int sent = m_stunSocket.SendTo(payload.data(), payload.size(), coordinator_ip, coordinator_port);
            if (sent < 0) {
                std::cerr << "[COORD] Failed to send hole punch request to coordinator " 
                          << coordinator_ip << ":" << coordinator_port << std::endl;
                return false;
            }

            std::cout << "[COORD] Sent hole punch request to coordinator for target " << target_id << std::endl;
            return true;
        }

        bool HolePunchManager::HandleHolePunchInfo(const std::string& /*sender_ip*/, int /*sender_port*/,
                                                   const char* data, int length) {
            if (length < static_cast<int>(sizeof(CoordHolePunchInfoPayload))) {
                return false;
            }

            const CoordHolePunchInfoPayload* info = reinterpret_cast<const CoordHolePunchInfoPayload*>(data);
            
            std::string peer_id(info->peer_id);
            std::string peer_ip(info->peer_ip);
            int peer_port = ntohs(info->peer_port);
            int stun_port = ntohs(info->stun_port);
            uint64_t startTime = info->punch_start_time;

            std::cout << "[COORD] Received hole punch info from coordinator:" << std::endl;
            std::cout << "  Target: " << peer_id << " @ " << peer_ip << ":" << peer_port << std::endl;
            std::cout << "  STUN port: " << stun_port << std::endl;
            std::cout << "  Punch start time: " << startTime << std::endl;

            // Store the peer's external address
            {
                std::lock_guard<std::mutex> lock(m_coordMutex);
                m_peerExternalAddress.ip = peer_ip;
                m_peerExternalAddress.port = peer_port;
                m_peerExternalAddress.discovered = true;
                m_punchStartTime = std::chrono::steady_clock::now();
                m_holePunchCoordinationActive = true;
            }

            // Start a punch session
            StartPunch(peer_ip, peer_port, peer_id);

            return true;
        }

        bool HolePunchManager::HandleHolePunchStart(const std::string& /*sender_ip*/, int /*sender_port*/,
                                                     const char* data, int length) {
            if (length < static_cast<int>(sizeof(CoordHolePunchStartPayload))) {
                return false;
            }

            const CoordHolePunchStartPayload* start = reinterpret_cast<const CoordHolePunchStartPayload*>(data);
            uint64_t startTime = start->punch_start_time;

            std::cout << "[COORD] Received hole punch start signal. Start time: " << startTime << std::endl;

            {
                std::lock_guard<std::mutex> lock(m_coordMutex);
                m_punchStartTime = std::chrono::steady_clock::now();
                m_holePunchCoordinationActive = true;
            }

            return true;
        }

        // =====================================================================
        // Hole Punch Management (existing functionality)
        // =====================================================================

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

        int HolePunchManager::GetAttemptCount(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& session : m_sessions) {
                if (session.target_id == peer_id && !session.success) {
                    return session.attempt_count;
                }
            }
            return 0;
        }

        // =====================================================================
        // Multi-port Hole Punching Implementation
        // =====================================================================

        MultiPortPunchSession* HolePunchManager::FindOrCreateMultiPortSession(const std::string& peer_id) {
            // Acquire mutex to prevent data races in multi-threaded scenarios
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Check if session already exists
            for (auto& session : m_multiPortSessions) {
                if (session.target_id == peer_id && !session.success) {
                    return &session;
                }
            }

            // Create new session (value initialization zero-initializes punched_ports)
            MultiPortPunchSession newSession{};
            newSession.target_id = peer_id;
            newSession.start_time = std::chrono::steady_clock::now();
            newSession.attempt_count = 0;
            newSession.success = false;
            newSession.target_port_range = MAX_PORT_RANGE;

            m_multiPortSessions.push_back(newSession);
            return &m_multiPortSessions.back();
        }

        bool HolePunchManager::IsMultiPortSessionComplete(MultiPortPunchSession& session) const {
            for (int i = 0; i < session.target_port_range && i < MAX_PORT_RANGE; ++i) {
                if (!session.punched_ports[i]) {
                    return false;
                }
            }
            return true;
        }

        bool HolePunchManager::StartMultiPortPunch(const std::string& ip, int base_port,
                                                     uint8_t port_range, const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_mutex);

            // Check if already active
            for (const auto& session : m_multiPortSessions) {
                if (session.target_id == peer_id && !session.success) {
                    return false; // Already punching
                }
            }

            // Value initialization zero-initializes punched_ports array
            MultiPortPunchSession newSession{};
            newSession.target_ip = ip;
            newSession.target_base_port = base_port;
            newSession.target_port_range = port_range > MAX_PORT_RANGE ? MAX_PORT_RANGE : port_range;
            newSession.target_id = peer_id;
            newSession.start_time = std::chrono::steady_clock::now();
            newSession.attempt_count = 0;
            newSession.success = false;

            m_multiPortSessions.push_back(newSession);
            std::cout << "[MULTI-PORT PUNCH] Starting multi-port hole punch to " << ip
                      << ":" << base_port << " (range: " << static_cast<int>(newSession.target_port_range)
                      << " ports, Peer: " << peer_id << ")" << std::endl;
            return true;
        }

        bool HolePunchManager::HandleMultiPortHolePunchInfo(const std::string& /*sender_ip*/, int /*sender_port*/,
                                                              const char* data, int length) {
            if (length < static_cast<int>(sizeof(CoordHolePunchInfoMultiPayload))) {
                return false;
            }

            const CoordHolePunchInfoMultiPayload* info = reinterpret_cast<const CoordHolePunchInfoMultiPayload*>(data);
            
            std::string peer_id(info->peer_id);
            std::string peer_ip(info->peer_ip);
            int peer_base_port = ntohs(info->peer_base_port);
            uint8_t peer_port_range = info->peer_port_range;
            int stun_port = ntohs(info->stun_port);
            uint64_t startTime = info->punch_start_time;
            bool use_multi_port_val = info->use_multi_port != 0;
            (void)use_multi_port_val; // Future use: determine punch strategy

            std::cout << "[MULTI-PORT COORD] Received multi-port hole punch info from coordinator:" << std::endl;
            std::cout << "  Target: " << peer_id << " @ " << peer_ip << ":" << peer_base_port << std::endl;
            std::cout << "  Port range: " << static_cast<int>(peer_port_range) << std::endl;
            std::cout << "  STUN port: " << stun_port << std::endl;
            std::cout << "  Punch start time: " << startTime << std::endl;

            // Store the peer's external address (base port)
            {
                std::lock_guard<std::mutex> lock(m_coordMutex);
                m_peerExternalAddress.ip = peer_ip;
                m_peerExternalAddress.port = peer_base_port;
                m_peerExternalAddress.discovered = true;
                m_punchStartTime = std::chrono::steady_clock::now();
                m_holePunchCoordinationActive = true;
            }

            // Start multi-port punch session
            StartMultiPortPunch(peer_ip, peer_base_port, peer_port_range, peer_id);

            return true;
        }

        bool HolePunchManager::SendMultiPortPunch(const std::string& ip, int base_port,
                                                   uint8_t port_range, const std::string& peer_id) {
            if (!m_stunSocket.IsValid()) {
                std::cerr << "[MULTI-PORT PUNCH] STUN socket not valid." << std::endl;
                return false;
            }

            int success_count = 0;
            
            // Get current attempt count from multi-port sessions (not single-port)
            int attempt = 0;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& session : m_multiPortSessions) {
                    if (session.target_id == peer_id && !session.success) {
                        attempt = session.attempt_count + 1;
                        break;
                    }
                }
            }
            
            // Send punch packets to each port in the range
            for (uint8_t i = 0; i < port_range && i < MAX_PORT_RANGE; ++i) {
                int target_port = base_port + i;
                
                // Build punch payload with port offset
                std::vector<uint8_t> payload(sizeof(PunchPayload));
                std::memset(payload.data(), 0, payload.size());
                
                PunchPayload* punchPayload = reinterpret_cast<PunchPayload*>(payload.data());
                fai_strncpy(punchPayload->sender_id, "local", sizeof(punchPayload->sender_id));
                punchPayload->timestamp = static_cast<uint64_t>(
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now().time_since_epoch()).count());
                punchPayload->attempt_num = static_cast<uint8_t>(attempt);
                punchPayload->port_offset = i;
                punchPayload->port_range = port_range;
                
                int sent = m_stunSocket.SendTo(payload.data(), payload.size(), ip, target_port);
                if (sent > 0) {
                    std::cout << "[MULTI-PORT PUNCH] Sent punch packet to " << ip
                              << ":" << target_port << " (offset: " << static_cast<int>(i)
                              << ", attempt: " << attempt << ")" << std::endl;
                    success_count++;
                }
            }
            
            return success_count > 0;
        }

        bool HolePunchManager::IsMultiPortPunchActive(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (const auto& session : m_multiPortSessions) {
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

        void HolePunchManager::MarkMultiPortSuccess(const std::string& peer_id, uint8_t port_index) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& session : m_multiPortSessions) {
                if (session.target_id == peer_id && !session.success) {
                    if (port_index < session.target_port_range && port_index < MAX_PORT_RANGE) {
                        session.punched_ports[port_index] = true;
                        std::cout << "[MULTI-PORT PUNCH] Port " << static_cast<int>(port_index)
                                  << " punched successfully for peer " << peer_id << std::endl;
                        
                        // Check if all ports are now punched
                        if (IsMultiPortSessionComplete(session)) {
                            session.success = true;
                            std::cout << "[MULTI-PORT PUNCH] SUCCESS! All ports punched for peer "
                                      << peer_id << std::endl;
                        }
                    }
                    break;
                }
            }
        }

        std::vector<MultiPortPunchSession> HolePunchManager::GetActiveMultiPortSessions() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<MultiPortPunchSession> active;
            auto now = std::chrono::steady_clock::now();

            for (const auto& session : m_multiPortSessions) {
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

        void HolePunchManager::CleanupMultiPortSessions() {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto now = std::chrono::steady_clock::now();

            m_multiPortSessions.erase(
                std::remove_if(m_multiPortSessions.begin(), m_multiPortSessions.end(),
                    [now](const MultiPortPunchSession& session) {
                        if (session.success) {
                            return true; // Remove completed sessions
                        }
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - session.start_time).count();
                        return elapsed >= PUNCH_TIMEOUT_MS; // Remove timed out sessions
                    }),
                m_multiPortSessions.end());
        }

        // =====================================================================
        // Automatic Strategy Selection Implementation
        // =====================================================================

        PunchAttemptTracker* HolePunchManager::FindOrCreateTracker(const std::string& peer_id) {
            // Check if tracker already exists
            for (auto& tracker : m_punchTrackers) {
                if (tracker.peer_id == peer_id) {
                    return &tracker;
                }
            }

            // Create new tracker
            PunchAttemptTracker tracker{};
            tracker.peer_id = peer_id;
            tracker.single_port_attempts = 0;
            tracker.multi_port_attempts = 0;
            tracker.single_port_failed = false;
            tracker.multi_port_failed = false;
            tracker.reported_single_failure = false;
            tracker.reported_multi_failure = false;
            tracker.last_attempt_time = std::chrono::steady_clock::now();
            tracker.start_time = std::chrono::steady_clock::now();
            tracker.current_phase = PunchAttemptTracker::Phase::SinglePort;

            m_punchTrackers.push_back(tracker);
            return &m_punchTrackers.back();
        }

        PunchAttemptTracker* HolePunchManager::FindTracker(const std::string& peer_id) const {
            for (const auto& tracker : m_punchTrackers) {
                if (tracker.peer_id == peer_id) {
                    return const_cast<PunchAttemptTracker*>(&tracker);
                }
            }
            return nullptr;
        }

        bool HolePunchManager::ShouldUseMultiPortPunch() const {
            // Check if any tracker has failed single-port and switched to multi-port
            for (const auto& tracker : m_punchTrackers) {
                if (tracker.single_port_failed && !tracker.multi_port_failed) {
                    return true;
                }
            }
            return false;
        }

        bool HolePunchManager::ShouldUseMultiPortPunch(const std::string& peer_id) const {
            // Check if the specific peer's tracker has failed single-port and switched to multi-port
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            for (const auto& tracker : m_punchTrackers) {
                if (tracker.peer_id == peer_id && tracker.single_port_failed && !tracker.multi_port_failed) {
                    return true;
                }
            }
            return false;
        }

        bool HolePunchManager::StartPunchAuto(const std::string& ip, int port,
                                               const std::string& peer_id,
                                               uint8_t suggested_port_range) {
            std::lock_guard<std::mutex> lock(m_trackerMutex);

            auto* tracker = FindOrCreateTracker(peer_id);
            
            // If we haven't tried single-port yet, start with that
            if (tracker->current_phase == PunchAttemptTracker::Phase::SinglePort) {
                // Start single-port punching first
                tracker->peer_ip = ip;
                tracker->peer_port = port;
                tracker->current_phase = PunchAttemptTracker::Phase::SinglePort;
                
                std::cout << "[AUTO STRATEGY] Starting single-port punch to " << ip << ":" << port
                          << " for peer " << peer_id << std::endl;
                
                // Also start the single-port punch session
                StartPunch(ip, port, peer_id);
                return true;
            }
            else if (tracker->current_phase == PunchAttemptTracker::Phase::SwitchingToMulti) {
                // Switch to multi-port punching
                uint8_t port_range = suggested_port_range > 0 ? suggested_port_range : DEFAULT_MULTI_PORT_RANGE;
                tracker->current_phase = PunchAttemptTracker::Phase::MultiPort;
                
                std::cout << "[AUTO STRATEGY] Switching to multi-port punch to " << ip << ":" << port
                          << " (range: " << static_cast<int>(port_range) << " ports) for peer " << peer_id << std::endl;
                
                // Start multi-port punch session
                StartMultiPortPunch(ip, port, port_range, peer_id);
                return true;
            }
            else if (tracker->current_phase == PunchAttemptTracker::Phase::MultiPort) {
                // Continue multi-port punching
                uint8_t port_range = suggested_port_range > 0 ? suggested_port_range : DEFAULT_MULTI_PORT_RANGE;
                StartMultiPortPunch(ip, port, port_range, peer_id);
                return true;
            }
            
            return false;
        }

        bool HolePunchManager::ShouldSendPunchAuto(const std::string& peer_id) const {
            // Check single-port session first
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& session : m_sessions) {
                    if (session.target_id == peer_id && !session.success) {
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
            }
            
            // Check multi-port session
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (const auto& session : m_multiPortSessions) {
                    if (session.target_id == peer_id && !session.success) {
                        auto now = std::chrono::steady_clock::now();
                        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - session.start_time).count();
                        
                        int expected_attempt = int(elapsed / PUNCH_INTERVAL_MS) + 1;
                        
                        if (expected_attempt > session.attempt_count &&
                            expected_attempt <= MAX_PUNCH_ATTEMPTS) {
                            return true;
                        }
                    }
                }
            }
            
            return false;
        }

        HolePunchManager::PunchStrategy HolePunchManager::GetPunchStrategy(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            auto* tracker = FindTracker(peer_id);
            
            if (!tracker) {
                return PunchStrategy::SinglePort; // Default to single-port
            }
            
            switch (tracker->current_phase) {
                case PunchAttemptTracker::Phase::SinglePort:
                    return PunchStrategy::SinglePort;
                case PunchAttemptTracker::Phase::SwitchingToMulti:
                case PunchAttemptTracker::Phase::MultiPort:
                    return PunchStrategy::MultiPort;
                case PunchAttemptTracker::Phase::Failed:
                    return PunchStrategy::SinglePort; // No strategy available
            }
            
            return PunchStrategy::SinglePort;
        }

        void HolePunchManager::RecordPunchSuccess(const std::string& peer_id, uint8_t port_index) {
            // Check if this is a multi-port success
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                for (auto& session : m_multiPortSessions) {
                    if (session.target_id == peer_id && !session.success) {
                        MarkMultiPortSuccess(peer_id, port_index);
                        return;
                    }
                }
            }
            
            // Otherwise, mark single-port success
            MarkSuccess(peer_id);
            
            // Update tracker
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            auto* tracker = FindTracker(peer_id);
            if (tracker) {
                tracker->current_phase = PunchAttemptTracker::Phase::Success; // Connection succeeded, stop tracking
                std::cout << "[AUTO STRATEGY] Punch succeeded for peer " << peer_id << std::endl;
            }
        }

        void HolePunchManager::ReportPunchFailure(const std::string& coordinator_ip, int coordinator_port,
                                                   const std::string& peer_id, const std::string& /*peer_ip*/, int /*peer_port*/,
                                                   uint8_t phase) {
            // Send failure report to the coordinator (STUN server / super node)
            // This will be handled by the PeerManager's packet handler
            
            std::cout << "[FAILURE REPORT] Reporting " << static_cast<int>(phase)
                      << " phase failure for peer " << peer_id
                      << " to coordinator " << coordinator_ip << ":" << coordinator_port << std::endl;
            
            // The actual packet sending will be handled by PeerManager
            // This method coordinates with the coordinator to track failures from both peers
        }

        bool HolePunchManager::ShouldSwitchToMultiPort(const std::string& peer_id) const {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            auto* tracker = FindTracker(peer_id);
            
            if (!tracker) {
                return false;
            }
            
            // Switch if single-port attempts exceeded max and not yet switched
            return (tracker->single_port_attempts >= MAX_SINGLE_PORT_ATTEMPTS &&
                    tracker->current_phase == PunchAttemptTracker::Phase::SinglePort);
        }

        void HolePunchManager::SwitchToMultiPortPunch(const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            auto* tracker = FindTracker(peer_id);
            
            if (!tracker) {
                return;
            }
            
            tracker->single_port_failed = true;
            tracker->current_phase = PunchAttemptTracker::Phase::SwitchingToMulti;
            
            std::cout << "[AUTO STRATEGY] Switching peer " << peer_id << " to multi-port punching" << std::endl;
        }

        std::vector<PunchAttemptTracker> HolePunchManager::GetFailedTrackers() const {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            std::vector<PunchAttemptTracker> failed;
            
            for (const auto& tracker : m_punchTrackers) {
                // Only return trackers where BOTH single-port AND multi-port have failed
                // (i.e., all methods exhausted, not just those that switched to multi-port)
                if (tracker.single_port_failed && tracker.multi_port_failed) {
                    failed.push_back(tracker);
                }
            }
            
            return failed;
        }

        // =====================================================================
        // STUN server coordination methods
        // =====================================================================

        bool HolePunchManager::RecordFailureReport(const std::string& peer_id, const std::string& peer_ip,
                                                    int peer_port, uint8_t phase) {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            
            FailureReport report;
            report.peer_id = peer_id;
            report.peer_ip = peer_ip;
            report.peer_port = peer_port;
            report.phase = phase;
            report.report_time = std::chrono::steady_clock::now();
            
            m_failureReports.push_back(report);
            
            std::cout << "[COORD] Recorded failure report from peer " << peer_id
                      << " for phase " << static_cast<int>(phase) << std::endl;
            
            // Check if both peers have failed this phase
            // We need to find if there are reports from two different peers for the same phase
            std::set<std::string> peersFailed;
            for (const auto& r : m_failureReports) {
                if (r.phase == phase) {
                    peersFailed.insert(r.peer_id);
                }
            }
            
            // Check if we have at least 2 different peers reporting failure for this phase
            if (peersFailed.size() >= 2) {
                std::cout << "[COORD] Both peers have failed phase " << static_cast<int>(phase)
                          << ". Initiating next phase coordination." << std::endl;
                return true;
            }
            
            return false;
        }

        bool HolePunchManager::HasPeerReportedFailure(const std::string& peer_id, uint8_t phase) const {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            
            for (const auto& report : m_failureReports) {
                if (report.peer_id == peer_id && report.phase == phase) {
                    return true;
                }
            }
            return false;
        }

        std::vector<std::string> HolePunchManager::GetSinglePortFailedPeers() const {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            std::vector<std::string> failed;
            
            for (const auto& report : m_failureReports) {
                if (report.phase == 0) { // Single-port failed
                    failed.push_back(report.peer_id);
                }
            }
            
            return failed;
        }

        std::vector<std::string> HolePunchManager::GetMultiPortFailedPeers() const {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            std::vector<std::string> failed;
            
            for (const auto& report : m_failureReports) {
                if (report.phase == 1) { // Multi-port failed
                    failed.push_back(report.peer_id);
                }
            }
            
            return failed;
        }

        bool HolePunchManager::BothPeersFailedSinglePort(const std::string& peer_a, const std::string& peer_b) const {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            
            bool aFailed = false;
            bool bFailed = false;
            
            for (const auto& report : m_failureReports) {
                if (report.phase == 0) { // Single-port
                    if (report.peer_id == peer_a) aFailed = true;
                    if (report.peer_id == peer_b) bFailed = true;
                }
            }
            
            return aFailed && bFailed;
        }

        bool HolePunchManager::BothPeersFailedMultiPort(const std::string& peer_a, const std::string& peer_b) const {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            
            bool aFailed = false;
            bool bFailed = false;
            
            for (const auto& report : m_failureReports) {
                if (report.phase == 1) { // Multi-port
                    if (report.peer_id == peer_a) aFailed = true;
                    if (report.peer_id == peer_b) bFailed = true;
                }
            }
            
            return aFailed && bFailed;
        }

        void HolePunchManager::ClearFailureReports(const std::string& peer_id) {
            std::lock_guard<std::mutex> lock(m_failureReportMutex);
            
            m_failureReports.erase(
                std::remove_if(m_failureReports.begin(), m_failureReports.end(),
                    [&peer_id](const FailureReport& report) {
                        return report.peer_id == peer_id;
                    }),
                m_failureReports.end());
        }

        // =====================================================================
        // Helper to update tracker after single-port attempt
        // =====================================================================

        void HolePunchManager::UpdateTrackerAfterAttempt(const std::string& peer_id, bool success) {
            std::lock_guard<std::mutex> lock(m_trackerMutex);
            auto* tracker = FindOrCreateTracker(peer_id);
            
            tracker->last_attempt_time = std::chrono::steady_clock::now();
            
            if (success) {
                // Connection succeeded, stop tracking
                tracker->current_phase = PunchAttemptTracker::Phase::Success;
                std::cout << "[AUTO STRATEGY] Punch succeeded for peer " << peer_id << std::endl;
            }
            else {
                // Increment attempt count based on current phase
                switch (tracker->current_phase) {
                    case PunchAttemptTracker::Phase::SinglePort:
                        tracker->single_port_attempts++;
                        std::cout << "[AUTO STRATEGY] Single-port attempt " << tracker->single_port_attempts
                                  << " for peer " << peer_id << std::endl;
                        
                        // Check if we should switch to multi-port
                        if (tracker->single_port_attempts >= MAX_SINGLE_PORT_ATTEMPTS) {
                            tracker->single_port_failed = true;
                            tracker->current_phase = PunchAttemptTracker::Phase::SwitchingToMulti;
                            std::cout << "[AUTO STRATEGY] Single-port failed for peer " << peer_id
                                      << ". Switching to multi-port mode." << std::endl;
                        }
                        break;
                        
                    case PunchAttemptTracker::Phase::SwitchingToMulti:
                        // Already switched, will be updated in multi-port path
                        break;
                        
                    case PunchAttemptTracker::Phase::MultiPort:
                        tracker->multi_port_attempts++;
                        std::cout << "[AUTO STRATEGY] Multi-port attempt " << tracker->multi_port_attempts
                                  << " for peer " << peer_id << std::endl;
                        
                        // Check if multi-port also failed
                        if (tracker->multi_port_attempts >= MAX_MULTI_PORT_ATTEMPTS) {
                            tracker->multi_port_failed = true;
                            tracker->current_phase = PunchAttemptTracker::Phase::Failed;
                            std::cerr << "[AUTO STRATEGY] Multi-port also failed for peer " << peer_id
                                      << ". All hole punching methods exhausted." << std::endl;
                        }
                        break;
                        
                    case PunchAttemptTracker::Phase::Failed:
                        // Already failed, do nothing
                        break;
                }
            }
        }

    }
}
