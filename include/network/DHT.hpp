#pragma once
#include "network/Protocol.hpp"
#include "network/UDPSocket.hpp"
#include "crypto/Identity.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>
#include <array>
#include <functional>

namespace FreeAI {
    namespace Network {

        // Helper for manipulations with network IDs
        struct NodeId {
            uint8_t node_id[DHT_NODE_ID_SIZE];

            // Constructor (zero-initialize)
            NodeId() {
                memset(node_id, 0, DHT_NODE_ID_SIZE);
            }

            // Copy constructor
            NodeId(const NodeId& other) {
                memcpy(node_id, other.node_id, DHT_NODE_ID_SIZE);
            }

            // Construct from std::array (avoids ambiguity with default constructor)
            explicit NodeId(std::array<uint8_t, DHT_NODE_ID_SIZE> data) noexcept
                : node_id{} {
                memcpy(node_id, data.data(), DHT_NODE_ID_SIZE);
            }

            // Construct from raw uint8_t* pointer
            NodeId(const uint8_t* data) noexcept
                : node_id{} {
                if (data) {
                    memcpy(node_id, data, DHT_NODE_ID_SIZE);
                }
            }

            // Assignment operator
            NodeId& operator=(const NodeId& other) {
                if (this != &other) {
                    memcpy(node_id, other.node_id, DHT_NODE_ID_SIZE);
                }
                return *this;
            }

            // Equality operator
            bool operator==(const NodeId& other) const {
                return memcmp(node_id, other.node_id, DHT_NODE_ID_SIZE) == 0;
            }

            // Inequality operator
            bool operator!=(const NodeId& other) const {
                return !(*this == other);
            }

            // Less-than operator for sorting (lexicographic comparison)
            bool operator<(const NodeId& other) const {
                return memcmp(node_id, other.node_id, DHT_NODE_ID_SIZE) < 0;
            }

            // Fill node_id with SHA1 of pubkey
            bool FromPubkey(const std::string& pubkeyPEM);

            // Visualize as hex string, cropped to first char_cnt characters
            std::string ToString(int char_cnt) const;

            // Write to character array (null-terminated)
            // outBuf must be at least (char_cnt + 1) bytes for null terminator
            void ToChar(char* outBuf, int char_cnt) const;

            // Check if ID is valid (not all zeros)
            bool IsValid() const {
                for (size_t i = 0; i < DHT_NODE_ID_SIZE; ++i) {
                    if (node_id[i] != 0) {
                        return true;
                    }
                }
                return false;
            }

            // Clear the ID (set to all zeros)
            void Clear() {
                memset(node_id, 0, DHT_NODE_ID_SIZE);
            }

            // Get const pointer to node_id data
            const uint8_t* Data() const {
                return node_id;
            }

            // Get mutable pointer to node_id data (use with caution)
            uint8_t* Data() {
                return node_id;
            }
        };

        // Hash function for NodeId (to use in unordered_map)
        struct NodeIdHash {
            std::size_t operator()(const NodeId& id) const {
                // Simple hash: XOR first 8 bytes with shifts
                std::size_t h = 0;
                for (int i = 0; i < 8; ++i) {
                    h ^= static_cast<std::size_t>(id.node_id[i]) << (i * 8);
                }
                return h;
            }
        };

        // XOR distance between two node IDs
                // Returns the raw XOR result as a 160-bit value
                std::array<uint8_t, DHT_NODE_ID_SIZE> XORDistance(const uint8_t* id1, const uint8_t* id2);
                std::array<uint8_t, DHT_NODE_ID_SIZE> XORDistance(const NodeId& id1, const NodeId& id2);

                // Helper to compare two XOR distances (lexicographical for 160-bit int)
                bool CompareXORDistance(const std::array<uint8_t, DHT_NODE_ID_SIZE>& a, const std::array<uint8_t, DHT_NODE_ID_SIZE>& b);

        // K-Bucket entry
        struct KBucketEntry {
            NodeId node_id;
            std::string ip;
            int port;
            uint32_t last_seen;
            bool responsive;
        };

        // K-Bucket (stores nodes at similar XOR distance)
        class KBucket {
        public:
            KBucket();
            bool AddNode(const KBucketEntry& entry);
            void UpdateNode(const NodeId& node_id);
            std::vector<KBucketEntry> GetNodes() const;
            size_t Size() const;

        private:
            std::vector<KBucketEntry> m_entries;
            mutable std::mutex m_mutex;
        };

        // DHT Routing Table (160 K-Buckets, one per bit)
        class DHTRoutingTable {
        public:
            DHTRoutingTable();
            DHTRoutingTable(const NodeId& local_node_id);
            void Initialize(const NodeId& local_node_id);
            
