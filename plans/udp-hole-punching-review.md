# UDP Hole Punching - Remaining Issues for Implementation

> **Status:** 16 of 17 issues have been fixed. Only 1 issue remains actionable.
> 
> **Last Updated:** 2026-04-23

---

## Remaining Actionable Issues

### 1. STUN Message-Integrity Attribute Missing (Low Severity - Optional)

**Files:** [`HolePunchManager.cpp:156-212`](src/network/HolePunchManager.cpp:156) (server side), [`HolePunchManager.cpp:289-387`](src/network/HolePunchManager.cpp:289) (client side)

**Current State:** The STUN implementation does not include the STUN Message-Integrity attribute. Any peer can spoof STUN responses.

**Impact:** Security vulnerability - malicious peers can spoof STUN responses to get incorrect external address information.

**Recommendation:** 
- Add HMAC-SHA1 Message-Integrity attribute to STUN responses (server side)
- Verify Message-Integrity on STUN responses (client side)
- Use a shared password derived from the peer's public key or a configured secret

**Not urgent** for initial deployment but should be addressed before production.

---

## Summary of Previously Fixed Issues (for reference)

| # | Issue | Status |
|---|-------|--------|
| 1 | STUN Binding Request Detection Logic | FIXED - Uses direct comparison `classMethod == 0x0000 && method == 0x0001` |
| 2 | STUN XOR-MAPPED-ADDRESS Port Byte Order | FIXED - Proper `ntohs()`/`htons()` conversion around XOR |
| 3 | STUN Address XOR Byte Order | FIXED - Direct XOR on network-order bytes, correct `inet_ntop` usage |
| 4 | STUN Transaction ID RNG | FIXED - Uses `thread_local std::mt19937` |
| 5 | Missing Peer ID in Hole Punch Request | FIXED - Added `SetLocalPeerID()` method, uses `m_localPeerID` |
| 6 | Duplicate Punch Packets | FIXED - Removed redundant counter-punch loop |
| 7 | Multi-Port Race Condition | FIXED - `FindOrCreateMultiPortSession` acquires `m_mutex` |
| 8 | ShouldUseMultiPortPunch Logic | FIXED - Added peer_id-specific overload |
| 9 | UpdateTracker Phase Naming | FIXED - Uses `Phase::Success` for success case |
| 10 | RecordPunchSuccess Phase Transition | FIXED - Uses `Phase::Success` |
| 11 | GetFailedTrackers Wrong Peers | FIXED - Checks both `single_port_failed && multi_port_failed` |
| 12 | SendMultiPortPunch Wrong Attempt Count | FIXED - Looks up from `m_multiPortSessions` |
| 13 | STUN Message Length Validation | FIXED - Checks `bytes >= 20` |
| 14 | STUN Message-Integrity | **REMAINING** - See issue #1 above |
| 15 | m_punchStartTime Not Initialized | FIXED - Initialized in constructor |
| 16 | m_coordinatorID Unused Member | FIXED - Replaced with `m_localPeerID` |
| 17 | Punch Payload Timestamp Inconsistency | FIXED - Uses `steady_clock::now()` in milliseconds |
