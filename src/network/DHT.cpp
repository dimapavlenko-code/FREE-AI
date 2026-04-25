#include "network/DHT.hpp"
#include "network/PacketSecurity.hpp"
#include "utils/Helpers.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <chrono>
#include <mbedtls/sha1.h>
#include <random>
#include <sstream>

namespace FreeAI {
    namespace Network {

        // =====================================================================
        // TTLValueStore Implementation
        // =====================================================================

        void TTLValueStore::Store(const std::string& key, const std::vector<uint8_t>& value, uint32_t ttl) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_store[key] = StoredValue(value, GetCurrentTime() + ttl);
        }

        std::vector<uint8_t> TTLValueStore::Get(const std::string& key) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_store.find(key);
            if (it != m_store.end()) {
                if (IsExpired(it->second.expires_at)) {
                    m_store.erase(it);
                    return {};
                }
                return it->second.data;
            }
            return {};
        }

        bool TTLValueStore::Exists(const std::string& key) const {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_store.find(key);
            if (it != m_store.end()) {
                return !IsExpired(it->second.expires_at);
            }
            return false;
        }

        bool TTLValueStore::Remove(const std::string& key) {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_store.erase(key) > 0;
        }

        std::vector<std::string> TTLValueStore::GetKeys() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<std::string> keys;
            for (const auto& [key, value] : m_store) {
                if (!IsExpired(value.expires_at)) {
                    keys.push_back(key);
                }
            }
            return keys;
        }

        size_t TTLValueStore::CleanupExpired() {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto initialSize = m_store.size();
            auto currentTime = GetCurrentTime();
            for (auto it = m_store.begin(); it != m_store.end(); ) {
                if (it->second.expires_at <= currentTime) {
                    it = m_store.erase(it);
                } else {
                    ++it;
                }
            }
            return initialSize - m_store.size();
        }

        size_t TTLValueStore::Size() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_store.size();
        }

        size_t TTLValueStore::GetActiveSize() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return std::count_if(m_store.begin(), m_store.end(),
                [this](const auto& entry) { return !IsExpired(entry.second.expires_at); });
        }

        bool TTLValueStore::IsExpired(uint32_t expires_at) const {
            return GetCurrentTime() > expires_at;
        }

        uint32_t TTLValueStore::GetCurrentTime() const {
            return static_cast<uint32_t>(std::time(nullptr));
        }

        // XOR Distance Calculation
        // XOR all bytes of both 160-bit node IDs and return the result as a 160-bit array.
        std::array<uint8_t, DHT_NODE_ID_SIZE> XORDistance(const uint8_t* id1, const uint8_t* id2) {
            std::array<uint8_t, DHT_NODE_ID_SIZE> distance;
            for (size_t i = 0; i < DHT_NODE_ID_SIZE; ++i) {
                distance[i] = id1[i] ^ id2[i];
            }
            return distance;
        }

        std::array<uint8_t, DHT_NODE_ID_SIZE> XORDistance(const NodeId& id1, const NodeId& id2) {
            return XORDistance(id1.Data(), id2.Data());
        }

        // Helper to compare two XOR distances (lexicographical comparison for 160-bit values)
        bool CompareXORDistance(const std::array<uint8_t, DHT_NODE_ID_SIZE>& a, const std::array<uint8_t, DHT_NODE_ID_SIZE>& b) {
            for (size_t i = 0; i < DHT_NODE_ID_SIZE; ++i) {
                if (a[i] != b[i]) {
                    return a[i] < b[i];
                }
            }
            return false; // Equal
        }

        // KBucket Implementation
        KBucket::KBucket() {}

        bool KBucket::AddNode(const KBucketEntry& entry) {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Check if already exists
            for (auto& e : m_entries) {
                if (e.node_id == entry.node_id) {
                    e.last_seen = entry.last_seen;
                    e.responsive = true;
                    return true;
                }
            }

            // Add if bucket not full
            if (m_entries.size() < DHT_K_BUCKET_SIZE) {
                m_entries.push_back(entry);
                return true;
            }

            // Bucket full - could implement eviction policy here
            return false;
        }

        void KBucket::UpdateNode(const NodeId& node_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& e : m_entries) {
                if (e.node_id == node_id) {
                    e.last_seen = static_cast<uint32_t>(std::time(nullptr));
                    e.responsive = true;
                    break;
                }
            }
        }

        std::vector<KBucketEntry> KBucket::GetNodes() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_entries;
        }

        size_t KBucket::Size() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_entries.size();
        }

        // DHTRoutingTable Implementation
        DHTRoutingTable::DHTRoutingTable() {
        }

        DHTRoutingTable::DHTRoutingTable(const NodeId& local_node_id) : m_local_node_id(local_node_id), m_initialized(true) {
        }

        void DHTRoutingTable::Initialize(const NodeId& local_node_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_local_node_id = local_node_id;
            m_initialized = true;
        }

        int DHTRoutingTable::GetBucketIndex(const NodeId& node_id) const {
            if (!m_initialized) return 0;  // Safety check
            // Find the first bit position where local_node_id and node_id differ
            // This determines which k-bucket the node belongs to in Kademlia
            for (int i = 0; i < 160; ++i) {
                int byte_idx = i / 8;
                int bit_idx = 7 - (i % 8);
                auto d1 = m_local_node_id.Data();
                auto d2 = node_id.Data();
                if (((d1[byte_idx] ^ d2[byte_idx]) >> bit_idx) & 1) {
                    // Found the first differing bit
                    return i;
                }
            }
            // All bits match - use the last bucket
            return 159;
        }


        void DHTRoutingTable::AddNode(const NodeId& node_id, const std::string& ip, int port) {
            // Get bucket index without locking (GetBucketIndex is const and doesn't lock)
            int bucket_idx = GetBucketIndex(node_id);
            
            KBucketEntry entry;
            entry.node_id = node_id;
            entry.ip = ip;
            entry.port = port;
            entry.last_seen = static_cast<uint32_t>(std::time(nullptr));
            entry.responsive = true;

            // Delegate locking to KBucket
            m_buckets[bucket_idx].AddNode(entry);
        }

        void DHTRoutingTable::UpdateNode(const NodeId& node_id) {
            int bucket_idx = GetBucketIndex(node_id);
            m_buckets[bucket_idx].UpdateNode(node_id);
        }

        std::vector<KBucketEntry> DHTRoutingTable::GetClosestNodes(
            const NodeId& target_id, size_t count) const
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<KBucketEntry> all_nodes;

            for (const auto& bucket : m_buckets) {
                auto nodes = bucket.GetNodes();
                all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end());
            }

            // Sort by XOR distance to target (ascending - closest first)
            std::sort(all_nodes.begin(), all_nodes.end(),
                [target_id](const KBucketEntry& a, const KBucketEntry& b) {
                    return CompareXORDistance(XORDistance(a.node_id, target_id),
                                             XORDistance(b.node_id, target_id));
                });

            if (all_nodes.size() > count) {
                all_nodes.resize(count);
            }

            return all_nodes;
        }

        std::vector<KBucketEntry> DHTRoutingTable::GetAllNodes() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<KBucketEntry> all_nodes;

            for (const auto& bucket : m_buckets) {
                auto nodes = bucket.GetNodes();
                all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end());
            }

            return all_nodes;
        }

        size_t DHTRoutingTable::TotalNodes() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            size_t total = 0;
            for (const auto& bucket : m_buckets) {
                total += bucket.Size();
            }
            return total;
        }

        // DHT Implementation
        DHT::DHT() : m_identity(nullptr), m_socket(nullptr), m_running(false) {            
        }

        DHT::~DHT() {
            Stop();
        }

        bool DHT::Initialize(const Crypto::Identity* identity) {
            if (!identity || !identity->IsValid()) {
                return false;
            }

            m_identity = identity;

            
            // Generate Node ID = SHA1 hash of public key
            std::string pubkey_pem = identity->GetPublicKeyPEM();
            m_local_node_id.FromPubkey(pubkey_pem);

            // FIXED: Use Initialize() instead of assignment
            m_routingTable.Initialize(m_local_node_id);

            std::cout << "[DHT] Initialized with Node ID: " << m_local_node_id.ToString(16) << std::endl;

            return true;
        }

        void DHT::Start(UDPSocket* socket) {
            m_socket = socket;
            m_running = true;
            m_refreshThread = std::thread(&DHT::RefreshLoop, this);
        }

        void DHT::Stop() {
            m_running = false;
            if (m_refreshThread.joinable()) {
                m_refreshThread.join();
            }
        }

        void DHT::RefreshLoop() {
            while (m_running) {
                // Periodic bucket refresh
                //std::this_thread::sleep_for(std::chrono::seconds(DHT_REFRESH_INTERVAL)); // Qwen, this deadlocks the application at exit, 900 seconds is very long time even for humans, so better to implement handlers in the next way
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                auto curT = (uint32_t)time(nullptr);
                if (curT > m_lastDhtRefreshTs + 900) {
                    m_lastDhtRefreshTs = curT;
                    // Could implement random node lookup here to refresh buckets
                    std::cout << "[DHT] Routing table has " << m_routingTable.TotalNodes() << " nodes" << std::endl;
                }
            }
        }

        std::vector<KBucketEntry> DHT::FindNodes(const NodeId& target_id) {
            // If we have no nodes, try to find bootstrap nodes
            if (m_routingTable.TotalNodes() == 0) {
                std::cout << "[DHT] No nodes in routing table, cannot find nodes" << std::endl;
                return {};
            }
            return m_routingTable.GetClosestNodes(target_id, DHT_K_BUCKET_SIZE);
        }

        bool DHT::StoreValue(const NodeId& key, const void* value, size_t size) {
            // Find closest nodes to key (Kademlia: store on K closest nodes)
            auto nodes = m_routingTable.GetClosestNodes(key, DHT_K_BUCKET_SIZE);
            
            if (nodes.empty()) {
                std::cout << "[DHT] No nodes to store value" << std::endl;
                return false;
            }

            bool stored = false;
            for (const auto& node : nodes) {
                DHTStorePayload payload;
                memset(&payload, 0, sizeof(payload));
                memcpy(payload.key, key.Data(), DHT_NODE_ID_SIZE);
                payload.value_size = static_cast<uint16_t>(std::min(size, static_cast<size_t>(65535)));

                std::vector<uint8_t> packet(sizeof(DHTStorePayload) + size);
                memcpy(packet.data(), &payload, sizeof(DHTStorePayload));
                memcpy(packet.data() + sizeof(DHTStorePayload), value, size);

                SendDHTPacket(m_socket, node.ip, node.port, PT_DHT_STORE,
                             packet.data(), packet.size());
                stored = true;
            }

            return stored;
        }

        std::vector<uint8_t> DHT::GetValue(const NodeId& key) {
            // First check local store (TTLValueStore handles expiration internally)
            std::string keyStr(reinterpret_cast<const char*>(key.Data()), DHT_NODE_ID_SIZE);
            auto value = m_localStore.Get(keyStr);
            if (!value.empty()) {
                std::cout << "[DHT] Found value locally for key: " << key.ToString(16) << std::endl;
                return value;
            }

            // Find closest nodes to key
            auto nodes = m_routingTable.GetClosestNodes(key, DHT_ALPHA);
            
            if (nodes.empty()) {
                std::cout << "[DHT] No nodes to request value" << std::endl;
                return {};
            }

            // Request value from closest nodes
            DHTFindNodePayload payload;
            memset(&payload, 0, sizeof(payload));
            memcpy(payload.target_id, key.Data(), DHT_NODE_ID_SIZE);

            for (const auto& node : nodes) {
                SendDHTPacket(m_socket, node.ip, node.port, PT_DHT_GET_VALUE,
                             &payload, sizeof(payload));
            }

            // In production, would wait for response with timeout
            // For now, return empty (value will be populated via ProcessIncoming)
            return {};
        }

        size_t DHT::GetNodeCount() const {
            return m_routingTable.TotalNodes();
        }

        void DHT::SendDHTPacket(UDPSocket* socket, const std::string& ip, int port,
            uint8_t type, const void* payload, size_t size) {
            // Use the provided socket, fall back to m_socket if null
            UDPSocket* targetSocket = socket ? socket : m_socket;
            if (!targetSocket) {
                return;
            }

            // DHT packets don't need encryption (public routing info)
            // But we still sign them for authenticity
            bool sign = (m_identity != nullptr) && m_identity->IsValid();
            bool encrypt = false; // DHT data is public

            // Use PacketSecurity for consistent packet format
            std::vector<uint8_t> packet = PacketSecurity::PrepareOutgoing(
                type, payload, size, sign, encrypt, m_identity);

            if (!packet.empty()) {
                targetSocket->SendTo(packet.data(), packet.size(), ip, port);
            }
        }

        void DHT::ProcessIncoming(const uint8_t* data, size_t size,
            const std::string& ip, int port) {
            // Parse DHT packet header
            if (size < sizeof(SecurePacketHeader)) {
                return;
            }

            SecurePacketHeader header;
            std::vector<uint8_t> payload;

            // Process the secure packet
            if (!PacketSecurity::ProcessIncoming(data, size, header, payload, nullptr, "")) {
                return;
            }

            switch (header.type) {
                case PT_DHT_FIND_NODE: {
                    // Handle incoming FIND_NODE request
                    if (payload.size() >= sizeof(DHTFindNodePayload)) {
                        const DHTFindNodePayload* findPayload =
                            reinterpret_cast<const DHTFindNodePayload*>(payload.data());
                        HandleFindNodeRequest(findPayload, ip, port);
                    }
                    break;
                }

                case PT_DHT_FIND_NODE_RESPONSE: {
                    // Handle FIND_NODE response
                    if (payload.size() >= sizeof(DHTFindNodeResponsePayload)) {
                        const DHTFindNodeResponsePayload* resp =
                            reinterpret_cast<const DHTFindNodeResponsePayload*>(payload.data());

                        const uint8_t* ptr = payload.data() + sizeof(DHTFindNodeResponsePayload);

                        for (int i = 0; i < resp->node_count; ++i) {
                            const DHTNodeInfo* nodeInfo = reinterpret_cast<const DHTNodeInfo*>(
                                ptr + i * sizeof(DHTNodeInfo));

                            // Construct NodeId from std::array
                            std::array<uint8_t, DHT_NODE_ID_SIZE> nodeIdBytes{};
                            memcpy(nodeIdBytes.data(), nodeInfo->node_id, DHT_NODE_ID_SIZE);
                            NodeId nid(nodeIdBytes);
                            AddNode(nid, nodeInfo->ip, nodeInfo->port);

                            std::cout << "[DHT] Learned about node: " << nid.ToString(16) << " @ " << nodeInfo->ip << ":" << nodeInfo->port << std::endl;
                        }

                        // Check if there's a pending query callback
                        ProcessPendingQueries();
                    }
                    break;
                }

                case PT_DHT_GET_VALUE: {
                    // Handle GET_VALUE request - look up the key in our local store
                    if (payload.size() >= sizeof(DHTFindNodePayload)) {
                        const DHTFindNodePayload* getValuePayload =
                            reinterpret_cast<const DHTFindNodePayload*>(payload.data());
                        std::array<uint8_t, DHT_NODE_ID_SIZE> keyBytes{};
                        memcpy(keyBytes.data(), getValuePayload->target_id, DHT_NODE_ID_SIZE);
                        HandleGetValueRequest(NodeId(keyBytes), ip, port);
                    }
                    break;
                }

                case PT_DHT_GET_VALUE_RESPONSE: {
                    // Handle GET_VALUE response
                    // Store in pending values for the requester
                    std::cout << "[DHT] Received GET_VALUE_RESPONSE from " << ip << ":" << port << std::endl;
                    break;
                }

                case PT_DHT_PING: {
                    // Handle PING request
                    HandlePing(ip, port);
                    break;
                }

                case PT_DHT_STORE: {
                    // Handle STORE request - store the value locally
                    if (payload.size() >= sizeof(DHTStorePayload)) {
                        const DHTStorePayload* storePayload =
                            reinterpret_cast<const DHTStorePayload*>(payload.data());

                        std::array<uint8_t, DHT_NODE_ID_SIZE> keyBytes{};
                        memcpy(keyBytes.data(), storePayload->key, DHT_NODE_ID_SIZE);
                        NodeId key(keyBytes);

                        // Store the value locally using TTLValueStore
                        const uint8_t* valueData = payload.data() + sizeof(DHTStorePayload);
                        size_t valueSize = payload.size() - sizeof(DHTStorePayload);
                        std::string keyStr(reinterpret_cast<const char*>(key.Data()), DHT_NODE_ID_SIZE);
                        
                        m_localStore.Store(keyStr, std::vector<uint8_t>(valueData, valueData + valueSize), 3600);

                        std::cout << "[DHT] Stored value for key: " << key.ToString(16) << " (" << valueSize << " bytes)" << std::endl;
                    }
                    break;
                }

                default:
                    std::cout << "[DHT] Unknown packet type: " << static_cast<int>(header.type) << std::endl;
                    break;
            }
        }

        void DHT::HandleFindNodeRequest(const DHTFindNodePayload* payload,
            const std::string& ip, int port) {
            std::cout << "[DHT] FIND_NODE request for: "
                      << std::string(reinterpret_cast<const char*>(payload->target_id), DHT_NODE_ID_SIZE).substr(0, 16) << " from " << ip << ":" << port << std::endl;

            // Find closest nodes to the target
            std::array<uint8_t, DHT_NODE_ID_SIZE> targetBytes{};
            memcpy(targetBytes.data(), payload->target_id, DHT_NODE_ID_SIZE);
            NodeId targetId(targetBytes);
            
            auto closestNodes = m_routingTable.GetClosestNodes(targetId, DHT_K_BUCKET_SIZE);

            // Build response
            DHTFindNodeResponsePayload resp;
            memset(&resp, 0, sizeof(resp));
            resp.node_count = static_cast<uint8_t>(std::min(closestNodes.size(), static_cast<size_t>(DHT_K_BUCKET_SIZE)));

            std::vector<uint8_t> packet(sizeof(DHTFindNodeResponsePayload) + resp.node_count * sizeof(DHTNodeInfo));
            memcpy(packet.data(), &resp, sizeof(DHTFindNodeResponsePayload));

            uint8_t* nodeData = packet.data() + sizeof(DHTFindNodeResponsePayload);
            for (size_t i = 0; i < closestNodes.size(); ++i) {
                DHTNodeInfo* nodeInfo = reinterpret_cast<DHTNodeInfo*>(nodeData + i * sizeof(DHTNodeInfo));
                memcpy(nodeInfo->node_id, closestNodes[i].node_id.Data(), DHT_NODE_ID_SIZE);
                fai_strncpy(nodeInfo->ip, closestNodes[i].ip, sizeof(nodeInfo->ip));
                nodeInfo->port = static_cast<uint16_t>(closestNodes[i].port);
                nodeInfo->last_seen = closestNodes[i].last_seen;
                nodeInfo->reserved[0] = 0;
                nodeInfo->reserved[1] = 0;
            }

            // Send response back
            SendDHTPacket(m_socket, ip, port, PT_DHT_FIND_NODE_RESPONSE,
                         packet.data(), packet.size());
        }

        void DHT::HandlePing(const std::string& ip, int port) {
            std::cout << "[DHT] PING from " << ip << ":" << port << std::endl;
            // A PING response is essentially an acknowledgment
            // We could send back a PONG, but for simplicity we just log it
        }

        void DHT::HandleGetValueRequest(const NodeId& key, const std::string& ip, int port) {
            std::cout << "[DHT] GET_VALUE request for: " << key.ToString(16) << " from " << ip << ":" << port << std::endl;

            // Look up the key in our local store (TTLValueStore handles expiration)
            std::string keyStr(reinterpret_cast<const char*>(key.Data()), DHT_NODE_ID_SIZE);
            auto valueData = m_localStore.Get(keyStr);

            if (!valueData.empty()) {
                // Build GET_VALUE_RESPONSE
                std::vector<uint8_t> packet(sizeof(uint8_t) + valueData.size());
                packet[0] = static_cast<uint8_t>(valueData.size());
                memcpy(packet.data() + 1, valueData.data(), valueData.size());

                SendDHTPacket(m_socket, ip, port, PT_DHT_GET_VALUE_RESPONSE,
                             packet.data(), packet.size());
            } else {
                std::cout << "[DHT] Key not found: " << key.ToString(16) << std::endl;
            }
        }

        void DHT::ProcessPendingQueries() {
            std::lock_guard<std::mutex> lock(m_pendingMutex);
            auto now = static_cast<uint32_t>(std::time(nullptr));

            for (auto& [id, query] : m_pendingQueries) {
                if (!query.completed && (now - query.created_at > 5)) {
                    // Query timed out (5 seconds)
                    query.completed = true;
                    auto it = m_queryCallbacks.find(id);
                    if (it != m_queryCallbacks.end()) {
                        it->second(query.response);
                        m_queryCallbacks.erase(it);
                    }
                }
            }
        }

        std::string DHT::GenerateQueryId() {
            static std::mt19937 gen(std::random_device{}());
            static std::uniform_int_distribution<uint32_t> dist;
            std::ostringstream oss;
            oss << std::hex << dist(gen) << std::time(nullptr);
            return oss.str();
        }

        // ============================== struct NodeId ========================

        // Fill node_id with SHA1 of pubkey
        bool NodeId::FromPubkey(const std::string& pubkeyPEM) {
            auto cleanKey = Trim(pubkeyPEM);
            if (cleanKey.empty()) {
                return false;
            }

            mbedtls_sha1_context ctx;
            mbedtls_sha1_init(&ctx);

            int ret = mbedtls_sha1_starts(&ctx);
            if (ret != 0) {
                mbedtls_sha1_free(&ctx);
                return false;
            }

            ret = mbedtls_sha1_update(&ctx,
                (const unsigned char*) cleanKey.c_str(),
                cleanKey.size());
            if (ret != 0) {
                mbedtls_sha1_free(&ctx);
                return false;
            }

            ret = mbedtls_sha1_finish(&ctx, node_id);
            mbedtls_sha1_free(&ctx);

            return (ret == 0);
        }

        // Visualize as hex string, cropped to first char_cnt characters
        std::string NodeId::ToString(int char_cnt) const {
            // Limit to valid range
            if (char_cnt <= 0) {
                return "";
            }

            // Max hex chars = 2 per byte (20 bytes = 40 hex chars)
            int max_chars = static_cast<int>(DHT_NODE_ID_SIZE * 2);
            if (char_cnt > max_chars) {
                char_cnt = max_chars;
            }

            std::string result;
            result.reserve(char_cnt);

            for (int i = 0; i < DHT_NODE_ID_SIZE; ++i) {
                if (static_cast<int>(result.size()) >= char_cnt) {
                    break;
                }

                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned int>(node_id[i]));

                // Add 1 or 2 chars depending on remaining space
                for (int j = 0; j < 2 && static_cast<int>(result.size()) < char_cnt; ++j) {
                    result += hex[j];
                }
            }

            return result;
        }

        // Write to character array (null-terminated)
        // outBuf must be at least (char_cnt + 1) bytes for null terminator
        void NodeId::ToChar(char* outBuf, int char_cnt) const {
            if (outBuf == nullptr || char_cnt <= 0) {
                if (outBuf != nullptr) {
                    outBuf[0] = '\0';
                }
                return;
            }

            // Max hex chars = 2 per byte (20 bytes = 40 hex chars)
            int max_chars = static_cast<int>(DHT_NODE_ID_SIZE * 2);
            if (char_cnt > max_chars) {
                char_cnt = max_chars;
            }

            int buf_idx = 0;
            for (int i = 0; i < DHT_NODE_ID_SIZE && buf_idx < char_cnt; ++i) {
                char hex[3];
                snprintf(hex, sizeof(hex), "%02x", static_cast<unsigned int>(node_id[i]));

                for (int j = 0; j < 2 && buf_idx < char_cnt; ++j) {
                    outBuf[buf_idx++] = hex[j];
                }
            }

            // Null-terminate
            outBuf[buf_idx] = '\0';
        }

}
}


