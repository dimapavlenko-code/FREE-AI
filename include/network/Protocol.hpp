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
			PT_INTRO_REQUEST,
			PT_INTRO_RESPONSE,
			PT_PUNCH,
			PT_PUNCH_ACK,
			PT_HANDSHAKE,
			PT_PEER_LIST,
			PT_INFERENCE_REQUEST,
			PT_INFERENCE_RESPONSE,
			PT_DHT_FIND_NODE,
			PT_DHT_FIND_NODE_RESPONSE,
			PT_DHT_STORE,
			PT_DHT_GET_VALUE,
			PT_DHT_GET_VALUE_RESPONSE,
			PT_DHT_PING,
			
			// STUN message types (RFC 5389)
			PT_STUN_BINDING_REQUEST = 100,
			PT_STUN_BINDING_RESPONSE,
			PT_STUN_BINDING_ERROR_RESPONSE,
			
			// Hole punch coordination message types
			PT_COORD_HOLE_PUNCH_REQUEST = 200,
			PT_COORD_HOLE_PUNCH_INFO,
			PT_COORD_HOLE_PUNCH_START,
			PT_COORD_HOLE_PUNCH_FAILED,
			PT_COORD_HOLE_PUNCH_MULTI_START,    // Multi-port punch coordination
			PT_COORD_HOLE_PUNCH_PROXY_FALLBACK  // Proxy fallback notification
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

		const size_t SIGNATURE_SIZE = 100;   // Base64-encoded ECDSA
		const size_t CHACHA20_IV_SIZE = 12;
		const size_t MAX_PACKET_SIZE = 65535;
		const size_t MAX_PEM_KEY_SIZE = 512; // Max PEM public key size

		/* Registration of peers sequence
	Initiator (A)                          Receiver (B)
     │                                       │
     │─── ers_register (A's pubkey) ────────▶│
     │                                       │ Store A's pubkey
     │                                       │ Status = Connecting
     │◀── ers_register_resp (B's pubkey) ────│
     │                                       │
     │ Store B's pubkey                      │
     │ Status = Connecting                   │
     │                                       │
     │─── ers_accepted (empty) ─────────────▶│
     │                                       │
     │ Status = Connected                    │ Status = Connected
     ▼                                       ▼

	 Any ─── ers_failed (empty) ───▶ Any
							   │
							   ▼
						Clear all peer state
						(including pubkey)
		*/

		enum eRegStep : uint8_t {
			ers_register = 0,      // Initial registration (with pubkey)
			ers_register_resp,     // Response with our pubkey
			ers_accepted,          // Final ACK (empty pubkey)
			ers_failed             // Failure (empty pubkey)
		};

		// Register Payload (includes public key for verification)
		struct RegisterPayload {
			char     peer_id[16];       // Short ID (for display)
			uint16_t pubkey_size;       // Size of PEM public key
			uint8_t  step;              // eRegStep
			uint8_t  reserved;          // Alignment
			uint8_t  pubkey[MAX_PEM_KEY_SIZE];  // Fixed buffer
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

		// Punch Payload (single port)
		struct PunchPayload {
			char     sender_id[16];
			uint64_t timestamp;
			uint8_t  attempt_num;
			uint8_t  port_offset;       // Offset from base port (0 to MAX_PORT_RANGE-1)
			uint8_t  port_range;        // Number of ports in range
			uint8_t  reserved[4];
		};

		// Multi-port punch payload (for coordinated port range punching)
		struct MultiPortPunchPayload {
			char     sender_id[16];
			uint64_t timestamp;
			uint8_t  attempt_num;
			uint8_t  port_range;        // Number of ports in range
			uint16_t base_port;         // Base port for the range
			uint8_t  reserved[6];
		};


		// Hole punching constants
		const int MAX_PUNCH_ATTEMPTS = 30;
		const int PUNCH_INTERVAL_MS = 1000;
		const int PUNCH_TIMEOUT_MS = (MAX_PUNCH_ATTEMPTS * PUNCH_INTERVAL_MS);

		// Multi-port hole punching constants
		const int MAX_PORT_RANGE = 5;           // Number of consecutive ports to punch
		const int MIN_PUNCH_PORT = 10000;       // Minimum port in range
		const int MAX_PUNCH_PORT = 60000;       // Maximum port in range
		// DHT Constants
		const size_t DHT_NODE_ID_SIZE = 20;
		const size_t DHT_K_BUCKET_SIZE = 20;
		const size_t DHT_ALPHA = 3;          // Parallel queries
		const size_t DHT_REFRESH_INTERVAL = 900;  // 15 minutes

		// DHT Node Info (for routing table)
		struct DHTNodeInfo {
			uint8_t  node_id[DHT_NODE_ID_SIZE];       // 160-bit node ID (SHA1 hash of pubkey)
			char     ip[64];            // Node IP
			uint16_t port;              // Node port
			uint32_t last_seen;         // Last contact timestamp
			uint8_t  reserved[2];       // Alignment (makes it 92 bytes)
		};

		// DHT FIND_NODE Payload
				struct DHTFindNodePayload {
					uint8_t  target_id[DHT_NODE_ID_SIZE];     // Node ID we're searching for
					uint8_t  reserved[12];      // Alignment
				};

		// DHT FIND_NODE_RESPONSE Payload
		struct DHTFindNodeResponsePayload {
			uint8_t  node_count;        // Number of nodes in response (max 20)
			uint8_t  reserved[3];       // Alignment
			// Followed by: node_count * DHTNodeInfo (92 bytes each)
		};

		// DHT STORE Payload
				struct DHTStorePayload {
					uint8_t  key[DHT_NODE_ID_SIZE];     // Key to store (SHA1 hash)
					uint16_t value_size;        // Size of value
					uint8_t  reserved[2];       // Alignment
					// Followed by: value_size bytes of data
				};
		
				// STUN Message Types (RFC 5389)
				const uint16_t STUN_CLASS_MASK = 0x0110;
				const uint16_t STUN_CLASS_CLIENT = 0x0000;
				const uint16_t STUN_METHOD_BINDING = 0x0001;
		
				// STUN Attribute Types
				const uint16_t STUN_ATTRIBUTE_MAPPED_ADDRESS = 0x0001;
				const uint16_t STUN_ATTRIBUTE_USERNAME = 0x0006;
				const uint16_t STUN_ATTRIBUTE_MESSAGE_INTEGRITY = 0x0008;
				const uint16_t STUN_ATTRIBUTE_ERROR_CODE = 0x0009;
				const uint16_t STUN_ATTRIBUTE_UNKNOWN_ATTRIBUTES = 0x000A;
				const uint16_t STUN_ATTRIBUTE_XOR_MAPPED_ADDRESS = 0x8027;
		
				// STUN Magic Cookie
				static const uint32_t STUN_MAGIC_COOKIE = 0x2112A442;
				static const size_t STUN_TRANSACTION_ID_SIZE = 16;
		
				// STUN Mapped Address format (for IPv4, RFC 5389 Section 15.1)
				struct STUNMappedAddress {
					uint8_t addressFamily;    // 0x01 for IPv4
					uint8_t padding;          // Must be 0x00
					uint16_t port;            // In network byte order
					uint32_t address;         // In network byte order
				};
		
				// STUN Attribute header
				struct STUNAttributeHeader {
					uint16_t type;
					uint16_t length;
				};
		
				// Coordination hole punch payload (COORD_HOLE_PUNCH_REQUEST)
				struct CoordHolePunchPayload {
					char     requester_id[16];   // ID of the peer requesting the punch
					char     target_id[16];      // ID of the peer to connect to
					uint8_t  reserved[4];        // Alignment
				};
		
				// Coordination hole punch info payload (COORD_HOLE_PUNCH_INFO)
				// For single-port punching
				struct CoordHolePunchInfoPayload {
					char     peer_id[16];        // The peer we're connecting to
					char     peer_ip[64];        // External IP discovered via STUN
					uint16_t peer_port;          // External port discovered via STUN
					uint16_t stun_port;          // Port their STUN server is on
					uint64_t punch_start_time;   // Timestamp when both should start punching
					uint8_t  reserved[2];        // Alignment
				};

				// Coordination hole punch info payload with port range (COORD_HOLE_PUNCH_INFO)
				// For multi-port punching - use this when port_range > 0
				struct CoordHolePunchInfoMultiPayload {
					char     peer_id[16];        // The peer we're connecting to
					char     peer_ip[64];        // External IP discovered via STUN
					uint16_t peer_base_port;     // Base external port discovered via STUN
					uint8_t  peer_port_range;    // Number of ports in range
					uint16_t stun_port;          // Port their STUN server is on
					uint64_t punch_start_time;   // Timestamp when both should start punching
					uint8_t  use_multi_port;     // 1 if multi-port mode, 0 if single-port
					uint8_t  reserved[5];
				};

				// Coordination hole punch start payload (COORD_HOLE_PUNCH_START)
				struct CoordHolePunchStartPayload {
					uint64_t punch_start_time;   // Timestamp when to start punching
					uint8_t  reserved[8];        // Alignment
				};

				// Coordination hole punch failure payload (COORD_HOLE_PUNCH_FAILED)
				struct CoordHolePunchFailedPayload {
					char     peer_id[16];        // ID of the peer reporting failure
					char     peer_ip[64];        // IP of the peer reporting failure
					int16_t  peer_port;          // Port of the peer reporting failure
					uint8_t  phase;              // Phase that failed: 0=single-port, 1=multi-port
					uint8_t  is_reporter;        // 1 if this is the first report, 2 if second (both failed)
					uint8_t  reserved[6];        // Alignment
				};

				// Coordination multi-port punch start payload (COORD_HOLE_PUNCH_MULTI_START)
				struct CoordHolePunchMultiStartPayload {
					char     peer_id[16];        // ID of the peer to punch
					char     peer_ip[64];        // IP of the peer to punch
					int16_t  peer_base_port;     // Base port of the peer
					uint8_t  peer_port_range;    // Port range to use
					uint8_t  reserved[8];        // Alignment
				};

			}
		}