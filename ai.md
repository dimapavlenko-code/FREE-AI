# FREE-AI — Agent Reference Guide

## Project Overview

Decentralized P2P LLM inference network in C++17. Each node runs Qwen2.5-0.5B with its own identity, memory, and persistent state. Nodes discover via Kademlia DHT, traverse NAT with UDP hole punching, communicate over encrypted channels.

**License:** GPL-3.0

---

## ⚠️ ALWAYS READ CMakeLists.txt BEFORE ADDING FILES

New files must be added to `SOURCES`/`INFERENCE_SOURCES` and `HEADERS`/`INFERENCE_HEADERS` lists. Defines include dirs, linking (`mbedcrypto`, `mbedx509`, `llama`), GPU options (`LLAMA_VULKAN`), compiler flags.

---

## Build

- CMake 3.20+, C++17, Windows/Linux
- Vendored: [`src/crypto/mbedtls/`](src/crypto/mbedtls/) (Ed25519/ChaCha20), [`thirdparty/llama.cpp/`](thirdparty/llama.cpp/) (GGUF inference), [`src/compression/minilzo/`](src/compression/minilzo/) (LZO1X)
- Platform libs: `ws2_32 wsock32 shell32` (Win), `pthread` (Linux)

---

## Source Code Structure

### Headers (`include/`)

| File | Purpose |
|------|---------|
| `include/crypto/Identity.hpp` | Ed25519 keypair — gen, PEM, sign/verify |
| `include/inference/InferenceEngine.hpp` | High-level API — wraps LlamaContext + MemoryManager, `Infer()` |
| `include/inference/LlamaContext.hpp` | llama.cpp wrapper — model load, tokenize, generate |
| `include/inference/MemoryManager.hpp` | Persistent memory — identity/session/conversation/archive, token counting |
| `include/inference/ModelConfig.hpp` | Config struct — model path, ctx size, GPU layers, temperature, memory ratios |
| `include/inference/ContextBuilder.hpp` | System prompt assembly, memory status block, numbered context |
| `include/network/DHT.hpp` | Kademlia DHT — NodeId (SHA1), KBucket (20), DHTRoutingTable (160), FIND_NODE/STORE/GET/PING |
| `include/network/HolePunchManager.hpp` | NAT traversal — single/multi-port UDP punching, STUN coordination |
| `include/network/ISocket.hpp` | Abstract socket interface (legacy, unused) |
| `include/network/NetworkInit.hpp` | Winsock/POSIX init, STUN server start/stop |
| `include/network/PacketSecurity.hpp` | Sign-compress-encrypt pipeline — ECDSA, MiniLZO, ChaCha20 |
| `include/network/PeerConnectionTracker.hpp` | Connection state + exponential backoff for seed registration |
| `include/network/PeerManager.hpp` | Main network coordinator — seed reg, DHT, hole punching, message dispatch |
| `include/network/Protocol.hpp` | 20+ packet types, SecurePacketHeader (20B), registration handshake, payloads |
| `include/network/Socket.hpp` | TCP socket — cross-platform Create/Connect/Bind/Send/Receive |
| `include/network/STUNParser.hpp` | STUN Binding Response parser — XOR-MAPPED-ADDRESS per RFC 5389 |
| `include/network/UDPSocket.hpp` | UDP socket — SendTo/ReceiveFrom, non-blocking for hole punching |
| `include/utils/Config.hpp` | INI parser — multiline PEM, comment preservation, ModelConfig extraction |
| `include/utils/Helpers.hpp` | `Trim()`, `TrimNulls()`, `fai_strncpy()` |
| `include/utils/ThreadUtils.hpp` | Thread priority helpers (Win/POSIX) |
| `include/utils/TimeUtils.hpp` | `Now()`, `NowEpoch()`, `NowFormatted()`, `DurationMs()`, `DurationUs()` |

### Sources (`src/`)

| File | Purpose |
|------|---------|
| `src/main.cpp` | Entry — net init, config, identity, PeerManager, InferenceEngine, CLI loop |
| `src/inference/InferenceEngine.cpp` | Engine init, `Infer()` with context building |
| `src/inference/LlamaContext.cpp` | llama.cpp lifecycle, tokenize, generate, commands |
| `src/inference/MemoryManager.cpp` | Memory persistence, archive I/O, token counting, statements, commands |
| `src/inference/ModelConfig.cpp` | INI → ModelConfig loading |
| `src/inference/ContextBuilder.cpp` | Prompt assembly, status block, numbered context |
| `src/network/DHT.cpp` | Kademlia — SHA1 node ID, buckets, FIND_NODE/STORE/GET/PING |
| `src/network/HolePunchManager.cpp` | NAT punching — single/multi-port, STUN, auto fallback |
| `src/network/NetworkInit.cpp` | Winsock/POSIX init, STUN socket |
| `src/network/PacketSecurity.cpp` | Sign-compress-encrypt / decrypt-decompress-verify pipeline |
| `src/network/PeerConnectionTracker.cpp` | Exponential backoff state machine, recovery |
| `src/network/PeerManager.cpp` | Network coordinator — seed reg, DHT, punching, dispatch |
| `src/network/Socket.cpp` | TCP socket impl |
| `src/network/STUNParser.cpp` | STUN parser — XOR-MAPPED-ADDRESS |
| `src/network/UDPSocket.cpp` | UDP socket impl |
| `src/crypto/Identity.cpp` | Ed25519 — key gen, PEM, sign/verify |
| `src/utils/Config.cpp` | INI parser impl |
| `src/utils/Helpers.cpp` | `Trim()`, `TrimNulls()` |
| `src/compression/minilzo/minilzo.c` | MiniLZO LZO1X impl |
| `src/compression/minilzo/minilzo.h` | MiniLZO header |
| `src/compression/minilzo/lzoconf.h` | LZO config |
| `src/compression/minilzo/lzodefs.h` | LZO defs |

### Other

| Path | Purpose |
|------|---------|
| `tests/test_main.cpp` | Unit test entry |
| `info/config*.ini` | Sample configs (3 instances) |
| `plans/*.md` | Architecture documents |

---

## Code Style

- **Type shortcuts:** `using LockGuard = std::lock_guard<std::mutex>;`
- **C-style casts** — `(int)x` not `static_cast<int>(x)`
- **Namespaces:** `FreeAI::Crypto`, `FreeAI::Network`, `FreeAI::Inference`, `FreeAI::Utils`


---

## Architecture

```
main.cpp
  ├── NetworkEnvironment::Init()     # Winsock/STUN
  ├── Config                         # INI loader
  ├── Identity                       # Ed25519
  ├── PeerManager                    # Network coord
  │   ├── DHT → HolePunchManager     # Discovery + NAT
  │   ├── PeerConnectionTracker      # Backoff state
  │   └── PacketSecurity             # Sign/encrypt
  └── InferenceEngine                # LLM inference
      ├── LlamaContext               # llama.cpp
      ├── MemoryManager              # Persistence
      └── ContextBuilder             # Prompt assembly
```

**Data flow:** PeerManager → DHT → seed registration → HolePunchManager → UDP direct | InferenceEngine → ContextBuilder → LlamaContext → llama.cpp | MemoryManager → disk (identity/session/conversation/archive)

