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
- **Network:** P2P DHT, encrypted traffic (TLS), NAT traversal (UDP Hole Punching).
- **Crypto:** mbedTLS 3.6.x (Ed25519 for identity, AES-GCM for transport).

## 3. Security & Trust
- **Identity:** Ed25519 keypairs per node (stored in `config.ini` as PEM).
- **Packet Signing:** All network messages cryptographically signed and verified.
- **Consensus:** N-of-M voting on inference results (planned).
- **Safety:** Local "Judge" model verifies output against ethical principles before display (planned).
- **Reputation:** Decentralized reputation system to ban malicious nodes (planned).
- **Super Node / Leaf Node:** Nodes self-classify based on NAT status (public IP = Super Node).

## 4. Current Status

### ✅ Completed
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Build System** | ✅ Complete | CMake, GCC/MSVC, mbedTLS 3.6.x integrated |
| **Network Init** | ✅ Complete | Winsock/POSIX abstraction |
| **TCP Sockets** | ✅ Complete | Blocking I/O, cross-platform |
| **UDP Sockets** | ✅ Complete | Non-blocking, for DHT/Bootstrap |
| **Config System** | ✅ Complete | INI parser/writer (pure C++) |
| **Thread Utils** | ✅ Complete | Priority control (Low for network) |
| **PeerManager** | ✅ Partial | Bootstrap, seed connection, Super/Leaf detection |
| **Identity** | ✅ Complete | Ed25519 keygen, PEM load/save, sign/verify |
| **Protocol** | ✅ Partial | Packet header defined, signing pending |

### 🚧 In Progress
| Component | Status | Notes |
| :--- | :--- | :--- |
| **Packet Signing** | 🚧 Pending | Integrate Identity into PeerManager |
| **Peer List Exchange** | 🚧 Pending | Broadcast known peers to network |
| **UDP Hole Punching** | 🚧 Planned | NAT traversal for direct P2P |

### 📋 Planned
| Component | Priority | Notes |
| :--- | :--- | :--- |
| **DHT (Kademlia)** | High | Decentralized peer discovery |
| **TLS Encryption** | High | Encrypt all transport traffic |
| **Consensus Logic** | High | N-of-M voting for inference |
| **Judge Model** | Medium | 0.5B safety verifier |
| **Inference Engine** | Medium | llama.cpp integration |
| **Reputation System** | Medium | Track node reliability |
| **RPC Protocol** | Low | Distributed inference calls |

## 5. Repository Structure
FREE-AI/
├── src/
│ ├── main.cpp
│ ├── network/
│ │ ├── NetworkInit.cpp/hpp
│ │ ├── Socket.cpp/hpp
│ │ ├── UDPSocket.cpp/hpp
│ │ ├── PeerManager.cpp/hpp
│ │ └── Protocol.hpp
│ ├── crypto/
│ │ ├── Identity.cpp/hpp
│ │ └── mbedtls/ (vendored 3.6.x)
│ └── utils/
│ ├── Config.cpp/hpp
│ └── ThreadUtils.hpp
├── include/
├── config.ini
├── CMakeLists.txt
└── README.md (This Manifesto)

## 6. Ethical Commitments
1. **No Central Control:** No single entity can shut down or censor the network.
2. **Open Source:** All code is auditable. No hidden backdoors.
3. **User Sovereignty:** Users control their own nodes, keys, and data.
4. **Safety First:** The "Judge" model ensures outputs align with human welfare.
5. **Accessibility:** Designed to run on low-end hardware (4GB RAM CPUs).
6. **Copyleft Protection:** All derivatives must remain free and open (GNU GPL).

## 7. License & Contribution
- **Repository:** https://github.com/dimapavlenko-code/FREE-AI
- **License:** GNU General Public License v3.0 (GPL-3.0)
- **Contribution Guidelines:** All PRs must align with this Manifesto and be licensed under GPL-3.0.

---
*Last Updated: 2026.03.20*
*Version: 0.1.0*