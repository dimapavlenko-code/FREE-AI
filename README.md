# PROJECT FREE-AI: CONTEXT & ETHICAL MANIFESTO

## 1. Core Philosophy
- **Goal:** Decentralized P2P LLM inference free from corporate/government control.
- **Ethics:** AI must serve humanity collectively, not a single owner.
- **Inspiration:** Asimov's "The Last Question" (but avoiding centralized Multivac control).
- **Governance:** "Committee + Judge" model. No single node dictates truth.
- **Vision:** A "free" AI network where intelligence is distributed across thousands of consumer nodes, governed by consensus rather than corporate policy.

## 2. Technical Constraints
- **Target Hardware:** Consumer PCs (min 4GB RAM, CPU-only).
- **Model:** Qwen2.5-3B-Instruct (Q2_K) for workers, Qwen2.5-0.5B for Judge.
- **Language:** C++17 (GCC/MSVC), cross-platform, static linking preferred.
- **Transparency:** Minimal 3rd-party libs (vendor source code for auditability).
- **Network:** P2P DHT (Kademlia), encrypted traffic, NAT traversal (UDP Hole Punching).
- **Crypto:** mbedTLS 3.6.x (ECDSA secp256k1 for identity, SHA1 for DHT node IDs, ChaCha20/XOR for transport).

## 3. Security & Trust
- **Identity:** ECDSA secp256k1 keypairs per node (stored in `config.ini` as PEM).
- **Packet Signing:** Selective signing for critical packets (REGISTER, HANDSHAKE, INFERENCES).
- **Signature Verification:** Peers exchange public keys during registration; all signed packets verified.
- **DHT Routing:** 160 K-Buckets × 20 nodes = ~3200 max routing table size.
- **Node ID:** SHA1 hash of public key (160-bit).
- **Consensus:** N-of-M voting on inference results (planned).
- **Safety:** Local "Judge" model verifies output against ethical principles before display (planned).
- **Reputation:** Decentralized reputation system to ban malicious nodes (planned).
- **Super Node / Leaf Node:** Nodes self-classify based on NAT status (public IP = Super Node).

## 4. Current Status

### ✅ Completed (2026.03.21)
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Build System** | ✅ Complete | CMake, GCC/MSVC, mbedTLS 3.6.x integrated |
| **Network Init** | ✅ Complete | Winsock/POSIX abstraction |
| **TCP Sockets** | ✅ Complete | Blocking I/O, cross-platform |
| **UDP Sockets** | ✅ Complete | Non-blocking, for DHT/Bootstrap |
| **Config System** | ✅ Complete | INI parser/writer (multiline PEM, comment preservation) |
| **Thread Utils** | ✅ Complete | Priority control (Low for network) |
| **Identity** | ✅ Complete | ECDSA secp256k1 keygen, PEM load/save, sign/verify |
| **Packet Security** | ✅ Complete | XOR obfuscation, selective signing, ChaCha20 ready, nonce restoration |
| **Key Exchange** | ✅ Complete | Public keys exchanged via REGISTER/INTRO packets |
| **Signature Verification** | ✅ Complete | Peers verify each other's signed packets |
| **PeerManager** | ✅ Complete | Bootstrap, seed connection, Super/Leaf detection, DHT integration |
| **HolePunchManager** | ✅ Complete | NAT traversal with retry logic (5 attempts, 500ms intervals) |
| **UDP Hole Punching** | ✅ Complete | Direct P2P connections behind NAT |
| **DHT Routing Table** | ✅ Complete | Kademlia-based, 160 K-Buckets |
| **DHT Node ID** | ✅ Complete | SHA1 hash of public key |
| **DHT FIND_NODE** | ✅ Complete | Query closest nodes to target ID |
| **Protocol** | ✅ Complete | Secure packet header, DHT packet types, payload structures |

### 🚧 In Progress
| Component | Status | Notes |
| :--- | :--- | :--- |
| **DHT STORE/GET** | 🚧 Partial | Key-value storage logic implemented, needs testing |
| **DHT Integration** | 🚧 Partial | Packet routing in PeerManager, needs `SendDHTPacket()` implementation |
| **ChaCha20 Key Derivation** | 🚧 Planned | Replace hardcoded `0x42` placeholder with handshake-derived keys |
| **K-Bucket Eviction** | 🚧 Planned | Implement eviction policy for full buckets |

