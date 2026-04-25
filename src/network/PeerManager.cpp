#include "network/PeerManager.hpp"
#include "network/Protocol.hpp"
#include "network/PacketSecurity.hpp"
#include "utils/ThreadUtils.hpp"
#include "utils/Helpers.hpp"
#include <mbedtls/sha1.h>
#include <iostream>
#include <sstream>
#include <chrono>
#include <cstring>
#include <ctime>
#include <algorithm>

namespace FreeAI {
	namespace Network {

		PeerManager::PeerManager()
			: m_running(false), m_listenPort(9090), m_isSuperNode(false),
			m_identity(nullptr), m_enableSigning(true) {
		}

		PeerManager::~PeerManager() {
			Stop();
		}

		bool PeerManager::Initialize(const Utils::Config& config) {
			m_listenPort = config.GetInt("network", "bootstrap_port", 9090);
			m_enableSigning = config.GetBool("security", "enable_signing", true);

			std::string seeds = config.Get("network", "seed_nodes", "");
			std::stringstream ss(seeds);
			std::string seed;
			while (std::getline(ss, seed, ',')) {
				seed = Trim(seed);
				if (!seed.empty()) {
					m_seedNodes.push_back(seed);
				}
			}

			/*
			if (m_seedNodes.empty()) {
				m_seedNodes = {
					"seed1.freeai.network:9090",
					"seed2.freeai.network:9090"
				};
			}
			*/

			std::cout << "[PEER] Loaded " << m_seedNodes.size() << " seed nodes." << std::endl;
			return true;
		}

		void PeerManager::SetIdentity(Crypto::Identity* identity) {
			m_identity = identity;
			std::cout << "[PEER] Identity set. Node ID: " << identity->GetShortID() << std::endl;
		}

		void PeerManager::SetSigningEnabled(bool enableSigning) {
			m_enableSigning = enableSigning;
			std::cout << "[PEER] Packet signing " << (enableSigning ? "enabled" : "disabled") << std::endl;
		}

		void PeerManager::Start() {
			m_running = true;

			if (!m_identity || !m_identity->IsValid()) {
				std::cerr << "[PEER] FATAL: Identity not loaded before Start()!" << std::endl;
				return;
			}

			if (!m_socket.Create()) {
				std::cerr << "[PEER] Failed to create UDP socket." << std::endl;
				return;
			}

			if (m_socket.Bind(m_listenPort)) {
				m_isSuperNode = true;
				m_socket.SetNonBlocking(true);
				std::cout << "[PEER] Running as Super Node (Port " << m_listenPort << " open)." << std::endl;
			}
			else {
				m_isSuperNode = false;
				std::cout << "[PEER] Running as Leaf Node (NAT detected)." << std::endl;
			}

			m_dht.Initialize(m_identity);
			m_dht.Start(&m_socket);

			m_listenerThread = std::thread(&PeerManager::ListenLoop, this);
			m_punchThread = std::thread(&PeerManager::PunchLoop, this);
			m_seedThread = std::thread(&PeerManager::SeedRegistrationLoop, this);

			std::thread(&PeerManager::ConnectToSeeds, this).detach();
		}

		void PeerManager::Stop() {
			m_running = false;

			if (m_listenerThread.joinable()) m_listenerThread.join();
			if (m_punchThread.joinable()) m_punchThread.join();
			if (m_seedThread.joinable()) m_seedThread.join();

			m_socket.Close();
		}

		bool PeerManager::SendSecurePacket(UDPSocket& socket, const std::string& ip, int port,
			uint8_t type, const void* payload, size_t size)
		{
			bool sign = false;
			bool encrypt = true;

			switch (type) {
			case PT_REGISTER:
			case PT_HANDSHAKE:
			case PT_PEER_LIST:
			case PT_INFERENCE_REQUEST:
			case PT_INFERENCE_RESPONSE:
				sign = m_enableSigning && (m_identity != nullptr) && m_identity->IsValid();
				break;
			default:
				sign = false;
				break;
			}

			std::vector<uint8_t> packet = PacketSecurity::PrepareOutgoing(
				type, payload, size, sign, encrypt, m_identity);

			if (packet.empty()) {
				std::cerr << "[PEER] Failed to prepare secure packet." << std::endl;
				return false;
			}

			int sent = socket.SendTo(packet.data(), packet.size(), ip, port);
			if (sent < 0) {
				std::cerr << "[PEER] Failed to send packet to " << ip << ":" << port << std::endl;
				return false;
			}

			return true;
		}

