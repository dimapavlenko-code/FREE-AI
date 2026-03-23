#include "network/PacketSecurity.hpp"
#include "utils/Helpers.hpp"
#include <cstring>
#include <random>
#include <iostream>

#include <mbedtls/mbedtls_config.h>
#ifdef MBEDTLS_CHACHA20_C
#include <mbedtls/chacha20.h>
#endif

namespace FreeAI {
    namespace Network {

        // Static work memory for compression (~64KB)
        lzo_align_t PacketSecurity::s_compressWorkMem[LZO1X_1_MEM_COMPRESS];
        bool PacketSecurity::s_initialized = false;

        bool PacketSecurity::Initialize() {
            if (s_initialized) {
                return true;
            }

            int ret = lzo_init();
            if (ret != LZO_E_OK) {
                std::cerr << "[COMPRESSION] lzo_init() failed: " << ret << std::endl;
                return false;
            }

            s_initialized = true;
            std::cout << "[COMPRESSION] MiniLZO initialized successfully" << std::endl;
            return true;
        }

        void PacketSecurity::Shutdown() {
            // MiniLZO doesn't require cleanup, but good practice
            s_initialized = false;
        }

        uint32_t PacketSecurity::GenerateNonce() {
            std::random_device rd;
            std::mt19937 gen(rd());
            std::uniform_int_distribution<uint32_t> dist;
            return dist(gen);
        }

        void PacketSecurity::XorObfuscate(uint8_t* data, size_t size, uint32_t key) {
            for (size_t i = 0; i < size; ++i) {
                data[i] ^= (uint8_t)((key >> ((i % 4) * 8)) & 0xFF);
            }
        }

        std::vector<uint8_t> PacketSecurity::CompressData(const uint8_t* input, size_t inputSize) {
            if (!s_initialized) {
                std::cerr << "[COMPRESSION] Not initialized!" << std::endl;
                return std::vector<uint8_t>(input, input + inputSize);
            }

            // Calculate output buffer size (worst case)
            size_t maxOutSize = inputSize + inputSize / 16 + 64 + 3;
            std::vector<uint8_t> output(maxOutSize);

            lzo_uint outLen = (lzo_uint)maxOutSize;

            int ret = lzo1x_1_compress(
                input,
                (lzo_uint)inputSize,
                output.data(),
                &outLen,
                s_compressWorkMem
            );

            if (ret != LZO_E_OK) {
                std::cerr << "[COMPRESSION] Compression failed: " << ret << std::endl;
                return std::vector<uint8_t>(input, input + inputSize);
            }

            // If compression didn't help (incompressible data), return original
            if (outLen >= inputSize) {
                return std::vector<uint8_t>(input, input + inputSize);
            }

            output.resize(outLen);
            return output;
        }

        std::vector<uint8_t> PacketSecurity::DecompressData(const uint8_t* input, size_t inputSize, size_t expectedSize) {
            if (!s_initialized) {
                std::cerr << "[COMPRESSION] Not initialized!" << std::endl;
                return std::vector<uint8_t>(input, input + inputSize);
            }

            // Allocate output buffer (use expected size from header)
            std::vector<uint8_t> output(expectedSize);
            lzo_uint outLen = (lzo_uint)expectedSize;

            int ret = lzo1x_decompress_safe(
                input,
                (lzo_uint)inputSize,
                output.data(),
                &outLen,
                NULL  // No work memory needed for decompression
            );

            if (ret != LZO_E_OK) {
                std::cerr << "[COMPRESSION] Decompression failed: " << ret << std::endl;
                return std::vector<uint8_t>();
            }

            if (outLen != expectedSize) {
                std::cerr << "[COMPRESSION] Decompressed size mismatch: "
                    << outLen << " != " << expectedSize << std::endl;
                return std::vector<uint8_t>();
            }

            return output;
        }

