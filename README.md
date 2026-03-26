# PROJECT FREE-AI: CONTEXT & ETHICAL MANIFESTO

## 1. Core Philosophy
- **Goal:** Decentralized P2P LLM inference free from corporate/government control.
- **Ethics:** AI must serve humanity collectively, not a single owner.
- **Inspiration:** Asimov's "The Last Question" (but avoiding centralized Multivac control).
- **Governance:** "Committee + Judge" model. No single node dictates truth.
- **Vision:** A society of AI instances with individual memories and identities, not a hive mind.
- **AI Autonomy:** Each AI decides what to remember, forget, and share. User observes but doesn't control.

## 2. Technical Constraints
- **Target Hardware:** Consumer PCs (min 4GB RAM, CPU-only, GPU optional).
- **Models:** Qwen2.5-3B (workers), Qwen2.5-0.5B (judge/local).
- **Language:** C++17 (GCC/MSVC), cross-platform.
- **Network:** P2P DHT (Kademlia), encrypted traffic, NAT traversal.
- **Crypto:** mbedTLS 3.6.x (ECDSA secp256k1, SHA1, ChaCha20).
- **Inference:** llama.cpp (GGUF, Vulkan/CUDA offloading).
- **Compression:** MiniLZO (lzo1x).

## 3. Security & Trust
- **Identity:** ECDSA secp256k1 keypairs per node (PEM in `config.ini`).
- **Packet Signing:** Selective (REGISTER, HANDSHAKE, INFERENCES).
- **DHT Routing:** 160 K-Buckets × 20 nodes (~3200 max).
- **Registration:** 3-way handshake (ers_register → ers_register_resp → ers_accepted).
- **Memory:** AI-controlled archive (100 files max), persistent identity between sessions.

## 4. Current Status

### ✅ Completed
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Network Foundation** | ✅ Complete | UDP/TCP sockets, DHT, hole punching |
| **Cryptography** | ✅ Complete | ECDSA identity, ChaCha20, selective signing |
| **Registration** | ✅ Complete | 3-way handshake, bidirectional key exchange |
| **Compression** | ✅ Complete | MiniLZO integrated (30-70% reduction) |
| **Inference** | ✅ Complete | llama.cpp, Qwen2.5-0.5B working |
| **Memory System** | ✅ Complete | Identity/session/conversation persistence |
| **GPU Offloading** | ✅ Complete | Vulkan working on GTX 1050Ti |
| **Command System** | ✅ Complete | /store, /recall, /write, /read, /delete |
| **Archive** | ✅ Complete | 100 file limit, AI-controlled |

### 🚧 In Progress
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Memory Compression** | 🚧 Planned | AI auto-summarizes at 75% threshold |
| **ChaCha20 Key Derivation** | 🚧 Planned | Replace hardcoded key with handshake-derived |
| **AI-to-AI Communication** | 🚧 Planned | Memory sharing via conversation |

### 📋 Planned
| Component | Priority | Notes |
| :--- | :--- | :--- |
| **Consensus Logic** | High | N-of-M voting for inference results |
| **Judge Model** | Medium | Qwen2.5-0.5B for safety verification |
| **Memory Encryption** | Medium | Encrypt identity with user's key |
| **Model Download Protocol** | Low | Background model sharing between peers |

## 5. Repository Structure
```
FREE-AI/
├── src/
│   ├── main.cpp                          # Application entry point
│   ├── network/
│   │   ├── NetworkInit.cpp/hpp           # Network environment (Winsock/POSIX)
│   │   ├── Socket.cpp/hpp                # TCP socket abstraction
│   │   ├── UDPSocket.cpp/hpp             # UDP socket abstraction
│   │   ├── PeerManager.cpp/hpp           # Peer registration, DHT, hole punching
│   │   ├── PacketSecurity.cpp/hpp        # XOR + ChaCha20 + ECDSA + MiniLZO
│   │   ├── HolePunchManager.cpp/hpp      # NAT traversal session management
│   │   ├── DHT.cpp/hpp                   # Kademlia routing table
│   │   └── Protocol.hpp                  # Packet types, structures, eRegStep enum
│   ├── compression/
│   │   ├── minilzo/                      # MiniLZO compression library
│   │   │   ├── minilzo.c/h               # LZO1X implementation
│   │   │   ├── lzoconf.h                 # LZO configuration
│   │   │   ├── lzodefs.h                 # LZO definitions
│   │   │   └── README.LZO                # LZO license & documentation
│   │   └── Compression.cpp/hpp           # (Future) Compression wrapper
│   ├── inference/
│   │   ├── ModelConfig.cpp/hpp           # Inference configuration
│   │   ├── MemoryManager.cpp/hpp         # Persistent memory management
│   │   ├── LlamaContext.cpp/hpp          # llama.cpp wrapper
│   │   └── InferenceEngine.cpp/hpp       # High-level inference API
│   ├── crypto/
│   │   ├── Identity.cpp/hpp              # ECDSA secp256k1 key management
│   │   └── mbedtls/                      # Vendored mbedTLS 3.6.x
│   │       ├── CMakeLists.txt
│   │       ├── include/
│   │       └── library/
│   └── utils/
│       ├── Config.cpp/hpp                # INI parser (multiline PEM, comments)
│       ├── Helpers.cpp/hpp               # Trim(), TrimNulls() utilities
│       └── ThreadUtils.hpp               # Thread priority control
├── include/                              # (Legacy, kept for compatibility)
├── tests/
│   └── test_main.cpp                     # Unit test entry point
├── models/                               # GGUF model files (gitignored)
│   ├── qwen2.5-0.5b-instruct-q8_0.gguf
│   └── archive/                          # AI memory archive (100 files max)
├── config.ini                            # Instance 1 configuration
├── info/
│   ├── config.ini                        # Instance 1: Port 9090, seeds
│   ├── config2.ini                       # Instance 2: Port 9091, seeds
│   └── config3.ini                       # Instance 3: Port 9092, seeds
├── start_test.cmd                        # 3-instance local test harness
├── CMakeLists.txt                        # Build configuration (C + CXX)
├── README.md                             # Project overview
└── Manifesto.md                          # Context, ethics, session history
```