		void PeerManager::ListenLoop() {
			FreeAI::Utils::SetThreadPriorityLow();

			std::vector<char> buffer(MAX_PACKET_SIZE);
			auto pbuf = buffer.data();
			std::string sender_ip;
			int sender_port;

			while (m_running) {
				int bytes = m_socket.ReceiveFrom(pbuf, MAX_PACKET_SIZE, sender_ip, sender_port);
				if (bytes > 0) {
					HandleBootstrapPacket(m_socket, pbuf, bytes, sender_ip, sender_port);
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			}
		}

		void PeerManager::HandleBootstrapPacket(UDPSocket& sock, char* buffer, int bytes,
			const std::string& ip, int port) {

			std::string senderPubKey;
			auto peers = GetKnownPeers();
			for (const auto& peer : peers) {
				if (peer.ip == ip && peer.port == port) {
					senderPubKey = peer.public_key_pem;
					break;
				}
			}

			SecurePacketHeader header;
			std::vector<uint8_t> payload;

			if (!PacketSecurity::ProcessIncoming(
				reinterpret_cast<uint8_t*>(buffer), bytes, header, payload,
				m_identity, senderPubKey)) {
				std::cerr << "[PEER] Invalid packet from " << ip << ":" << port << " header.type=" << (int)header.type << " header.payload_size=" << header.payload_size << std::endl;
				return;
			}

			if (header.type == PT_REGISTER) {
				if (payload.size() >= sizeof(RegisterPayload)) {
					const RegisterPayload* reg = reinterpret_cast<const RegisterPayload*>(payload.data());

					std::string peer_id = std::string(reg->peer_id);
					std::string pubkey_pem = TrimNulls(std::string(reinterpret_cast<const char*>(reg->pubkey), reg->pubkey_size));
					eRegStep step = (eRegStep)reg->step;

					bool bCanSendPeerList = false;

					switch (step) {
					case ers_register: {
						bool bExists = false;
						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							if (m_peerPublicKeys.count(peer_id) > 0) {
								bExists = true;
								std::cout << "[PEER] Re-registering: " << peer_id << " @ " << ip << ":" << port << std::endl;
								m_peerPublicKeys.erase(peer_id);
							}
						}

						if (!bExists) {
							std::cout << "[PEER] Registered: " << peer_id << " @ " << ip << ":" << port << std::endl;
						}

						StorePeerPublicKey(peer_id, pubkey_pem);
						AddPeer({ ip, port, peer_id, pubkey_pem, std::time(nullptr), true, false });

						NodeId dht_node_id;
						dht_node_id.FromPubkey(pubkey_pem);

						m_dht.AddNode(dht_node_id, ip, port);
						std::cout << "[DHT] Added peer to routing table: " << peer_id << " DHT node id: " << dht_node_id.ToString(16) << std::endl;

						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							for (auto& tracker : m_seedTrackers) {
								auto info = tracker->GetInfo();
								if (info.ip == ip && info.port == port) {
									tracker->SetState(PeerConnectionState::Connecting);
									tracker->SetLastSuccessTime(static_cast<uint32_t>(std::time(nullptr)));
									break;
								}
							}
						}

						RegisterPayload resp;
						std::memset(&resp, 0, sizeof(resp));
						auto mypubkey = m_identity->GetPublicKeyPEM();
						auto short_id = m_identity->GetShortID();
						auto peer_id_len = std::min(short_id.size(), sizeof(resp.peer_id) - 1);
						std::memcpy(resp.peer_id, short_id.c_str(), peer_id_len);
						resp.peer_id[peer_id_len] = '\0';
						resp.pubkey_size = (uint16_t)mypubkey.size();
						std::memcpy(resp.pubkey, mypubkey.c_str(), resp.pubkey_size);
						resp.step = ers_register_resp;

						SendSecurePacket(sock, ip, port, PT_REGISTER, &resp, sizeof(resp));
						std::cout << "[PEER] Sent REGISTER_RESP to " << ip << ":" << port << std::endl;
						break;
					}

					case ers_register_resp: {
						std::cout << "[PEER] Received REGISTER_RESP from " << ip << ":" << port << std::endl;

						StorePeerPublicKey(peer_id, pubkey_pem);
						AddPeer({ ip, port, peer_id, pubkey_pem, std::time(nullptr), true, false });

						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							for (auto& tracker : m_seedTrackers) {
								auto info = tracker->GetInfo();
								if (info.ip == ip && info.port == port) {
									tracker->SetState(PeerConnectionState::Connecting);
									tracker->SetLastSuccessTime(static_cast<uint32_t>(std::time(nullptr)));
									break;
								}
							}
						}
	
						RegisterPayload ack;
						std::memset(&ack, 0, sizeof(ack));
						fai_strncpy(ack.peer_id, m_identity->GetShortID(), sizeof(ack.peer_id));
						ack.pubkey_size = 0;
						ack.step = ers_accepted;

						SendSecurePacket(sock, ip, port, PT_REGISTER, &ack, sizeof(ack));
						std::cout << "[PEER] Sent REGISTER_ACCEPTED to " << ip << ":" << port << std::endl;

						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							for (auto& tracker : m_seedTrackers) {
								auto info = tracker->GetInfo();
								if (info.ip == ip && info.port == port) {
									tracker->SetState(PeerConnectionState::Connected);
									break;
								}
							}
						}
						bCanSendPeerList = true;
						break;
					}

					case ers_accepted: {
						std::cout << "[PEER] Registration Accepted ACK received from " << ip << ":" << port << std::endl;

						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							for (auto& tracker : m_seedTrackers) {
								auto info = tracker->GetInfo();
								if (info.ip == ip && info.port == port) {
									tracker->SetState(PeerConnectionState::Connected);
									tracker->SetLastSuccessTime(static_cast<uint32_t>(std::time(nullptr)));
									break;
								}
							}
						}
						bCanSendPeerList = true;
						break;
					}

					case ers_failed: {
						std::cerr << "[PEER] Registration FAILED by " << ip << ":" << port << std::endl;

						{
							std::lock_guard<std::mutex> lock(m_networkMutex);
							m_peerPublicKeys.erase(peer_id);

							for (auto& tracker : m_seedTrackers) {
								auto info = tracker->GetInfo();
								if (info.ip == ip && info.port == port) {
									tracker->SetState(PeerConnectionState::Reset);
									break;
								}
							}
						}
						break;
					}
					} // switch