### 📋 Planned
| Component | Priority | Notes |
| :--- | :--- | :--- |
| **DHT Propagation Testing** | High | Verify 3+ nodes discover each other without seeds |
| **llama.cpp Integration** | High | Actual LLM inference engine |
| **Consensus Logic** | High | N-of-M voting for inference results |
| **Judge Model** | Medium | 0.5B safety verifier |
| **Reputation System** | Medium | Track node reliability |
| **RPC Protocol** | Medium | Distributed inference calls |
| **TLS Encryption** | Low | Additional transport layer security |

## 5. Repository Structure
FREE-AI/
├── src/
│ ├── main.cpp
│ ├── network/
│ │ ├── NetworkInit.cpp/hpp
│ │ ├── Socket.cpp/hpp
│ │ ├── UDPSocket.cpp/hpp
│ │ ├── PeerManager.cpp/hpp (DHT integrated, 2026.03.21)
│ │ ├── PacketSecurity.cpp/hpp (XOR + Signatures, nonce restoration)
│ │ ├── HolePunchManager.cpp/hpp (NAT traversal)
│ │ ├── DHT.cpp/hpp (Kademlia routing, 2026.03.21)
│ │ └── Protocol.hpp (Secure packet structures)
│ ├── crypto/
│ │ ├── Identity.cpp/hpp (ECDSA secp256k1)
│ │ └── mbedtls/ (vendored 3.6.x)
│ └── utils/
│ ├── Config.cpp/hpp (Multiline PEM, comments)
│ └── ThreadUtils.hpp (Priority control)
├── include/
├── config.ini (PEM keys, seed nodes, settings)
├── CMakeLists.txt
└── README.md (This Manifesto)


## 6. Ethical Commitments
1. **No Central Control:** No single entity can shut down or censor the network.
2. **Open Source:** All code is auditable. No hidden backdoors.
3. **User Sovereignty:** Users control their own nodes, keys, and data.
4. **Safety First:** The "Judge" model ensures outputs align with human welfare.
5. **Accessibility:** Designed to run on low-end hardware (4GB RAM CPUs).
6. **Copyleft Protection:** All derivatives must remain free and open (GNU GPL).
7. **Cryptographic Trust:** All peers verified via ECDSA signatures; no anonymous unverified nodes.

## 7. License & Contribution
- **Repository:** https://github.com/dimapavlenko-code/FREE-AI
- **License:** GNU General Public License v3.0 (GPL-3.0)
- **Contribution Guidelines:** All PRs must align with this Manifesto and be licensed under GPL-3.0.

## 8. Session History

### 2026.03.20
- mbedTLS 3.6.x integration (downgraded from 4.0.0)
- ECDSA secp256k1 identity system (fixed from Curve25519)
- Config system with multiline PEM support
- Packet security with XOR obfuscation and selective signing
- Manifesto v0.1.0 created

### 2026.03.21
- DHT (Kademlia) routing table implementation
- SHA1-based DHT node ID generation
- K-Bucket management (160 buckets × 20 nodes)
- DHT FIND_NODE packet handling
- PeerManager DHT integration
- Packet Security nonce restoration fixes
- REGISTER payload structure debugging (peer_id vs PEM key)
- Signature verification with peer public key exchange

## 9. Key Achievements

| Achievement | Impact |
| :--- | :--- |
| **ECDSA secp256k1 Identity** | Cryptographically secure node identity (fixed Curve25519 signing issue) |
| **Selective Packet Signing** | Critical packets signed; lightweight packets unsigned (performance balance) |
| **XOR Obfuscation + Nonce** | DPI-resistant transport with replay protection |
| **Public Key Exchange** | Peers exchange keys via REGISTER; all subsequent packets verified |
| **UDP Hole Punching** | Direct P2P connections behind NAT (no port forwarding required) |
| **Kademlia DHT** | Decentralized peer discovery beyond seed nodes |
| **Thread-Safe Architecture** | Separate mutexes for peers, keys, and routing table |
| **Cross-Platform Build** | GCC and MSVC supported; static linking preferred |

## 10. Next Milestones

1. **🧪 DHT Propagation Test** – Run 3+ nodes, verify they discover each other without seeds
2. **🔑 ChaCha20 Key Derivation** – Replace hardcoded encryption key with handshake-derived key
3. **🤖 llama.cpp Integration** – Load Qwen2.5-3B and run actual inference
4. **⚖️ Consensus Logic** – Implement N-of-M voting for inference results
5. **👁️ Judge Model** – Integrate Qwen2.5-0.5B for safety verification

---

*Last Updated: 2026.03.21*
*Version: 0.2.0*
*License: GPL-3.0*