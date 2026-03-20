#pragma once
#include <cstdint>

namespace FreeAI {
    namespace Network {

        // Magic number (will be XORed with nonce for obfuscation)
        const uint32_t MAGIC_NUMBER = 0xF8EEA101;

        enum PacketType : uint8_t {
            PT_REGISTER = 1,
            PT_INTRO_REQUEST = 2,
            PT_INTRO_RESPONSE = 3,
            PT_PUNCH = 4,
            PT_HANDSHAKE = 5,
            PT_PEER_LIST = 6,
            PT_INFERENCE_REQUEST = 7,
            PT_INFERENCE_RESPONSE = 8
        };

        // Packet flags
        const uint8_t FLAG_SIGNED = 0x01;
        const uint8_t FLAG_ENCRYPTED = 0x02;
        const uint8_t FLAG_COMPRESSED = 0x04;

        // Secure packet header (20 bytes base)
        struct SecurePacketHeader {
            uint32_t magic_xor;      // MAGIC_NUMBER XOR nonce (obfuscation)
            uint32_t nonce;          // Random value (prevents replay, used for XOR)
            uint8_t  flags;          // FLAG_SIGNED | FLAG_ENCRYPTED
            uint8_t  type;           // PacketType
            uint16_t payload_size;   // Actual payload size
            uint8_t  reserved[4];    // Alignment
            // Followed by (in order):
            // 1. Signature (if FLAG_SIGNED): 88 bytes (Base64 Ed25519)
            // 2. IV (if FLAG_ENCRYPTED): 12 bytes (ChaCha20)
            // 3. Payload (encrypted if FLAG_ENCRYPTED)
        };

        const size_t SIGNATURE_SIZE = 88;   // Base64-encoded Ed25519 (64 bytes)
        const size_t CHACHA20_IV_SIZE = 12;
        const size_t MAX_PACKET_SIZE = 65535;

    }
}