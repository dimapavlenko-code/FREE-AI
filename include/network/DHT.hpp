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

namespace FreeAI {
    namespace Network {

        // XOR distance between two node IDs
        uint64_t XORDistance(const uint8_t* id1, const uint8_t* id2);

        // K-Bucket entry
        struct KBucketEntry {
            uint8_t node_id[DHT_NODE_ID_SIZE];
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
            void UpdateNode(const uint8_t* node_id);
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
            DHTRoutingTable(const uint8_t* local_node_id);
            void Initialize(const uint8_t* local_node_id);
            
            void AddNode(const uint8_t* node_id, const std::string& ip, int port);
            void UpdateNode(const uint8_t* node_id);
            std::vector<KBucketEntry> GetClosestNodes(const uint8_t* target_id, size_t count) const;
            std::vector<KBucketEntry> GetAllNodes() const;
            size_t TotalNodes() const;

        private:
            uint8_t m_local_node_id[DHT_NODE_ID_SIZE];
            std::array<KBucket, 160> m_buckets;
            mutable std::mutex m_mutex;
            bool m_initialized;
            
            int GetBucketIndex(const uint8_t* node_id) const;
        };

        // DHT Engine
        class DHT {
            const Crypto::Identity* m_identity;
            uint8_t m_local_node_id[DHT_NODE_ID_SIZE];
            DHTRoutingTable m_routingTable;
            UDPSocket* m_socket;
            std::thread m_refreshThread;
            std::atomic<bool> m_running;
            mutable std::mutex m_mutex;
            uint32_t m_lastDhtRefreshTs = 0;

            // Pending queries (for FIND_NODE responses)
            std::unordered_map<std::string, std::vector<KBucketEntry>> m_pendingQueries;
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
            std::vector<KBucketEntry> FindNodes(const uint8_t* target_id);

            // Store value in DHT
            bool StoreValue(const uint8_t* key, const void* value, size_t size);

            // Get value from DHT
            std::vector<uint8_t> GetValue(const uint8_t* key);

            // Get routing table stats
            size_t GetNodeCount() const;

            void AddNode(const uint8_t* node_id, const std::string& ip, int port) {
                m_routingTable.AddNode(node_id, ip, port);
            }

            // Send DHT packet helper
            void SendDHTPacket(UDPSocket* socket, const std::string& ip, int port,
                              uint8_t type, const void* payload, size_t size);
            void ProcessIncoming(const uint8_t* data, size_t size,
                const std::string& ip, int port);

        private:
            void RefreshLoop();      // Periodic bucket refresh            
           
        };

    }
}