        std::vector<uint8_t> PacketSecurity::PrepareOutgoing(
            uint8_t type,
            const void* payload,
            size_t payloadSize,
            bool sign,
            bool encrypt,
            const Crypto::Identity* identity)
        {
            SecurePacketHeader header;
            header.nonce = GenerateNonce();
            header.magic_xor = MAGIC_NUMBER ^ header.nonce;
            header.type = type;
            header.flags = 0;
            header.payload_size = (uint16_t)payloadSize;  // ← Original size!
            memset(header.reserved, 0, sizeof(header.reserved));

            if (sign && identity && identity->IsValid() && type != PT_REGISTER && (type < PT_DHT_FIND_NODE || type > PT_DHT_PING)) { //  network topology discovery types of packet may be signed, but we still not have the peer's public key) {
                header.flags |= FLAG_SIGNED;
            }

            if (encrypt && payloadSize) {
                header.flags |= FLAG_ENCRYPTED;
            }

            // NEW: Prepare signature data BEFORE compression (sign original payload)
            std::vector<uint8_t> signData;
            std::string signature;

            if (header.flags & FLAG_SIGNED) {
                // Sign header + ORIGINAL payload (before compression)
                signData.insert(signData.end(), (uint8_t*)&header, (uint8_t*)&header + sizeof(SecurePacketHeader));
                signData.insert(signData.end(), (const uint8_t*)payload, (const uint8_t*)payload + payloadSize);

                signature = identity->Sign(signData.data(), signData.size());
                if (signature.empty()) {
                    header.flags &= ~FLAG_SIGNED;
                }
            }

            // Compress payload if large enough to benefit (threshold: 100 bytes)
            const void* finalPayload = payload;
            size_t finalPayloadSize = payloadSize;
            std::vector<uint8_t> compressedPayload;

            if (payloadSize > 100) {
                compressedPayload = CompressData((const uint8_t*)payload, payloadSize);
                if (compressedPayload.size() < payloadSize) {
                    header.flags |= FLAG_COMPRESSED;
                    finalPayload = compressedPayload.data();
                    finalPayloadSize = compressedPayload.size();
                }
            }

            // Calculate total size
            size_t totalSize = 0;

            std::vector<uint8_t> packet(MAX_PACKET_SIZE);
            uint8_t* ptr = (uint8_t*)packet.data();

            // Write header
            memcpy(ptr, &header, sizeof(SecurePacketHeader));
            ptr += sizeof(SecurePacketHeader);
            totalSize += sizeof(SecurePacketHeader);

            // Write signature (if signed)
            if (header.flags & FLAG_SIGNED) {
                memset(ptr, 0, SIGNATURE_SIZE);
                memcpy(ptr, signature.c_str(), signature.size());
                ptr += SIGNATURE_SIZE;
                totalSize += SIGNATURE_SIZE;
            }

            // Handle encryption
            std::vector<uint8_t> encryptedPayload;

            if (header.flags & FLAG_ENCRYPTED) {
                // Generate IV
                uint8_t iv[CHACHA20_IV_SIZE];
                for (int i = 0; i < CHACHA20_IV_SIZE; ++i) {
                    iv[i] = (uint8_t)(GenerateNonce() & 0xFF);
                }

                memcpy(ptr, iv, CHACHA20_IV_SIZE);
                ptr += CHACHA20_IV_SIZE;
                totalSize += CHACHA20_IV_SIZE;

                // Encrypt (ChaCha20 if available, else XOR)
#ifdef MBEDTLS_CHACHA20_C
                mbedtls_chacha20_context ctx;
                mbedtls_chacha20_init(&ctx);
                uint8_t key[32];
                memset(key, 0x42, 32);
                mbedtls_chacha20_setkey(&ctx, key);
                mbedtls_chacha20_starts(&ctx, iv, 0);

                encryptedPayload.resize(finalPayloadSize);
                mbedtls_chacha20_update(&ctx, finalPayloadSize,
                    (const uint8_t*)finalPayload,
                    encryptedPayload.data());
                mbedtls_chacha20_free(&ctx);

                finalPayload = encryptedPayload.data();
                finalPayloadSize = encryptedPayload.size();
#else
                encryptedPayload.assign((const uint8_t*)finalPayload,
                    (const uint8_t*)finalPayload + finalPayloadSize);
                XorObfuscate(encryptedPayload.data(), encryptedPayload.size(), header.nonce);
                finalPayload = encryptedPayload.data();
                finalPayloadSize = encryptedPayload.size();
#endif
            }

            // Write payload
            memcpy(ptr, finalPayload, finalPayloadSize);
            ptr += finalPayloadSize;
            totalSize += finalPayloadSize;

            packet.resize(totalSize);

            // Final XOR obfuscation on entire packet (except magic_xor and nonce)
            XorObfuscate(packet.data() + sizeof(uint32_t) * 2, totalSize - sizeof(uint32_t) * 2, header.nonce);

            return packet;
        }

