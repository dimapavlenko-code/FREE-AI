#include "network/PacketSecurity.hpp"
#include <cstring>
#include <random>
#include <iostream>

#ifdef MBEDTLS_CHACHA20_C
#include <mbedtls/chacha20.h>
#endif

namespace FreeAI {
	namespace Network {

		uint32_t PacketSecurity::GenerateNonce() {
			std::random_device rd;
			std::mt19937 gen(rd());
			std::uniform_int_distribution<uint32_t> dist;
			return dist(gen);
		}

		void PacketSecurity::XorObfuscate(uint8_t* data, size_t size, uint32_t key) {
			for (size_t i = 0; i < size; ++i) {
				data[i] ^= static_cast<uint8_t>((key >> ((i % 4) * 8)) & 0xFF);
			}
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
			header.payload_size = static_cast<uint16_t>(payloadSize);
			std::memset(header.reserved, 0, sizeof(header.reserved));

			if (sign && identity && identity->IsValid()) {
				header.flags |= FLAG_SIGNED;
			}
			if (encrypt) {
				header.flags |= FLAG_ENCRYPTED;
			}

			// Calculate total size
			size_t totalSize = 0;

			std::vector<uint8_t> packet(MAX_PACKET_SIZE);
			uint8_t* ptr = packet.data();

			// Write header (XOR obfuscate the magic)
			std::memcpy(ptr, &header, sizeof(SecurePacketHeader));
			ptr += sizeof(SecurePacketHeader);
			totalSize += sizeof(SecurePacketHeader);

			// Prepare data to sign (header + payload)
			std::vector<uint8_t> signData;
			if (header.flags & FLAG_SIGNED) {
				signData.insert(signData.end(), packet.data(), packet.data() + sizeof(SecurePacketHeader));
				signData.insert(signData.end(), static_cast<const uint8_t*>(payload),
					static_cast<const uint8_t*>(payload) + payloadSize);

				// Actually, let's put signature right after header for easier parsing             
				std::string signature = identity->Sign(signData.data(), signData.size());
				if (signature.empty()) {
					header.flags &= ~FLAG_SIGNED; // Remove flag if signing failed
				}
				else {
					// Write signature at the position after header                        
					std::memset(ptr, 0, SIGNATURE_SIZE);
					std::memcpy(ptr, signature.c_str(), signature.size());
					ptr += SIGNATURE_SIZE;
					totalSize += SIGNATURE_SIZE;
				}
			}

			// Handle encryption
			const void* finalPayload = payload;
			size_t finalPayloadSize = payloadSize;
			std::vector<uint8_t> encryptedPayload;

			if (header.flags & FLAG_ENCRYPTED) {
				// Generate IV
				uint8_t iv[CHACHA20_IV_SIZE];
				for (int i = 0; i < CHACHA20_IV_SIZE; ++i) {
					iv[i] = static_cast<uint8_t>(GenerateNonce() & 0xFF);
				}

				std::memcpy(ptr, iv, CHACHA20_IV_SIZE);
				ptr += CHACHA20_IV_SIZE;
				totalSize += CHACHA20_IV_SIZE;

				// Encrypt (ChaCha20 if available, else XOR)
#ifdef MBEDTLS_CHACHA20_C
				mbedtls_chacha20_context ctx;
				mbedtls_chacha20_init(&ctx);
				uint8_t key[32]; // Should derive from handshake
				std::memset(key, 0x42, 32); // Placeholder
				mbedtls_chacha20_setkey(&ctx, key);
				mbedtls_chacha20_starts(&ctx, iv, 0);

				encryptedPayload.resize(payloadSize);
				mbedtls_chacha20_update(&ctx, payloadSize,
					static_cast<const uint8_t*>(payload),
					encryptedPayload.data());
				mbedtls_chacha20_free(&ctx);

				finalPayload = encryptedPayload.data();
				finalPayloadSize = encryptedPayload.size();
#else
	// Fallback to XOR
				encryptedPayload.assign(static_cast<const uint8_t*>(payload),
					static_cast<const uint8_t*>(payload) + payloadSize);
				XorObfuscate(encryptedPayload.data(), encryptedPayload.size(), header.nonce);
				finalPayload = encryptedPayload.data();
				finalPayloadSize = encryptedPayload.size();
#endif
			}

			// Write payload			
			std::memcpy(ptr, finalPayload, finalPayloadSize);
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
			uint8_t* ptr = packet.data();
			uint32_t nonce;
			std::memcpy(&nonce, ptr + sizeof(uint32_t), sizeof(uint32_t));
			// skip magic and nonce
			XorObfuscate(ptr + sizeof(uint32_t) * 2, packet.size() - sizeof(uint32_t) * 2, nonce);

			// Read header
			std::memcpy(&outHeader, ptr, sizeof(SecurePacketHeader));

			ptr += sizeof(SecurePacketHeader);
			offs += sizeof(SecurePacketHeader);

			// Verify magic
			if ((outHeader.magic_xor ^ outHeader.nonce) != MAGIC_NUMBER) {
				return false; // Invalid packet
			}

			// extract signature if signed
			std::string signatureB64;
			if (outHeader.flags & FLAG_SIGNED) {
				// Extract signature from packet
				signatureB64 = std::string((const char*)ptr, SIGNATURE_SIZE);
				ptr += SIGNATURE_SIZE;
				offs += SIGNATURE_SIZE;
			}

			// Handle decryption
			const void* finalPayload = ptr;
			size_t finalPayloadSize = dataSize - offs;
			std::vector<uint8_t> decryptedPayload;

			if (outHeader.flags & FLAG_ENCRYPTED) {
				// Validate minimum size (IV + at least 1 byte of data)
				if (finalPayloadSize <= CHACHA20_IV_SIZE) {
					std::cerr << "[DECRYPT] Payload too small for encrypted data!" << std::endl;
					return "";
				}

				// Extract IV from the beginning of the payload
				uint8_t iv[CHACHA20_IV_SIZE];
				std::memcpy(iv, ptr, CHACHA20_IV_SIZE);
				ptr += CHACHA20_IV_SIZE;
				offs += CHACHA20_IV_SIZE;

				// Point to actual encrypted data (after IV)
				const uint8_t* encryptedData = ptr;
				size_t encryptedSize = dataSize - offs;

				// Decrypt (ChaCha20 if available, else XOR)
#ifdef MBEDTLS_CHACHA20_C
				mbedtls_chacha20_context ctx;
				mbedtls_chacha20_init(&ctx);

				// Use same key as encryption (should derive from handshake)
				uint8_t key[32];
				std::memset(key, 0x42, 32); // Placeholder - match encryption side

				mbedtls_chacha20_setkey(&ctx, key);
				mbedtls_chacha20_starts(&ctx, iv, 0); // Same counter start = 0

				decryptedPayload.resize(encryptedSize);
				mbedtls_chacha20_update(&ctx, encryptedSize,
					encryptedData,
					decryptedPayload.data());
				mbedtls_chacha20_free(&ctx);

				finalPayload = decryptedPayload.data();
				finalPayloadSize = decryptedPayload.size();
#else
	// Fallback to XOR (symmetric operation)
				decryptedPayload.assign(encryptedData, encryptedData + encryptedSize);
				XorObfuscate(decryptedPayload.data(), decryptedPayload.size(), outHeader.nonce);
				finalPayload = decryptedPayload.data();
				finalPayloadSize = decryptedPayload.size();
#endif
			}


			// Parse payload

			// Verify signature if present
			if (!signatureB64.empty()) {
				if (senderPubKey.empty()) {
					std::cerr << "[CRYPTO] Cannot verify signature: no public key" << std::endl;
					// Still accept for REGISTER packets (first-time key exchange)
					if (outHeader.type != PT_REGISTER) {
						return false;
					}
				}
				else {
					// Verify signature (header + payload)
					std::vector<uint8_t> signData(packet.begin(),
						packet.begin() + sizeof(SecurePacketHeader));

					signData.insert(signData.end(),
						(const uint8_t*)finalPayload,
						(const uint8_t*)finalPayload + finalPayloadSize);

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

			outPayload.assign((const uint8_t*)finalPayload,	(const uint8_t*)finalPayload + payloadSize);

			return true;
		}

	}
}