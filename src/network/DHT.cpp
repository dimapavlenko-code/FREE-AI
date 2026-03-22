#include "network/DHT.hpp"
#include "network/PacketSecurity.hpp"
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <mbedtls/sha1.h>

namespace FreeAI {
    namespace Network {

        // XOR Distance Calculation
        uint64_t XORDistance(const uint8_t* id1, const uint8_t* id2) {
            uint64_t distance = 0;
            for (int i = 0; i < DHT_NODE_ID_SIZE; ++i) {
                distance |= (uint64_t)(id1[i] ^ id2[i]);
            }
            return distance;
        }

        // KBucket Implementation
        KBucket::KBucket() {}

        bool KBucket::AddNode(const KBucketEntry& entry) {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            // Check if already exists
            for (auto& e : m_entries) {
                if (memcmp(e.node_id, entry.node_id, DHT_NODE_ID_SIZE) == 0) {
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

        void KBucket::UpdateNode(const uint8_t* node_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto& e : m_entries) {
                if (memcmp(e.node_id, node_id, DHT_NODE_ID_SIZE) == 0) {
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
        DHTRoutingTable::DHTRoutingTable() : m_initialized(false) {
            memset(m_local_node_id, 0, DHT_NODE_ID_SIZE);
        }

        DHTRoutingTable::DHTRoutingTable(const uint8_t* local_node_id) : m_initialized(true) {
            memcpy(m_local_node_id, local_node_id, DHT_NODE_ID_SIZE);
        }

        void DHTRoutingTable::Initialize(const uint8_t* local_node_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            memcpy(m_local_node_id, local_node_id, DHT_NODE_ID_SIZE);
            m_initialized = true;
            // Note: We don't clear buckets - they can be reused
        }

        int DHTRoutingTable::GetBucketIndex(const uint8_t* node_id) const {
            if (!m_initialized) return 0;  // Safety check
            for (int i = 0; i < 160; ++i) {
                int byte_idx = i / 8;
                int bit_idx = 7 - (i % 8);
                if (((m_local_node_id[byte_idx] ^ node_id[byte_idx]) >> bit_idx) & 1) {
                    return i;
                }
            }
            return 159;
        }


        void DHTRoutingTable::AddNode(const uint8_t* node_id, const std::string& ip, int port) {
            std::lock_guard<std::mutex> lock(m_mutex);
            int bucket_idx = GetBucketIndex(node_id);
            
            KBucketEntry entry;
            memcpy(entry.node_id, node_id, DHT_NODE_ID_SIZE);
            entry.ip = ip;
            entry.port = port;
            entry.last_seen = static_cast<uint32_t>(std::time(nullptr));
            entry.responsive = true;

            m_buckets[bucket_idx].AddNode(entry);
        }

        void DHTRoutingTable::UpdateNode(const uint8_t* node_id) {
            std::lock_guard<std::mutex> lock(m_mutex);
            int bucket_idx = GetBucketIndex(node_id);
            m_buckets[bucket_idx].UpdateNode(node_id);
        }

        std::vector<KBucketEntry> DHTRoutingTable::GetClosestNodes(
            const uint8_t* target_id, size_t count) const 
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            std::vector<KBucketEntry> all_nodes;

            for (const auto& bucket : m_buckets) {
                auto nodes = bucket.GetNodes();
                all_nodes.insert(all_nodes.end(), nodes.begin(), nodes.end());
            }

            // Sort by XOR distance to target
            std::sort(all_nodes.begin(), all_nodes.end(),
                [target_id](const KBucketEntry& a, const KBucketEntry& b) {
                    return XORDistance(a.node_id, target_id) < 
                           XORDistance(b.node_id, target_id);
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
            memset(m_local_node_id, 0, DHT_NODE_ID_SIZE);
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
            mbedtls_sha1_context ctx;
            mbedtls_sha1_init(&ctx);
            mbedtls_sha1_starts(&ctx);
            mbedtls_sha1_update(&ctx,
                reinterpret_cast<const unsigned char*>(pubkey_pem.c_str()),
                pubkey_pem.size());
            mbedtls_sha1_finish(&ctx, m_local_node_id);
            mbedtls_sha1_free(&ctx);

            // FIXED: Use Initialize() instead of assignment
            m_routingTable.Initialize(m_local_node_id);

            std::cout << "[DHT] Initialized with Node ID: ";
            for (int i = 0; i < 8; ++i) {
                printf("%02x", static_cast<unsigned int>(m_local_node_id[i]));  
            }
            std::cout << std::endl;

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
                    std::cout << "[DHT] Routing table has " << m_routingTable.TotalNodes()
                        << " nodes" << std::endl;
                }
            }
        }

        std::vector<KBucketEntry> DHT::FindNodes(const uint8_t* target_id) {
            return m_routingTable.GetClosestNodes(target_id, DHT_K_BUCKET_SIZE);
        }

        bool DHT::StoreValue(const uint8_t* key, const void* value, size_t size) {
            // Find closest nodes to key
            auto nodes = m_routingTable.GetClosestNodes(key, DHT_ALPHA);
            
            for (const auto& node : nodes) {
                DHTStorePayload payload;
                memset(&payload, 0, sizeof(payload));
                memcpy(payload.key, key, DHT_NODE_ID_SIZE);
                payload.value_size = static_cast<uint16_t>(size);

                std::vector<uint8_t> packet(sizeof(DHTStorePayload) + size);
                memcpy(packet.data(), &payload, sizeof(DHTStorePayload));
                memcpy(packet.data() + sizeof(DHTStorePayload), value, size);

                SendDHTPacket(m_socket, node.ip, node.port, PT_DHT_STORE,
                             packet.data(), packet.size());
            }

            return !nodes.empty();
        }

        std::vector<uint8_t> DHT::GetValue(const uint8_t* key) {
            // Find closest nodes to key
            auto nodes = m_routingTable.GetClosestNodes(key, DHT_ALPHA);
            
            // Request value from closest node
            if (!nodes.empty()) {
                DHTFindNodePayload payload;
                memset(&payload, 0, sizeof(payload));
                memcpy(payload.target_id, key, DHT_NODE_ID_SIZE);

                SendDHTPacket(m_socket, nodes[0].ip, nodes[0].port, PT_DHT_GET_VALUE,
                             &payload, sizeof(payload));
            }

            // In production, would wait for response
            return std::vector<uint8_t>();
        }

        size_t DHT::GetNodeCount() const {
            return m_routingTable.TotalNodes();
        }

        void DHT::SendDHTPacket(UDPSocket* socket, const std::string& ip, int port,
            uint8_t type, const void* payload, size_t size) {
            if (!socket || !m_socket) {
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
                socket->SendTo(packet.data(), packet.size(), ip, port);
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

            // Note: DHT packets may not have signature verification
            // since we're just learning about nodes
            if (!PacketSecurity::ProcessIncoming(data, size, header, payload, nullptr, "")) {
                return;
            }

            if (header.type == PT_DHT_FIND_NODE_RESPONSE) {
                if (payload.size() >= sizeof(DHTFindNodeResponsePayload)) {
                    const DHTFindNodeResponsePayload* resp =
                        reinterpret_cast<const DHTFindNodeResponsePayload*>(payload.data());

                    const uint8_t* ptr = payload.data() + sizeof(DHTFindNodeResponsePayload);

                    for (int i = 0; i < resp->node_count; ++i) {
                        const DHTNodeInfo* nodeInfo = reinterpret_cast<const DHTNodeInfo*>(
                            ptr + i * sizeof(DHTNodeInfo));

                        AddNode((const uint8_t*)nodeInfo->node_id, nodeInfo->ip, nodeInfo->port);

                        std::cout << "[DHT] Learned about node: ";
                        for (int j = 0; j < 8; ++j) {
                            printf("%02x", static_cast<unsigned int>(nodeInfo->node_id[j]));  
                        }
                        std::cout << " @ " << nodeInfo->ip << ":" << nodeInfo->port << std::endl;
                    }
                }
            }
        }

    }
}