					if (bCanSendPeerList) {
						auto peerlst = BuildPeerList();
						if (!peerlst.empty()) {
							SendSecurePacket(sock, ip, port, PT_PEER_LIST, peerlst.c_str(), peerlst.size() + 1);
							std::cout << "[PEER] Sent PT_PEER_LIST to " << ip << ":" << port << std::endl;
						}
						
					}
				}
			}
			else if (header.type == PT_INTRO_REQUEST && m_isSuperNode) {
				std::string target_id = std::string(
					reinterpret_cast<const char*>(payload.data()),
					payload.size());

				for (const auto& peer : peers) {
					if (peer.peer_id == target_id) {
						IntroResponsePayload response;
						std::memset(&response, 0, sizeof(response));
						fai_strncpy(response.target_ip, peer.ip, sizeof(response.target_ip));
						response.target_port = static_cast<uint16_t>(peer.port);
						fai_strncpy(response.target_id, peer.peer_id, sizeof(response.target_id));
						response.target_pubkey_size = static_cast<uint16_t>(peer.public_key_pem.size());

						std::vector<uint8_t> responsePacket;
						responsePacket.resize(sizeof(IntroResponsePayload) + peer.public_key_pem.size());
						std::memcpy(responsePacket.data(), &response, sizeof(IntroResponsePayload));
						std::memcpy(responsePacket.data() + sizeof(IntroResponsePayload),
							peer.public_key_pem.c_str(), peer.public_key_pem.size());

						SendSecurePacket(sock, ip, port, PT_INTRO_RESPONSE,
							responsePacket.data(), responsePacket.size());

						std::cout << "[PEER] Introduced " << ip << " to " << peer.ip << std::endl;
						break;
					}
				}
			}
			else if (header.type == PT_INTRO_RESPONSE) {
				if (payload.size() >= sizeof(IntroResponsePayload)) {
					const IntroResponsePayload* intro = reinterpret_cast<const IntroResponsePayload*>(payload.data());

					std::string target_ip = std::string(intro->target_ip);
					int target_port = intro->target_port;
					std::string target_id = std::string(intro->target_id);
					std::string target_pubkey = std::string(
						reinterpret_cast<const char*>(payload.data() + sizeof(IntroResponsePayload)),
						intro->target_pubkey_size);

					std::cout << "[PEER] Received intro: " << target_id << " @ " << target_ip << ":" << target_port << std::endl;

					StorePeerPublicKey(target_id, target_pubkey);
					m_punchManager.StartPunch(target_ip, target_port, target_id);
					AddPeer({ target_ip, target_port, target_id, target_pubkey, std::time(nullptr), true, false });
				}
			}
			else if (header.type == PT_PEER_LIST) {
				auto newPeers = ParsePeerList(
					reinterpret_cast<const char*>(payload.data()),
					payload.size());

				for (const auto& p : newPeers) {
					if (p.ip == "127.0.0.1" && p.port == m_listenPort) { // ToDo: replace hardcoded "127.0.0.1" to the list of real host IPs which we are listen on.
						continue;
					}

					AddPeer(p);

					if (!p.public_key_pem.empty()) {
						NodeId nid;
						nid.FromPubkey(p.public_key_pem);
						m_dht.AddNode(nid, p.ip, p.port);
					}
				}
				std::cout << "[PEER] Received " << newPeers.size() << " peers from seed." << std::endl;
				std::cout << "[DHT] Routing table has " << m_dht.GetNodeCount() << " nodes" << std::endl;
			}
			else if (header.type == PT_PUNCH) {
				if (payload.size() >= sizeof(PunchPayload)) {
					auto punch = (const PunchPayload*)payload.data();
					HandlePunchPacket(ip, port, punch);
				}
			}
			else if (header.type == PT_PUNCH_ACK) {
				std::cout << "[PUNCH] Received punch ACK from " << ip << ":" << port << std::endl;
				// Punch ACK packets are sent in plain format (header + payload directly)
				// Check if the payload starts with a valid SecurePacketHeader (encrypted format)
				// or directly with PunchPayload (plain format)
				if (payload.size() >= sizeof(PunchPayload)) {
					// Check if this is a plain punch packet (magic_xor matches MAGIC_NUMBER with flags=0)
					const auto* plainPayload = reinterpret_cast<const PunchPayload*>(payload.data());
					
					// Verify it looks like a plain punch payload (not encrypted data)
					// Plain punch payloads have readable sender_id
					bool looksValid = true;
					for (int i = 0; i < sizeof(plainPayload->sender_id) && looksValid; ++i) {
						char c = plainPayload->sender_id[i];
						if (c != '\0' && (c < ' ' || c > '~')) {
							looksValid = false;
						}
					}
					
					if (looksValid && strlen(plainPayload->sender_id) > 0) {
						m_punchManager.MarkSuccess(plainPayload->sender_id);
						std::cout << "[PUNCH] SUCCESS! Direct connection to " << plainPayload->sender_id << " established" << std::endl;
					}
				}
			}
			else if (header.type == PT_COORD_HOLE_PUNCH_INFO) {
				// Coordinator sent us hole punch info
				if (m_punchManager.HandleHolePunchInfo(ip, static_cast<int>(port),
					reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()))) {
					std::cout << "[COORD] Hole punch info processed successfully." << std::endl;
				}
				else {
					std::cerr << "[COORD] Failed to process hole punch info from " << ip << ":" << port << std::endl;
				}
			}
			else if (header.type == PT_COORD_HOLE_PUNCH_START) {
				// Coordinator sent us hole punch start signal
				if (m_punchManager.HandleHolePunchStart(ip, static_cast<int>(port),
					reinterpret_cast<const char*>(payload.data()), static_cast<int>(payload.size()))) {
					std::cout << "[COORD] Hole punch start signal processed." << std::endl;
				}
			}
			else if (header.type == PT_COORD_HOLE_PUNCH_FAILED) {
				// Handle failure report or failure notification
				if (payload.size() >= sizeof(CoordHolePunchFailedPayload)) {
					const CoordHolePunchFailedPayload* failPayload = reinterpret_cast<const CoordHolePunchFailedPayload*>(payload.data());
					
					// Check if this is a failure report (has peer_id and peer_ip fields)
					if (strlen(failPayload->peer_id) > 0 && failPayload->phase <= 1) {
						HandlePunchFailureReport(ip, port, failPayload);
					}
					else {
						// It's a regular failure notification
						std::cerr << "[COORD] Received failure notification from " << ip << ":" << port << std::endl;
					}
				}
			}
			else if (header.type == PT_COORD_HOLE_PUNCH_MULTI_START) {
				// Received multi-port punch coordination from super node
				if (payload.size() >= sizeof(CoordHolePunchMultiStartPayload)) {
					const CoordHolePunchMultiStartPayload* multiStart = reinterpret_cast<const CoordHolePunchMultiStartPayload*>(payload.data());
					std::string target_id(multiStart->peer_id);
					std::string target_ip(multiStart->peer_ip);
					int target_port = ntohs(multiStart->peer_base_port);
					uint8_t port_range = multiStart->peer_port_range;
					
					std::cout << "[COORD] Received multi-port punch coordination for peer " << target_id
					          << " @ " << target_ip << ":" << target_port << " (range: " << static_cast<int>(port_range) << ")" << std::endl;
					
					// Start multi-port punch to the target
					m_punchManager.StartMultiPortPunch(target_ip, target_port, port_range, target_id);
				}
			}
			else if (header.type == PT_COORD_HOLE_PUNCH_REQUEST) {
				// Received a hole punch request (only process if we're a super node)
				if (m_isSuperNode && payload.size() >= sizeof(CoordHolePunchPayload)) {
					const CoordHolePunchPayload* req = reinterpret_cast<const CoordHolePunchPayload*>(payload.data());
					std::string requester_id(req->requester_id);
					std::string target_id(req->target_id);

					std::cout << "[COORD] Received hole punch request from " << requester_id
					          << " for target " << target_id << std::endl;

					// Find the target peer
					auto knownPeers = GetKnownPeers();
					PeerInfo* targetPeer = nullptr;
					for (auto& peer : knownPeers) {
						if (peer.peer_id == target_id) {
							targetPeer = &peer;
							break;
						}
					}

					if (!targetPeer) {
						std::cerr << "[COORD] Target peer " << target_id << " not found." << std::endl;
						// Send failure response
						SendCoordHolePunchFailed(ip, port, requester_id, target_id, "Target not found");
						return;
					}

					// Query STUN servers for both peers' external addresses
					// For simplicity, use the coordinator's own STUN server (us)
					// In a real implementation, each peer would have their own STUN server
					
					// Get external address of the requester (from the connection)
					ExternalAddress requesterExternal;
					requesterExternal.ip = ip;
					requesterExternal.port = port;
					requesterExternal.discovered = true;

					// For the target, we need to query their STUN server
					// In this simplified version, we use the target's reported address
					ExternalAddress targetExternal;
					targetExternal.ip = targetPeer->ip;
					targetExternal.port = targetPeer->port;
					targetExternal.discovered = true;

					std::cout << "[COORD] Requester external: " << requesterExternal.ip << ":" << requesterExternal.port << std::endl;
					std::cout << "[COORD] Target external: " << targetExternal.ip << ":" << targetExternal.port << std::endl;

					// Calculate punch start time (2 seconds from now to allow both peers to prepare)
					auto now = std::chrono::steady_clock::now();
					auto punchStartTime = std::chrono::duration_cast<std::chrono::milliseconds>(
						now.time_since_epoch()).count() + 2000;

					// Send hole punch info to the requester
					SendHolePunchInfo(ip, port, target_id, targetExternal, punchStartTime);

					// Send hole punch info to the target
					SendHolePunchInfo(targetPeer->ip, targetPeer->port, requester_id, requesterExternal, punchStartTime);

					// Send start signal to both
					SendHolePunchStart(ip, port, punchStartTime);
					SendHolePunchStart(targetPeer->ip, targetPeer->port, punchStartTime);

					std::cout << "[COORD] Hole punch coordination complete for " << requester_id << " <-> " << target_id << std::endl;
				}
			}
			else if (header.type >= PT_DHT_FIND_NODE && header.type <= PT_DHT_PING) {
				ProcessDHTPacket(sock, ip, port, header.type, payload);
			}
		}

		void PeerManager::ProcessDHTPacket(UDPSocket& sock, const std::string& ip, int port,
			uint8_t type, const std::vector<uint8_t>& payload) {
			if (payload.size() < sizeof(DHTFindNodePayload)) {
				return;
			}

			if (type == PT_DHT_FIND_NODE) {
				auto findReq =
					(const DHTFindNodePayload*)payload.data();
				HandleDHTFindNode(sock, ip, port, findReq);
			}
			else if (type == PT_DHT_FIND_NODE_RESPONSE) {
				auto findResp =
					(const DHTFindNodeResponsePayload*)payload.data();
				HandleDHTFindNodeResponse(ip, port, findResp);
			}
			else if (type == PT_DHT_PING) {
				m_dht.SendDHTPacket(&m_socket, ip, port, PT_DHT_PING, nullptr, 0);
				m_dht.ProcessIncoming(payload.data(), payload.size(), ip, port);
			}
		}

		void PeerManager::HandleDHTFindNode(UDPSocket& /*sock*/, const std::string& ip, int port,
			const DHTFindNodePayload* payload) {
			std::cout << "[DHT] FIND_NODE request from " << ip << ":" << port << std::endl;

			NodeId targetId(payload->target_id);
			auto closestNodes = m_dht.FindNodes(targetId);

			DHTFindNodeResponsePayload response;
			memset(&response, 0, sizeof(response));
			response.node_count = static_cast<uint8_t>(std::min(closestNodes.size(), size_t(20)));

			std::vector<uint8_t> responsePacket;
			responsePacket.resize(sizeof(DHTFindNodeResponsePayload) +
				response.node_count * sizeof(DHTNodeInfo));

			memcpy(responsePacket.data(), &response, sizeof(DHTFindNodeResponsePayload));

			for (size_t i = 0; i < closestNodes.size() && i < 20; ++i) {
				DHTNodeInfo nodeInfo;
				memset(&nodeInfo, 0, sizeof(nodeInfo));

				memcpy(nodeInfo.node_id, closestNodes[i].node_id.Data(), DHT_NODE_ID_SIZE);
				fai_strncpy(nodeInfo.ip, closestNodes[i].ip, sizeof(nodeInfo.ip));
				nodeInfo.port = static_cast<uint16_t>(closestNodes[i].port);
				nodeInfo.last_seen = closestNodes[i].last_seen;

				memcpy(responsePacket.data() + sizeof(DHTFindNodeResponsePayload) +
					i * sizeof(DHTNodeInfo), &nodeInfo, sizeof(DHTNodeInfo));
			}

			std::cout << "[DEBUG] Sending DHT response: " << responsePacket.size() << " bytes, "
				<< (int)response.node_count << " nodes" << std::endl;

			m_dht.SendDHTPacket(&m_socket, ip, port, PT_DHT_FIND_NODE_RESPONSE,
				responsePacket.data(), responsePacket.size());
		}

		void PeerManager::HandleDHTFindNodeResponse(const std::string& ip, int port,
			const DHTFindNodeResponsePayload* payload) {
			std::cout << "[DHT] FIND_NODE_RESPONSE from " << ip << ":" << port
				<< " (" << (int)payload->node_count << " nodes)" << std::endl;

			const uint8_t* ptr = reinterpret_cast<const uint8_t*>(payload) +
				sizeof(DHTFindNodeResponsePayload);

			for (int i = 0; i < payload->node_count; ++i) {
				const DHTNodeInfo* nodeInfo = reinterpret_cast<const DHTNodeInfo*>(
					ptr + i * sizeof(DHTNodeInfo));

				NodeId nid(nodeInfo->node_id);
				m_dht.AddNode(nid, nodeInfo->ip, nodeInfo->port);

				std::cout << "[DHT] Learned about node: " << nid.ToString(16) << " @ " << nodeInfo->ip << ":" << nodeInfo->port << std::endl;
			}
		}

		void PeerManager::ConnectToSeeds() {
			std::this_thread::sleep_for(std::chrono::seconds(2));

			{
				std::lock_guard<std::mutex> lock(m_networkMutex);
				for (const auto& seed : m_seedNodes) {
					size_t pos = seed.find(':');
					if (pos == std::string::npos) continue;

					std::string seed_ip = seed.substr(0, pos);
					int seed_port = std::stoi(seed.substr(pos + 1));

					auto tracker = std::make_shared<PeerConnectionTracker>();
					tracker->Initialize(seed, seed_ip, seed_port);
					m_seedTrackers.push_back(tracker);
				}
			}

			SendInitialRegistrations();
		}

		void PeerManager::SendInitialRegistrations() {
			std::lock_guard<std::mutex> lock(m_networkMutex);

			for (auto& tracker : m_seedTrackers) {
				auto info = tracker->GetInfo();
				if (info.state == PeerConnectionState::Disconnected) {
					SendRegistration(info);
					tracker->SetState(PeerConnectionState::Connecting);
					tracker->SetLastSuccessTime(static_cast<uint32_t>(std::time(nullptr)));
				}
			}
		}

		void PeerManager::SendRegistration(PeerConnectionTracker::TrackerInfo& info) {
			if (!m_identity || !m_identity->IsValid()) {
				std::cerr << "[PEER] Cannot register: identity not valid!" << std::endl;
				return;
			}

			std::string peer_id = m_identity->GetShortID();
			std::string pubkey_pem = m_identity->GetPublicKeyPEM();

			RegisterPayload regPayload;
			std::memset(&regPayload, 0, sizeof(regPayload));

			fai_strncpy(regPayload.peer_id, peer_id, sizeof(regPayload.peer_id));
			regPayload.step = ers_register;
			regPayload.pubkey_size = static_cast<uint16_t>(pubkey_pem.size());
			std::memcpy(regPayload.pubkey, pubkey_pem.c_str(), pubkey_pem.size());

			SendSecurePacket(m_socket, info.ip, info.port, PT_REGISTER,
				&regPayload, sizeof(regPayload));

			info.last_attempt_ts = static_cast<uint32_t>(std::time(nullptr));
			info.retry_count++;

			std::cout << "[PEER] Sent REGISTER to " << info.ip << ":" << info.port
				<< " (attempt " << info.retry_count << ", delay " << info.next_retry_delay_sec << "s)" << std::endl;
		}

		void PeerManager::SeedRegistrationLoop() {
			FreeAI::Utils::SetThreadPriorityLow();

			while (m_running) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1000));

				uint32_t curTs = static_cast<uint32_t>(std::time(nullptr));
				bool allConnected = true;

				std::lock_guard<std::mutex> lock(m_networkMutex);

				for (auto& tracker : m_seedTrackers) {
					auto info = tracker->GetInfo();
					
					// Check if already connected
					if (info.state == PeerConnectionState::Connected) {
						continue;
					}

					allConnected = false;

					if (!m_identity || !m_identity->IsValid()) {
						continue;
					}

					// Try to recover from failed state
					if (tracker->TryRecover(curTs)) {
						info = tracker->GetInfo();
					}

					// Check if we should retry
					if (!tracker->ShouldRetry(curTs)) {
						continue;
					}

					switch (info.state) {
					case PeerConnectionState::Disconnected:
					case PeerConnectionState::Reset:
						if (tracker->RecordRetryAttempt()) {
							SendRegistration(info);
							tracker->SetState(PeerConnectionState::Connecting);
							std::cout << "[PEER] Initial registration to " << info.ip
								<< ":" << info.port << std::endl;
						} else {
							tracker->SetState(PeerConnectionState::Failed);
							std::cerr << "[PEER] Registration failed for " << info.seed_address
								<< " after max retries" << std::endl;
						}
						break;

					case PeerConnectionState::Connecting:
						if (tracker->RecordRetryAttempt()) {
							SendRegistration(info);
							std::cout << "[PEER] Retrying registration with " << info.ip
								<< ":" << info.port << " (attempt " << info.retry_count << ")" << std::endl;
						} else {
							tracker->SetState(PeerConnectionState::Failed);
							std::cerr << "[PEER] Registration failed for " << info.seed_address
								<< " after " << info.retry_count << " attempts" << std::endl;
						}
						break;

					case PeerConnectionState::Failed:
						std::cout << "[PEER] Waiting for recovery delay for " << info.seed_address << std::endl;
						break;

					default:
						break;
					}
				}

				static bool dhtDiscoverySent = false;
				if (!dhtDiscoverySent && PeerConnectionTracker::AllConnected(m_seedTrackers) && !m_seedTrackers.empty()) {
					dhtDiscoverySent = true;

					uint8_t random_target[20];
					memset(random_target, 0, 20);

					for (const auto& tracker : m_seedTrackers) {
						auto info = tracker->GetInfo();
						DHTFindNodePayload findPayload;
						memset(&findPayload, 0, sizeof(findPayload));
						memcpy(findPayload.target_id, random_target, 20);

						m_dht.SendDHTPacket(&m_socket, info.ip, info.port, PT_DHT_FIND_NODE,
							&findPayload, sizeof(findPayload));

						std::cout << "[DHT] Sent FIND_NODE to " << info.ip << ":" << info.port << std::endl;
					}

					std::cout << "[DHT] Final routing table has " << m_dht.GetNodeCount() << " nodes" << std::endl;
				}
			}
		}

		std::vector<PeerInfo> PeerManager::ParsePeerList(const char* data, size_t size) {
			std::vector<PeerInfo> peers;
			if (size == 0) return peers;

			std::string str(data, size);
			std::stringstream ss(str);
			std::string line;

			while (std::getline(ss, line, ';')) {
				size_t pos1 = line.find(':');
				size_t pos2 = line.find(':', pos1 + 1);
				size_t pos3 = line.find(':', pos2 + 1);

				if (pos1 != std::string::npos && pos2 != std::string::npos) {
					PeerInfo p;
					p.ip = line.substr(0, pos1);
					p.port = std::stoi(line.substr(pos1 + 1, pos2 - pos1 - 1));

					if (pos3 != std::string::npos) {
						p.peer_id = line.substr(pos2 + 1, pos3 - pos2 - 1);
						p.public_key_pem = TrimNulls(line.substr(pos3 + 1));
					}
					else {
						p.peer_id = line.substr(pos2 + 1);
						p.public_key_pem = "";
					}

					p.last_seen = std::time(nullptr);
					p.is_super_node = true;
					peers.push_back(p);
				}
			}
			return peers;
		}

		std::string PeerManager::BuildPeerList() {
			std::stringstream ss;
			auto peers = GetKnownPeers();
			for (size_t i = 0; i < peers.size() && i < 10; ++i) {
				if (i > 0) ss << ";";
				ss << peers[i].ip << ":" << peers[i].port << ":"
					<< peers[i].peer_id << ":" << peers[i].public_key_pem;
			}
			return ss.str();
		}

		void PeerManager::AddPeer(const PeerInfo& peer) {
			std::lock_guard<std::mutex> lock(m_peerMutex);
			for (const auto& p : m_peers) {
				if (p.ip == peer.ip && p.port == peer.port) {
					return;
				}
			}
			m_peers.push_back(peer);
		}

		std::vector<PeerInfo> PeerManager::GetKnownPeers() const {
			std::lock_guard<std::mutex> lock(m_peerMutex);
			return m_peers;
		}

		bool PeerManager::IsSuperNode() const {
			return m_isSuperNode;
		}

		void PeerManager::PunchLoop() {
			FreeAI::Utils::SetThreadPriorityLow();

			while (m_running) {
				m_punchManager.Cleanup();

				// Process single-port punch sessions
				auto activeSessions = m_punchManager.GetActiveSessions();
				for (const auto& session : activeSessions) {
					// Check if we should send a punch packet
					if (m_punchManager.ShouldSendPunchAuto(session.target_id)) {
						SendPunchPacket(session.target_ip, session.target_port, session.target_id);
						m_punchManager.RecordAttempt(session.target_id);
						
						// Check if we should switch to multi-port punching
						if (m_punchManager.ShouldSwitchToMultiPort(session.target_id)) {
							std::cout << "[PUNCH] Single-port failed for peer " << session.target_id
								<< ", switching to multi-port punch" << std::endl;
							m_punchManager.SwitchToMultiPortPunch(session.target_id);
						}
					}
				}

				// Process multi-port punch sessions
				auto activeMultiPortSessions = m_punchManager.GetActiveMultiPortSessions();
				for (const auto& session : activeMultiPortSessions) {
					if (m_punchManager.ShouldSendPunchAuto(session.target_id)) {
						// Send punch packets across the port range
						m_punchManager.SendMultiPortPunch(session.target_ip, session.target_base_port,
							session.target_port_range, session.target_id);
					}
				}

				// Removed redundant counter-punch loop (Issue #6 from review)
				// The active session loops above already handle punching targets
				// This redundant loop wasted bandwidth and confused attempt counting

				std::this_thread::sleep_for(std::chrono::milliseconds(PUNCH_INTERVAL_MS));
			}
		}

		void PeerManager::RequestIntroduction(const std::string& ip, int port, const std::string& target_peer_id) {
			std::cout << "[PEER] Requesting introduction to " << target_peer_id << std::endl;
			SendSecurePacket(m_socket, ip, port, PT_INTRO_REQUEST,
				target_peer_id.c_str(), target_peer_id.size());
		}

		void PeerManager::SendPunchPacket(const std::string& ip, int port, const std::string& peer_id) {
			PunchPayload payload;
			std::memset(&payload, 0, sizeof(payload));

			std::string short_id = m_identity ? m_identity->GetShortID() : "unknown";
			fai_strncpy(payload.sender_id, short_id, sizeof(payload.sender_id));
			// Use milliseconds since epoch for consistent timestamp handling
			payload.timestamp = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			
			// Get the current attempt count for this session
			uint8_t attempt = static_cast<uint8_t>(m_punchManager.GetAttemptCount(peer_id) + 1);
			payload.attempt_num = attempt;

			std::cout << "[PUNCH] Sending punch (attempt " << (int)attempt << ") to " << ip << ":" << port << std::endl;
			
			// Send as a plain UDP packet without encryption for hole punching
			// This is critical for NAT traversal - the target needs to see our source IP/port
			// Prepare the packet with SecurePacketHeader but without encryption
			SecurePacketHeader header;
			std::memset(&header, 0, sizeof(header));
			header.magic_xor = MAGIC_NUMBER;
			header.nonce = static_cast<uint32_t>(payload.timestamp);
			header.flags = 0;  // No signing, no encryption for punch packets
			header.type = PT_PUNCH;
			header.payload_size = static_cast<uint16_t>(sizeof(PunchPayload));

			std::vector<uint8_t> packet(sizeof(SecurePacketHeader) + sizeof(PunchPayload));
			std::memcpy(packet.data(), &header, sizeof(SecurePacketHeader));
			std::memcpy(packet.data() + sizeof(SecurePacketHeader), &payload, sizeof(PunchPayload));

			int sent = m_socket.SendTo(packet.data(), packet.size(), ip, port);
			if (sent < 0) {
				std::cerr << "[PUNCH] Failed to send punch packet to " << ip << ":" << port << std::endl;
			}
		}

		void PeerManager::HandlePunchPacket(const std::string& ip, int port, const PunchPayload* payload) {
			std::string sender_id(payload->sender_id);
			std::cout << "[PUNCH] Received punch from " << sender_id
				<< " @ " << ip << ":" << port << " (attempt " << (int)payload->attempt_num << ")" << std::endl;

			// Verify the sender is a known peer before proceeding
			bool isKnownPeer = false;
			{
				auto peers = GetKnownPeers();
				for (const auto& peer : peers) {
					if (peer.peer_id == sender_id) {
						isKnownPeer = true;
						break;
					}
				}
			}

			if (!isKnownPeer) {
				std::cout << "[PUNCH] Ignoring punch from unknown peer: " << sender_id << std::endl;
				return;
			}

			// Check if we have an active punch session for this peer
			if (!m_punchManager.IsPunchActive(sender_id)) {
				std::cout << "[PUNCH] No active punch session for " << sender_id << ", ignoring" << std::endl;
				return;
			}

			// Verify timestamp is recent (within 60 seconds) to prevent replay attacks
			// Both timestamps are now in milliseconds since epoch
			auto nowMs = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			if (std::abs(static_cast<int64_t>(nowMs) - static_cast<int64_t>(payload->timestamp)) > 60000) {
				std::cout << "[PUNCH] Punch from " << sender_id << " has stale timestamp, ignoring" << std::endl;
				return;
			}

			// Send PUNCH_ACK back to the sender
			// This ACK also needs to be unencrypted for hole punching to work
			PunchPayload ackPayload;
			std::memset(&ackPayload, 0, sizeof(ackPayload));
			fai_strncpy(ackPayload.sender_id, m_identity->GetShortID(), sizeof(ackPayload.sender_id));
			ackPayload.timestamp = static_cast<uint64_t>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
			ackPayload.attempt_num = payload->attempt_num;

			SecurePacketHeader ackHeader;
			std::memset(&ackHeader, 0, sizeof(ackHeader));
			ackHeader.magic_xor = MAGIC_NUMBER;
			ackHeader.nonce = static_cast<uint32_t>(ackPayload.timestamp);
			ackHeader.flags = 0;
			ackHeader.type = PT_PUNCH_ACK;
			ackHeader.payload_size = static_cast<uint16_t>(sizeof(PunchPayload));

			std::vector<uint8_t> ackPacket(sizeof(SecurePacketHeader) + sizeof(PunchPayload));
			std::memcpy(ackPacket.data(), &ackHeader, sizeof(SecurePacketHeader));
			std::memcpy(ackPacket.data() + sizeof(SecurePacketHeader), &ackPayload, sizeof(PunchPayload));

			int sent = m_socket.SendTo(ackPacket.data(), ackPacket.size(), ip, port);
			if (sent < 0) {
				std::cerr << "[PUNCH] Failed to send PUNCH_ACK to " << ip << ":" << port << std::endl;
			}
			else {
				std::cout << "[PUNCH] Sent PUNCH_ACK to " << ip << ":" << port << std::endl;
			}
		}

		void PeerManager::StorePeerPublicKey(const std::string& peer_id, const std::string& pem) {
			std::lock_guard<std::mutex> lock(m_networkMutex);
			m_peerPublicKeys[peer_id] = pem;
			std::cout << "[KEYS] Stored public key for peer: " << peer_id << std::endl;
		}

		std::string PeerManager::GetPeerPublicKey(const std::string& peer_id) const {
			std::lock_guard<std::mutex> lock(m_networkMutex);
			auto it = m_peerPublicKeys.find(peer_id);
			if (it != m_peerPublicKeys.end()) {
				return it->second;
			}
			return "";
		}

		// =====================================================================
		// STUN/Coordination Helper Methods
		// =====================================================================

		ExternalAddress PeerManager::QueryPeerExternalAddress(const std::string& peer_ip, int /*peer_port*/) {
			// Query the peer's STUN server to get their external address
			// For now, assume the peer's STUN server is on the same IP but port 3478
			// In a real implementation, this would be configured or discovered
			return m_punchManager.QuerySTUNServer(peer_ip, 3478);
		}

		void PeerManager::SendHolePunchInfo(const std::string& ip, int port, const std::string& peer_id,
			                                  const ExternalAddress& peerAddr, uint64_t punchStartTime,
			                                  uint8_t port_range) {
			if (port_range > 0) {
				// Send multi-port hole punch info
				CoordHolePunchInfoMultiPayload info;
				std::memset(&info, 0, sizeof(info));

				fai_strncpy(info.peer_id, peer_id, sizeof(info.peer_id));

				fai_strncpy(info.peer_ip, peerAddr.ip, sizeof(info.peer_ip));

				info.peer_base_port = htons(static_cast<uint16_t>(peerAddr.port));
				info.peer_port_range = port_range;
				info.stun_port = htons(3478); // Default STUN port
				info.punch_start_time = punchStartTime;
				info.use_multi_port = 1;

				SendSecurePacket(m_socket, ip, port, PT_COORD_HOLE_PUNCH_INFO,
					&info, sizeof(info));

				std::cout << "[COORD] Sent multi-port hole punch info to " << ip << ":" << port
				          << " for peer " << peer_id << " (range: " << static_cast<int>(port_range) << " ports)" << std::endl;
			}
			else {
				// Send single-port hole punch info (legacy format)
				CoordHolePunchInfoPayload info;
				std::memset(&info, 0, sizeof(info));

				fai_strncpy(info.peer_id, peer_id, sizeof(info.peer_id));

				fai_strncpy(info.peer_ip, peerAddr.ip, sizeof(info.peer_ip));

				info.peer_port = htons(static_cast<uint16_t>(peerAddr.port));
				info.stun_port = htons(3478); // Default STUN port
				info.punch_start_time = punchStartTime;

				SendSecurePacket(m_socket, ip, port, PT_COORD_HOLE_PUNCH_INFO,
					&info, sizeof(info));

				std::cout << "[COORD] Sent hole punch info to " << ip << ":" << port
				          << " for peer " << peer_id << std::endl;
			}
		}

		void PeerManager::SendHolePunchStart(const std::string& ip, int port, uint64_t punchStartTime) {
			CoordHolePunchStartPayload start;
			std::memset(&start, 0, sizeof(start));

			start.punch_start_time = punchStartTime;

			SendSecurePacket(m_socket, ip, port, PT_COORD_HOLE_PUNCH_START,
				&start, sizeof(start));

			std::cout << "[COORD] Sent hole punch start signal to " << ip << ":" << port << std::endl;
		}

		void PeerManager::SendCoordHolePunchFailed(const std::string& ip, int port,
			                                          const std::string& requester_id, const std::string& target_id,
			                                          const char* reason) {
			// Build a simple failure message
			std::vector<uint8_t> payload(sizeof(CoordHolePunchPayload) + 64);
			std::memset(payload.data(), 0, payload.size());

			CoordHolePunchPayload* req = reinterpret_cast<CoordHolePunchPayload*>(payload.data());
			fai_strncpy(req->requester_id, requester_id, sizeof(req->requester_id));
			fai_strncpy(req->target_id, target_id, sizeof(req->target_id));

			// Append reason string
			size_t reasonLen = strlen(reason);
			std::memcpy(payload.data() + sizeof(CoordHolePunchPayload), reason, reasonLen);

			SendSecurePacket(m_socket, ip, port, PT_COORD_HOLE_PUNCH_FAILED,
				payload.data(), payload.size());

			std::cerr << "[COORD] Sent failure to " << ip << ":" << port
			          << ": " << reason << std::endl;
		}

		void PeerManager::SendPunchFailureReport(const std::string& coordinator_ip, int coordinator_port,
		                                         const std::string& peer_id, const std::string& peer_ip,
		                                         int peer_port, uint8_t phase) {
			// Send failure report to the coordinator (STUN server / super node)
			CoordHolePunchFailedPayload payload;
			std::memset(&payload, 0, sizeof(payload));

			fai_strncpy(payload.peer_id, peer_id, sizeof(payload.peer_id));
			fai_strncpy(payload.peer_ip, peer_ip, sizeof(payload.peer_ip));
			payload.peer_port = htons(static_cast<int16_t>(peer_port));
			payload.phase = phase;
			payload.is_reporter = 1; // First report

			SendSecurePacket(m_socket, coordinator_ip, coordinator_port, PT_COORD_HOLE_PUNCH_FAILED,
				&payload, sizeof(payload));

			std::cout << "[FAILURE REPORT] Sending failure report for peer " << peer_id
			          << " phase " << static_cast<int>(phase) << " to coordinator "
			          << coordinator_ip << ":" << coordinator_port << std::endl;
		}

		void PeerManager::HandlePunchFailureReport(const std::string& /*sender_ip*/, int /*sender_port*/,
		                                            const CoordHolePunchFailedPayload* payload) {
			if (!m_isSuperNode) {
				// Only the super node processes failure reports
				return;
			}

			std::string reporter_id(payload->peer_id);
			std::string reporter_ip(payload->peer_ip);
			int reporter_port = ntohs(payload->peer_port);
			uint8_t phase = payload->phase;

			std::cout << "[COORD] Received failure report from peer " << reporter_id
			          << " @ " << reporter_ip << ":" << reporter_port
			          << " for phase " << static_cast<int>(phase) << std::endl;

			// Record the failure report in HolePunchManager
			bool bothFailed = m_punchManager.RecordFailureReport(reporter_id, reporter_ip, reporter_port, phase);

			if (bothFailed) {
				// Both peers have failed this phase - initiate next phase
				if (phase == 0) {
					// Both peers failed single-port - initiate multi-port punch
					std::cout << "[COORD] Both peers failed single-port punching. Initiating multi-port punch." << std::endl;
					
					// Get the list of failed peers
					auto failedPeers = m_punchManager.GetSinglePortFailedPeers();
					
					// For each pair of failed peers, initiate multi-port punch
					if (failedPeers.size() >= 2) {
						// Get external addresses for both peers
						auto peers = GetKnownPeers();
						std::string peerA_ip, peerB_ip;
						int peerA_port = 0, peerB_port = 0;
						
						for (const auto& peer : peers) {
							if (peer.peer_id == failedPeers[0]) {
								peerA_ip = peer.ip;
								peerA_port = peer.port;
							}
							else if (peer.peer_id == failedPeers[1]) {
								peerB_ip = peer.ip;
								peerB_port = peer.port;
							}
						}
						
						if (!peerA_ip.empty() && !peerB_ip.empty() && peerA_port != 0 && peerB_port != 0) {
							// Send multi-port punch start to both peers
							// First peer
							CoordHolePunchMultiStartPayload multiStartA;
							std::memset(&multiStartA, 0, sizeof(multiStartA));
							fai_strncpy(multiStartA.peer_id, failedPeers[1], sizeof(multiStartA.peer_id));
							fai_strncpy(multiStartA.peer_ip, peerB_ip, sizeof(multiStartA.peer_ip));
							multiStartA.peer_base_port = htons(static_cast<int16_t>(peerB_port));
							multiStartA.peer_port_range = 5; // Default 5 ports
							
							SendSecurePacket(m_socket, peerA_ip, peerA_port, PT_COORD_HOLE_PUNCH_MULTI_START,
								&multiStartA, sizeof(multiStartA));
							
							// Second peer
							CoordHolePunchMultiStartPayload multiStartB;
							std::memset(&multiStartB, 0, sizeof(multiStartB));
							fai_strncpy(multiStartB.peer_id, failedPeers[0], sizeof(multiStartB.peer_id));
							fai_strncpy(multiStartB.peer_ip, peerA_ip, sizeof(multiStartB.peer_ip));
							multiStartB.peer_base_port = htons(static_cast<int16_t>(peerA_port));
							multiStartB.peer_port_range = 5;
							
							SendSecurePacket(m_socket, peerB_ip, peerB_port, PT_COORD_HOLE_PUNCH_MULTI_START,
								&multiStartB, sizeof(multiStartB));
							
							std::cout << "[COORD] Sent multi-port punch coordination to both peers." << std::endl;
						}
					}
				}
				else if (phase == 1) {
					// Both peers failed multi-port - trigger proxy fallback
					std::cerr << "[COORD] Both peers failed multi-port punching. Triggering proxy fallback." << std::endl;
					// Proxy fallback will be implemented separately
				}
			}
		}

	}
}