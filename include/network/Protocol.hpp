#pragma once
#include <cstdint>

namespace FreeAI {
    namespace Network {

        // Magic number (will be XORed with nonce for obfuscation)
        const uint32_t MAGIC_NUMBER = 0xF8EEA101;

        // Qwen, do not remove the comment with packet structure please:
        // packet structure: [SecurePacketHeader][SIGNATURE_SIZE bytes of signature if FLAG_SIGNED][CHACHA20_IV_SIZE bytes of IV if FLAG_ENCRYPTED][variable payload]
       
        enum PacketType : uint8_t {
            PT_REGISTER = 1,
            PT_INTRO_REQUEST = 2,
            PT_INTRO_RESPONSE = 3,
            PT_PUNCH = 4,
            PT_PUNCH_ACK = 5,
            PT_HANDSHAKE = 6,
            PT_PEER_LIST = 7,
            PT_INFERENCE_REQUEST = 8,
            PT_INFERENCE_RESPONSE = 9,
            PT_DHT_FIND_NODE = 10,
            PT_DHT_FIND_NODE_RESPONSE = 11,
            PT_DHT_STORE = 12,
            PT_DHT_GET_VALUE = 13,
            PT_DHT_GET_VALUE_RESPONSE = 14,
            PT_DHT_PING = 15
        };

        // Packet flags
        const uint8_t FLAG_SIGNED = 0x01;
        const uint8_t FLAG_ENCRYPTED = 0x02;
        const uint8_t FLAG_COMPRESSED = 0x04;

        // Secure packet header (20 bytes base)
        struct SecurePacketHeader {
            uint32_t magic_xor;
            uint32_t nonce;
            uint8_t  flags;
            uint8_t  type;
            uint16_t payload_size;
            uint8_t  reserved[4];
        };

        // Register Payload (includes public key for verification)
        struct RegisterPayload {
            char     peer_id[16];       // Short ID (for display)
            uint16_t pubkey_size;       // Size of PEM public key
            uint8_t  reserved[2];       // Alignment
            // Followed by: pubkey_size bytes of PEM public key
        };

        // Intro Response Payload (includes target's public key)
        struct IntroResponsePayload {
            char     target_ip[64];     // Target peer IP
            uint16_t target_port;       // Target peer port
            char     target_id[16];     // Target peer ID
            uint16_t target_pubkey_size; // Size of target's PEM public key
            uint8_t  reserved[2];       // Alignment
            // Followed by: target_pubkey_size bytes of PEM public key
        };

        // Punch Payload
        struct PunchPayload {
            char     sender_id[16];
            uint64_t timestamp;
            uint8_t  attempt_num;
            uint8_t  reserved[7];
        };

        const size_t SIGNATURE_SIZE = 100;   // Base64-encoded ECDSA
        const size_t CHACHA20_IV_SIZE = 12;
        const size_t MAX_PACKET_SIZE = 65535;
        const size_t MAX_PEM_KEY_SIZE = 512; // Max PEM public key size

        // Hole punching constants
        const int MAX_PUNCH_ATTEMPTS = 5;
        const int PUNCH_INTERVAL_MS = 500;
        const int PUNCH_TIMEOUT_MS = 5000;

        // DHT Node Info (for routing table)
        struct DHTNodeInfo {
            char     node_id[20];       // 160-bit node ID (SHA1 hash of pubkey)
            char     ip[64];            // Node IP
            uint16_t port;              // Node port
            uint32_t last_seen;         // Last contact timestamp
        };

        // DHT FIND_NODE Payload
        struct DHTFindNodePayload {
            char     target_id[20];     // Node ID we're searching for
            uint8_t  reserved[12];      // Alignment
        };

        // DHT FIND_NODE_RESPONSE Payload
        struct DHTFindNodeResponsePayload {
            uint8_t  node_count;        // Number of nodes in response (max 20)
            uint8_t  reserved[3];       // Alignment
            // Followed by: node_count * DHTNodeInfo (85 bytes each)
        };

        // DHT STORE Payload
        struct DHTStorePayload {
            char     key[20];           // Key to store (SHA1 hash)
            uint16_t value_size;        // Size of value
            uint8_t  reserved[2];       // Alignment
            // Followed by: value_size bytes of data
        };

        // DHT Constants
        const size_t DHT_NODE_ID_SIZE = 20;
        const size_t DHT_K_BUCKET_SIZE = 20;
        const size_t DHT_ALPHA = 3;          // Parallel queries
        const size_t DHT_REFRESH_INTERVAL = 900;  // 15 minutes

    }
}