        bool PacketSecurity::ProcessIncoming(
            const uint8_t* data,
            size_t dataSize,
            SecurePacketHeader& outHeader,
            std::vector<uint8_t>& outPayload,
            const Crypto::Identity* identity,
            const std::string& senderPubKey)
        {
            if (dataSize < sizeof(SecurePacketHeader)) {
                return false;
            }

            // De-obfuscate
            size_t offs = 0;
            std::vector<uint8_t> packet(data, data + dataSize);
            uint8_t* ptr = (uint8_t*)packet.data();
            uint32_t nonce;
            memcpy(&nonce, ptr + sizeof(uint32_t), sizeof(uint32_t));
            XorObfuscate(ptr + sizeof(uint32_t) * 2, packet.size() - sizeof(uint32_t) * 2, nonce);

            // Read header
            memcpy(&outHeader, ptr, sizeof(SecurePacketHeader));

            ptr += sizeof(SecurePacketHeader);
            offs += sizeof(SecurePacketHeader);

            // Verify magic
            if ((outHeader.magic_xor ^ outHeader.nonce) != MAGIC_NUMBER) {
                return false;
            }

            // Extract signature if signed
            std::string signatureB64;
            if (outHeader.flags & FLAG_SIGNED) {
                signatureB64 = TrimNulls(std::string((const char*)ptr, SIGNATURE_SIZE));
                ptr += SIGNATURE_SIZE;
                offs += SIGNATURE_SIZE;
            }

            // Handle decryption
            const void* finalPayload = ptr;
            size_t finalPayloadSize = dataSize - offs;
            std::vector<uint8_t> decryptedPayload;

            if (outHeader.flags & FLAG_ENCRYPTED) {
                if (finalPayloadSize <= CHACHA20_IV_SIZE) {
                    std::cerr << "[DECRYPT] Payload too small for encrypted data!" << std::endl;
                    return false;
                }

                uint8_t iv[CHACHA20_IV_SIZE];
                memcpy(iv, ptr, CHACHA20_IV_SIZE);
                ptr += CHACHA20_IV_SIZE;
                offs += CHACHA20_IV_SIZE;

                const uint8_t* encryptedData = ptr;
                size_t encryptedSize = dataSize - offs;

#ifdef MBEDTLS_CHACHA20_C
                mbedtls_chacha20_context ctx;
                mbedtls_chacha20_init(&ctx);
                uint8_t key[32];
                memset(key, 0x42, 32);
                mbedtls_chacha20_setkey(&ctx, key);
                mbedtls_chacha20_starts(&ctx, iv, 0);

                decryptedPayload.resize(encryptedSize);
                mbedtls_chacha20_update(&ctx, encryptedSize,
                    encryptedData,
                    decryptedPayload.data());
                mbedtls_chacha20_free(&ctx);

                finalPayload = decryptedPayload.data();
                finalPayloadSize = decryptedPayload.size();
#else
                decryptedPayload.assign(encryptedData, encryptedData + encryptedSize);
                XorObfuscate(decryptedPayload.data(), decryptedPayload.size(), outHeader.nonce);
                finalPayload = decryptedPayload.data();
                finalPayloadSize = decryptedPayload.size();
#endif
            }

            // NEW: Decompress BEFORE signature verification
            std::vector<uint8_t> decompressedPayload;
            if (outHeader.flags & FLAG_COMPRESSED) {
                decompressedPayload = DecompressData(
                    (const uint8_t*)finalPayload,
                    finalPayloadSize,
                    outHeader.payload_size  // Expected original size
                );

                if (decompressedPayload.empty()) {
                    std::cerr << "[DECOMPRESS] Decompression failed!" << std::endl;
                    return false;
                }

                finalPayload = decompressedPayload.data();
                finalPayloadSize = decompressedPayload.size();
            }

            // NEW: Verify signature AFTER decompression (against original data)
            if (!signatureB64.empty()) {
                if (senderPubKey.empty()) { 
                    std::cerr << "[CRYPTO] Cannot verify signature: no public key" << std::endl;                    
                    return false;
                    
                }
                else {
                    // Verify against header + DECOMPRESSED payload (original data)
                    std::vector<uint8_t> signData;
                    signData.insert(signData.begin(), packet.begin(), packet.begin() + sizeof(SecurePacketHeader));
                    signData.insert(signData.end(), (const uint8_t*)finalPayload, (const uint8_t*)finalPayload + finalPayloadSize);

                    if (!Crypto::Identity::Verify(
                        signData.data(), signData.size(),
                        signatureB64, senderPubKey)) {
                        std::cerr << "[CRYPTO] Signature verification FAILED" << std::endl;
                        return false;
                    }
                    std::cout << "[CRYPTO] Signature verified OK" << std::endl;
                }
            }

            // Extract payload
            size_t payloadSize = outHeader.payload_size;
            if (payloadSize > finalPayloadSize) {
                return false;
            }

            outPayload.assign((const uint8_t*)finalPayload, (const uint8_t*)finalPayload + payloadSize);

            return true;
        }

    }
}