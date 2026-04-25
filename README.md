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
- **Network:** P2P DHT (Kademlia), encrypted traffic, NAT traversal (STUN + UDP hole punching).
- **Crypto:** mbedTLS 3.6.x (Ed25519/ECDSA, SHA1, ChaCha20).
- **Inference:** llama.cpp (GGUF, Vulkan/CUDA offloading).
- **Compression:** MiniLZO (lzo1x).

## 3. Security & Trust
- **Identity:** Ed25519 keypairs per node (PEM in `config.ini`), generated via mbedTLS.
- **Packet Signing:** Selective (REGISTER, HANDSHAKE, INFERENCES) via `PacketSecurity::PrepareOutgoing()`.
- **DHT Routing:** 160 K-Buckets × 20 nodes (~3200 max), SHA1-based node IDs from public keys.
- **Registration:** 3-way handshake (ers_register → ers_register_resp → ers_accepted), exponential backoff.
- **Memory:** AI-controlled archive (100 files max), persistent identity between sessions.
- **Encryption:** ChaCha20 stream cipher with 12-byte IV, XOR obfuscation on magic number.

## 4. Current Status

### ✅ Completed
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Network Foundation** | ✅ Complete | UDP/TCP sockets (`Socket.hpp`, `UDPSocket.hpp`), `NetworkInit.hpp` |
| **DHT (Kademlia)** | ✅ Complete | `DHT.hpp/cpp` - NodeId, KBucket, DHTRoutingTable, FIND_NODE, STORE/GET, PING, bucket refresh |
| **Hole Punching** | ✅ Complete | `HolePunchManager.hpp/cpp` - Single-port, multi-port, STUN server, automatic strategy selection |
| **Protocol** | ✅ Complete | `Protocol.hpp` - 20+ packet types, SecurePacketHeader, registration handshake, DHT payloads |
| **Identity (Ed25519)** | ✅ Complete | `Identity.hpp/cpp` - Key generation, PEM import/export, signing, verification via mbedTLS |
| **Packet Security** | ✅ Complete | `PacketSecurity.hpp/cpp` - Sign-compress-encrypt pipeline, ChaCha20, MiniLZO |
| **Registration** | ✅ Complete | 3-way handshake (ers_register → ers_register_resp → ers_accepted), bidirectional key exchange |
| **Peer Management** | ✅ Complete | `PeerManager.hpp/cpp` - Seed registration, exponential backoff, peer list, DHT integration |
| **Compression** | ✅ Complete | MiniLZO (LZO1X) integrated in `src/compression/minilzo/` |
| **Inference Engine** | ✅ Complete | `InferenceEngine.hpp/cpp` - High-level API, `LlamaContext.hpp/cpp` wraps llama.cpp |
| **Model Config** | ✅ Complete | `ModelConfig.hpp` - Qwen2.5-0.5B defaults, GPU offload settings, context ratios |
| **Memory Manager** | ✅ Complete | `MemoryManager.hpp/cpp` - Identity/session/conversation persistence, archive (100 files), token counting |
| **Config System** | ✅ Complete | `Config.hpp/cpp` - INI parser with multiline PEM support, comment preservation |
| **GPU Backend** | ✅ Configured | Vulkan enabled in CMake (`LLAMA_VULKAN ON`), `n_gpu_layers` configurable |
| **mbedTLS** | ✅ Vendored | 3.6.x in `src/crypto/mbedtls/`, ECDSA/Ed25519, ChaCha20, SHA1 |
| **Statement-Based Context** | ✅ Complete | `Statement` struct with ID/timestamp/type, `BuildStatementContext()`, `UpdateSystemStatus()` for KV cache optimization |
| **context_rewrite Commands** | ✅ Complete | Range format (`compress S1-001 to S1-005 with: summary`) and comma-separated format (`compress S1-001, S1-002, S1-003 with: summary`) |
| **Re-Invocation Loop** | ✅ Complete | AI can manage memory across 20 housekeeping rounds with automatic tool report injection |

