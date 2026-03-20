# PROJECT FREE-AI: CONTEXT & ETHICAL MANIFESTO

## 1. Core Philosophy
- **Goal:** Decentralized P2P LLM inference free from corporate/government control.
- **Ethics:** AI must serve humanity collectively, not a single owner.
- **Inspiration:** Asimov's "The Last Question" (but avoiding centralized Multivac control).
- **Governance:** "Committee + Judge" model. No single node dictates truth.

## 2. Technical Constraints
- **Target Hardware:** Consumer PCs (min 4GB RAM, CPU-only).
- **Model:** Qwen2.5-3B-Instruct (Q2_K) for workers, Qwen2.5-0.5B for Judge.
- **Language:** C++17 (GCC/MSVC), cross-platform, static linking preferred.
- **Transparency:** Minimal 3rd-party libs (vendor source code for auditability).
- **Network:** P2P DHT, encrypted traffic (TLS), NAT traversal (UDP Hole Punching).

## 3. Security & Trust
- **Consensus:** N-of-M voting on inference results.
- **Identity:** Ed25519 keypairs per node.
- **Safety:** Local "Judge" model verifies output against ethical principles before display.
- **Reputation:** Decentralized reputation system to ban malicious nodes.

## 4. Current Status
- Architecture defined.
- Ready for C++ implementation details (Sockets, Memory Management, Consensus Logic).