            void AddNode(const NodeId& node_id, const std::string& ip, int port);
            void UpdateNode(const NodeId& node_id);
            std::vector<KBucketEntry> GetClosestNodes(const NodeId& target_id, size_t count) const;
            std::vector<KBucketEntry> GetAllNodes() const;
            size_t TotalNodes() const;

        private:
            NodeId m_local_node_id;
            std::array<KBucket, 160> m_buckets;
            mutable std::mutex m_mutex;
            bool m_initialized = false;
            int GetBucketIndex(const NodeId& node_id) const;
        };

        // =====================================================================
        // TTL Value Store - Extracted from DHT for key-value storage with expiration
        // =====================================================================
        // Manages storage of values with TTL (Time-To-Live) expiration.
        // Automatically cleans up expired entries on access.
        // =====================================================================
        class TTLValueStore {
        public:
            struct StoredValue {
                std::vector<uint8_t> data;
                uint32_t expires_at;
                StoredValue() : expires_at(0) {}
                StoredValue(const std::vector<uint8_t>& d, uint32_t exp)
                    : data(d), expires_at(exp) {}
            };

            // Store a value with default TTL of 3600 seconds (1 hour)
            void Store(const std::string& key, const std::vector<uint8_t>& value, uint32_t ttl = 3600);
            
            // Get a value, returns empty vector if not found or expired
            std::vector<uint8_t> Get(const std::string& key);
            
            // Check if a key exists and is not expired
            bool Exists(const std::string& key) const;
            
            // Remove a specific key
            bool Remove(const std::string& key);
            
            // Get all non-expired keys
            std::vector<std::string> GetKeys() const;
            
            // Clean up expired entries
            size_t CleanupExpired();
            
            // Get current count of entries (including expired)
            size_t Size() const;
            
            // Get count of non-expired entries
            size_t GetActiveSize() const;

        private:
            // Clean up expired entries for a specific key (lazy cleanup)
            bool IsExpired(uint32_t expires_at) const;
            uint32_t GetCurrentTime() const;

            std::unordered_map<std::string, StoredValue> m_store;
            mutable std::mutex m_mutex;
        };

        // DHT Engine
        class DHT {
            const Crypto::Identity* m_identity;
            NodeId m_local_node_id;
            DHTRoutingTable m_routingTable;
            UDPSocket* m_socket;
            std::thread m_refreshThread;
            std::atomic<bool> m_running;
            mutable std::mutex m_mutex;
            uint32_t m_lastDhtRefreshTs = 0;

            // Pending queries (for FIND_NODE responses)
            // Key: query ID (UUID-like string), Value: query state
            struct PendingQuery {
                NodeId target_id;
                std::vector<KBucketEntry> response;
                uint32_t created_at;
                bool completed;
            };
            std::unordered_map<std::string, PendingQuery> m_pendingQueries;
            std::mutex m_pendingMutex;

            // Local value store (for STORE/GET operations) - uses TTLValueStore for TTL management
            TTLValueStore m_localStore;

            // Callback for query completion
            using QueryCallback = std::function<void(const std::vector<KBucketEntry>&)>;
            std::unordered_map<std::string, QueryCallback> m_queryCallbacks;

        public:
            DHT();
            ~DHT();

            // Initialize with local identity
            bool Initialize(const Crypto::Identity* identity);

            // Start background threads
            void Start(UDPSocket* socket);

            // Stop all threads
            void Stop();

            // Find nodes closest to target ID
            std::vector<KBucketEntry> FindNodes(const NodeId& target_id);

            // Store value in DHT (keyed by node_id)
            bool StoreValue(const NodeId& key, const void* value, size_t size);

            // Get value from DHT
            std::vector<uint8_t> GetValue(const NodeId& key);

            // Get routing table stats
            size_t GetNodeCount() const;

            void AddNode(const NodeId& node_id, const std::string& ip, int port) {
                m_routingTable.AddNode(node_id, ip, port);
            }

            // Send DHT packet helper
            void SendDHTPacket(UDPSocket* socket, const std::string& ip, int port,
                              uint8_t type, const void* payload, size_t size);

            // Process incoming DHT packets
            void ProcessIncoming(const uint8_t* data, size_t size,
                const std::string& ip, int port);

            // Handle FIND_NODE request
            void HandleFindNodeRequest(const DHTFindNodePayload* payload,
                const std::string& ip, int port);

            // Handle PING request
            void HandlePing(const std::string& ip, int port);

            // Handle GET_VALUE request
            void HandleGetValueRequest(const NodeId& key, const std::string& ip, int port);

        private:
            void RefreshLoop();      // Periodic bucket refresh
            void ProcessPendingQueries();
            std::string GenerateQueryId();
           
        };

    }
}