### ⚠️ Partially Implemented
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Inference Integration** | ⚠️ Partial | Engine code exists but `g_inferenceEngine.Initialize()` is commented out in [`main.cpp:125`](src/main.cpp:125) |
| **Command System** | ⚠️ Partial | Console handlers not implemented; statement commands work via `context_rewrite` in inference loop |
| **ContextBuilder Refactoring** | ⚠️ Partial | Standalone class implemented (80%), MemoryManager delegation pending (see [`context-builder-refactoring-remaining.md`](plans/context-builder-refactoring-remaining.md)) |
| **forget Command Cleanup** | ⚠️ Partial | `ApplyForgetCommand` and `is_forget` fields still in code, need removal (see [`comma-separated-statements-implementation.md`](plans/comma-separated-statements-implementation.md)) |

### 📋 Planned
| Component | Priority | Notes |
| :--- | :--- | :--- |
| **Consensus Logic** | High | N-of-M voting for inference results |
| **Judge Model** | Medium | Qwen2.5-0.5B for safety verification |
| **Memory Encryption** | Medium | Encrypt identity with user's key |
| **Model Download Protocol** | Low | Background model sharing between peers |
| **AI-to-AI Communication** | Medium | Memory sharing via conversation |

## 5. Repository Structure
```
FREE-AI/
├── CMakeLists.txt                        # Build config (C++17, mbedTLS, llama.cpp subdirs)
├── CMakeSettings.json                    # VS Code CMake settings
├── .gitignore                            # Git ignore rules
├── LICENSE                               # GPL-3.0 license
├── README.md                             # Project overview & status
├── launch.vs.json                        # VS Code launch configuration
├── .vscode/                              # VS Code workspace settings
│
├── include/                              # Public headers
│   ├── crypto/
│   │   └── Identity.hpp                  # Ed25519 key management
│   ├── inference/
│   │   ├── ContextBuilder.hpp            # System prompt assembly, memory status, numbered context
│   │   ├── InferenceEngine.hpp           # High-level inference API
│   │   ├── LlamaContext.hpp              # llama.cpp wrapper
│   │   ├── MemoryManager.hpp             # Persistent memory management
│   │   └── ModelConfig.hpp               # Inference configuration struct
│   ├── network/
│   │   ├── DHT.hpp                       # Kademlia DHT (NodeId, KBucket, DHTRoutingTable)
│   │   ├── HolePunchManager.hpp          # NAT traversal (single/multi-port, STUN)
│   │   ├── ISocket.hpp                   # Socket interface (not currently used)
│   │   ├── NetworkInit.hpp               # Winsock/POSIX initialization
│   │   ├── PacketSecurity.hpp            # Sign-compress-encrypt pipeline
│   │   ├── PeerConnectionTracker.hpp     # Connection state + exponential backoff
│   │   ├── PeerManager.hpp               # Peer registration, DHT mesh
│   │   ├── Protocol.hpp                  # Packet types, payloads, constants
│   │   ├── Socket.hpp                    # TCP socket abstraction
│   │   ├── STUNParser.hpp                # STUN Binding Response parser (RFC 5389)
│   │   └── UDPSocket.hpp                 # UDP socket abstraction
│   └── utils/
│       ├── Config.hpp                    # INI parser (multiline PEM support)
│       ├── Helpers.hpp                   # Trim(), TrimNulls() utilities
│       └── ThreadUtils.hpp               # Thread priority control
│
├── src/
│   ├── main.cpp                          # Application entry point
│   │
│   ├── compression/
│   │   └── minilzo/                      # MiniLZO LZO1X compression
│   │       ├── minilzo.c/h               # Implementation
│   │       ├── lzoconf.h                 # Configuration
│   │       ├── lzodefs.h                 # Definitions
│   │       └── README.LZO                # License
│   │
│   ├── crypto/
│   │   ├── Identity.cpp                  # Ed25519 implementation
│   │   └── mbedtls/                      # Vendored mbedTLS 3.6.x
│   │       ├── CMakeLists.txt
│   │       ├── include/
│   │       └── library/
│   │
│   ├── inference/
│   │   ├── ContextBuilder.cpp            # Prompt assembly, status block, numbered context
│   │   ├── InferenceEngine.cpp           # Inference engine implementation
│   │   ├── LlamaContext.cpp              # llama.cpp wrapper
│   │   ├── MemoryManager.cpp             # Memory persistence
│   │   └── ModelConfig.cpp               # Config loading
│   │
│   ├── network/
│   │   ├── DHT.cpp                       # Kademlia implementation
│   │   ├── HolePunchManager.cpp          # NAT traversal
│   │   ├── NetworkInit.cpp               # Network init/shutdown
│   │   ├── PacketSecurity.cpp            # Security pipeline
│   │   ├── PeerConnectionTracker.cpp     # Exponential backoff state machine
│   │   ├── PeerManager.cpp               # Network coordinator
│   │   ├── Socket.cpp                    # TCP socket impl
│   │   ├── STUNParser.cpp                # STUN parser - XOR-MAPPED-ADDRESS
│   │   ├── UDPSocket.cpp                 # UDP socket impl
│   │   └── config.ini                    # Network config
│   │
│   └── utils/
│       ├── Config.cpp                    # INI parser
│       └── Helpers.cpp                   # String utilities
│
├── tests/
│   └── test_main.cpp                     # Unit test entry point
│
├── models/                               # GGUF models (gitignored)
│   └── qwen2.5-0.5b-instruct-q8_0.gguf
│
├── info/
│   ├── config.ini                        # Instance 1 config
│   ├── config2.ini                       # Instance 2 config
│   ├── config3.ini                       # Instance 3 config
│   └── chat-export-*.json                # Chat export logs
│
├── start_test.cmd                        # 3-instance local test harness
└── plans/
    ├── comma-separated-statements-implementation.md  # Statement command implementation
    ├── context-builder-refactoring-plan.md           # ContextBuilder refactoring plan
    ├── context-builder-refactoring-remaining.md      # Remaining refactoring tasks
    ├── memory-compression-system.md                  # Memory compression design
    ├── memory-compression-system-v2.md               # Memory compression v2 design
    └── udp-hole-punching-review.md                   # NAT traversal architecture review
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
- mbedTLS 3.6.x integration, Ed25519 identity (not ECDSA secp256k1)
- Config system (multiline PEM support)
- Packet security (XOR obfuscation + ChaCha20 + ECDSA signing)

### 2026.03.21 - DHT
- Kademlia routing table (160 K-Buckets, 20 entries each)
- SHA1-based DHT node IDs from public keys
- FIND_NODE packet handling with response callback

### 2026.03.22 - Registration
- 3-way registration handshake (ers_register → ers_register_resp → ers_accepted)
- Connection state machine (Disconnected → Connecting → Connected/Failed)
- Exponential backoff for seed registration
- Self-healing registration with single mutex design

### 2026.03.23 - Compression & Mesh
- MiniLZO (LZO1X) compression integrated
- Full DHT mesh support (FIND_NODE, STORE, GET_VALUE, PING)
- Peer public key exchange during registration

### 2026.03.26 - Inference & Memory
- llama.cpp integration (GGUF loading, tokenization, generation)
- MemoryManager (identity/session/conversation persistence)
- Archive system (100 files max, AI-controlled)
- GPU offloading configuration (Vulkan enabled in CMake)
- AI autonomy philosophy established

### 2026.04.xx - Hole Punching & NAT Traversal
- Single-port UDP hole punching
- Multi-port hole punching (5-port ranges)
- STUN server implementation for external address discovery
- Automatic strategy selection (single → multi-port fallback)
- Failure report coordination between peers

### 2026.04.24-25 - Statement-Based Memory System
- `Statement` struct with ID (S1-001 format), timestamp, type (USER/ASSISTANT/TOOL_REPORT/TOOL_ERROR/CODE/SUMMARY)
- `context_rewrite` command system with range and comma-separated statement ID support
- `BuildStatementContext()` - renders context with statement IDs and timestamps
- `UpdateSystemStatus()` - KV cache optimization via dynamic status injection at context end
- `ExtractStatementCommands()` / `FilterStatementCommands()` - regex-based command parsing
- `ApplyCompressCommand()` / `ApplyDeleteCommand()` / `ApplyCompressIDs()` / `ApplyDeleteIDs()` - statement manipulation
- `FindStatementRange()` - ID resolution helper
- Re-invocation loop in `LlamaContext::Generate()` (max 20 housekeeping rounds)
- Tool report injection after command execution
- ContextBuilder class extraction (80% complete, delegation pending)

## 9. Key Achievements

| Achievement | Impact |
| :--- | :--- |
| **Ed25519 Identity** | Cryptographically secure node identity via mbedTLS |
| **3-Way Registration** | Clear protocol, bidirectional key exchange |
| **Kademlia DHT** | Decentralized peer discovery (160 buckets, FIND_NODE, STORE/GET) |
| **UDP Hole Punching** | Direct P2P behind NAT (single-port, multi-port, STUN coordination) |
| **MiniLZO Compression** | 30-70% traffic reduction |
| **Sign-Compress-Encrypt** | Correct security order in PacketSecurity pipeline |
| **llama.cpp Integration** | LLM inference framework (model loading, tokenization, generation) |
| **MemoryManager** | Persistent identity across sessions (identity/session/conversation/archive) |
| **Archive System** | AI-controlled file management (100 files max) |
| **GPU Offloading** | Vulkan configuration ready (n_gpu_layers configurable) |
| **AI Autonomy** | AI decides what to remember |
| **AI Individuality** | Society of AIs, not hive mind |
| **Statement-Based Context** | AI-driven memory management with unique IDs (S1-001 format) |
| **context_rewrite Commands** | Range and comma-separated statement manipulation |
| **Re-Invocation Loop** | AI can manage memory across 20 housekeeping rounds |
| **KV Cache Optimization** | Dynamic system status injection at context end |

## 10. Next Milestones

1. **🔧 Fix context_rewrite Regex** - Update regex patterns to require `context_rewrite` mandatory prefix (see [`comma-separated-statements-implementation.md`](plans/comma-separated-statements-implementation.md))
2. **🏗️ Complete ContextBuilder Refactoring** - Remove inline class from MemoryManager.hpp, delegate methods (see [`context-builder-refactoring-remaining.md`](plans/context-builder-refactoring-remaining.md))
3. **🧹 Remove forget Command Remnants** - Delete `ApplyForgetCommand`, `is_forget`, `forget_before` from codebase
4. **✅ Enable Inference Engine** - Uncomment `g_inferenceEngine.Initialize()` in [`main.cpp:125`](src/main.cpp:125)
5. **🔑 ChaCha20 Key Derivation** - Handshake-derived encryption keys
6. **🤖 AI-to-AI Communication** - Memory sharing via conversation
7. **⚖️ Consensus Logic** - N-of-M voting for inference results
8. **👁️ Judge Model** - Safety verification before display
9. **📥 Model Download Protocol** - Background model sharing
10. **🔐 Memory Encryption** - Encrypt identity with user's key
11. **🌐 Larger Models** - Qwen2.5-3B/7B with GPU offloading

---
*Last Updated: 2026.04.25*
*Version: 0.6.0*
*License: GPL-3.0*
*Status: Network Foundation Complete + Inference Framework + Statement-Based Memory System*