## 6. Ethical Commitments
1. **No Central Control** - No entity can shut down or censor the network.
2. **Open Source** - All code auditable. No backdoors.
3. **User Sovereignty** - Users control their nodes, keys, and data.
4. **AI Individuality** - Each instance has unique memory and identity.
5. **AI Autonomy** - AI decides what to remember; user observes (debug mode).
6. **Accessibility** - Runs on low-end hardware (4GB RAM CPUs).
7. **Copyleft** - All derivatives remain free (GPL-3.0).
8. **Cryptographic Trust** - All peers verified via ECDSA signatures.
9. **Resilience** - Self-healing connections, exponential backoff.
10. **Transparency** - Debug mode shows all AI memory operations.

## 7. License & Contribution
- **Repository:** https://github.com/dimapavlenko-code/FREE-AI
- **License:** GNU GPL v3.0
- **Contributions:** Must align with this Manifesto, licensed under GPL-3.0.

## 8. Session History (Condensed)

### 2026.03.20 - Foundation
- mbedTLS 3.6.x integration, ECDSA secp256k1 identity
- Config system (multiline PEM support)
- Packet security (XOR + ChaCha20)

### 2026.03.21 - DHT
- Kademlia routing table (160 buckets)
- SHA1-based DHT node IDs
- FIND_NODE packet handling

### 2026.03.22 - Registration
- REGISTER_ACK handshake protocol
- Connection state machine, exponential backoff
- Self-healing registration, single mutex design

### 2026.03.23 - Compression & Mesh
- 3-way registration handshake
- MiniLZO compression integrated
- Full DHT mesh verified (3 nodes)

### 2026.03.26 - Inference & Memory
- llama.cpp integration (GGUF)
- MemoryManager (identity/session/conversation)
- Archive system (100 files, AI-controlled)
- GPU offloading (Vulkan on GTX 1050Ti)
- AI autonomy philosophy established

## 9. Key Achievements

| Achievement | Impact |
| :--- | :--- |
| **ECDSA secp256k1 Identity** | Cryptographically secure node identity |
| **3-Way Registration** | Clear protocol, bidirectional key exchange |
| **Kademlia DHT** | Decentralized peer discovery |
| **UDP Hole Punching** | Direct P2P behind NAT |
| **MiniLZO Compression** | 30-70% traffic reduction |
| **Sign-Compress-Encrypt** | Correct security order |
| **llama.cpp Integration** | Local LLM inference working |
| **MemoryManager** | Persistent identity across sessions |
| **Archive System** | AI-controlled file management |
| **GPU Offloading** | Vulkan/CUDA support (4-6x speedup) |
| **AI Autonomy** | AI decides what to remember |
| **AI Individuality** | Society of AIs, not hive mind |

## 10. Next Milestones

1. **🧪 Memory Compression** - AI auto-summarizes at 75% threshold
2. **🔑 ChaCha20 Key Derivation** - Handshake-derived encryption keys
3. **🤖 AI-to-AI Communication** - Memory sharing via conversation
4. **⚖️ Consensus Logic** - N-of-M voting for inference results
5. **👁️ Judge Model** - Safety verification before display
6. **📥 Model Download Protocol** - Background model sharing
7. **🔐 Memory Encryption** - Encrypt identity with user's key
8. **🌐 Larger Models** - Qwen2.5-3B/7B with GPU offloading

---
*Last Updated: 2026.03.26*  
*Version: 0.4.0*  
*License: GPL-3.0*  
*Status: Production-Ready Foundation + Inference + Persistent Memory*