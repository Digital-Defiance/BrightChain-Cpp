# Implementation Plan: C++ Gossip Protocol

## Overview

Incremental implementation of the BrightChain gossip protocol in C++. Each task builds on previous steps, starting with data structures and build system, progressing through core engine logic, networking, and integration. All code uses C++20, Boost.Beast/Asio for networking, nlohmann/json for serialization, and RapidCheck for property-based tests.

## Tasks

- [x] 1. Build system and project scaffolding
  - [x] 1.1 Add vcpkg dependencies and CMake configuration
    - Add `boost-beast`, `boost-asio`, `jwt-cpp`, `miniupnpc`, and `rapidcheck` to `vcpkg.json`
    - Create `src/gossip/CMakeLists.txt` defining a `brightchain_gossip` library target that links `brightchain`, Boost::beast, Boost::asio, miniupnpc, jwt-cpp
    - Update `src/CMakeLists.txt` to `add_subdirectory(gossip)`
    - Create `tests/gossip/CMakeLists.txt` defining a `gossip_tests` executable linking `brightchain_gossip`, GTest, and RapidCheck
    - Update `tests/CMakeLists.txt` to `add_subdirectory(gossip)`
    - Create empty directory structure: `include/brightchain/gossip/`, `src/gossip/`, `tests/gossip/`
    - _Requirements: 13.1, 13.4, 14.1_

- [ ] 2. BlockAnnouncement data structures and serialization
  - [x] 2.1 Implement BlockAnnouncement header and source
    - Create `include/brightchain/gossip/block_announcement.hpp` with all enums (`AnnouncementType`), structs (`MessageDeliveryMetadata`, `DeliveryAckMetadata`, `HeadUpdateMetadata`, `PoolAnnouncementMetadata`, `QuorumProposalMetadata`, `QuorumVoteMetadata`, `WriteProof`, `CblIndexEntry`, `BlockAnnouncement`), and the `AnnouncementType` ↔ string mapping table
    - Create `src/gossip/block_announcement.cpp` implementing `toJson()`, `fromJson()`, and `validate()` with type-specific validation logic per the design
    - Ensure JSON field names match TypeScript wire format exactly (camelCase keys, optional fields omitted when absent)
    - _Requirements: 1.1, 1.10, 1.11, 1.12, 2.1–2.10, 14.1, 14.6_

  - [x] 2.2 Write property test: BlockAnnouncement JSON round-trip
    - **Property 1: BlockAnnouncement JSON round-trip**
    - Create RapidCheck generator for `BlockAnnouncement` covering all 12 types with valid required/optional fields
    - Test: `fromJson(toJson(ann)) == ann` for all generated announcements
    - **Validates: Requirements 1.10, 1.11, 1.12, 14.1, 14.6**

  - [x] 2.3 Write property test: BlockAnnouncement type-specific validation
    - **Property 4: BlockAnnouncement type-specific validation**
    - Generate announcements with correct and incorrect metadata combinations per type
    - Test: `validate()` returns true iff all type-specific constraints are satisfied
    - **Validates: Requirements 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7, 2.8, 2.9, 2.10**

- [ ] 3. GossipConfig
  - [x] 3.1 Implement GossipConfig header and source
    - Create `include/brightchain/gossip/gossip_config.hpp` with `PriorityGossipConfig` and `GossipConfig` structs, `toJson()`, `fromJson()`, `validate()` static method
    - Create `src/gossip/gossip_config.cpp` implementing serialization and validation (all fanout/TTL >= 1)
    - Default values: fanout=3, TTL=3, normal={5,5}, high={7,7}, batchIntervalMs=1000, maxBatchSize=100
    - _Requirements: 1.7, 1.8, 1.9, 14.2_

  - [x] 3.2 Write property test: GossipConfig validation
    - **Property 2: GossipConfig validation**
    - Generate configs with valid and invalid fanout/TTL values
    - Test: `validate()` returns true iff all fanout >= 1 and all TTL >= 1
    - **Validates: Requirements 1.8, 1.9**

  - [x] 3.3 Write property test: GossipConfig JSON round-trip
    - **Property 3: GossipConfig JSON round-trip**
    - Test: `fromJson(toJson(config)) == config` for all valid configs
    - **Validates: Requirements 14.2**

- [x] 4. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 5. BloomFilter
  - [x] 5.1 Implement BloomFilter header and source
    - Create `include/brightchain/gossip/bloom_filter.hpp` and `src/gossip/bloom_filter.cpp`
    - Implement `add()`, `mightContain()`, `serialize()`, `deserialize()` using SHA3-512 truncated offsets for hash functions
    - Constructor takes `expectedItems`, `falsePositiveRate`, `hashCount`
    - Binary serialization: 4-byte LE size prefix + packed bit array
    - _Requirements: 4.1, 4.7_

  - [x] 5.2 Write property test: Bloom filter membership (no false negatives)
    - **Property 26: Bloom filter membership**
    - Generate random sets of block IDs, add all to filter, verify `mightContain()` returns true for every added ID
    - **Validates: Requirements 4.1**

- [x] 6. RetryService
  - [x] 6.1 Implement RetryService header and source
    - Create `include/brightchain/gossip/retry_service.hpp` and `src/gossip/retry_service.cpp`
    - Implement `DeliveryStatus` enum, `RetryConfig`, `PendingDelivery` structs
    - Implement `trackDelivery()`, `handleAck()`, `checkRetries()`, `start()`/`stop()` lifecycle
    - Implement delivery status state machine with valid transitions only
    - Implement exponential backoff: `min(initialTimeoutMs * multiplier^(n-1), maxBackoffMs)`
    - Implement failure handler callbacks when max retries exhausted
    - Note: `start()`/`stop()` use `boost::asio::steady_timer` for periodic retry checks; forward declaration of `GossipEngine` reference is sufficient at this stage
    - _Requirements: 6.1–6.8_

  - [x] 6.2 Write property test: Retry backoff calculation with cap
    - **Property 11: Retry backoff calculation with cap**
    - Generate `RetryConfig` values and retry counts; verify backoff = `min(initial * mult^(n-1), maxBackoff)`
    - **Validates: Requirements 6.2, 6.3**

  - [x] 6.3 Write property test: Delivery status state machine transitions
    - **Property 12: Delivery status state machine transitions**
    - Generate all (from, to) pairs; verify only valid transitions accepted
    - **Validates: Requirements 6.8**

  - [x] 6.4 Write property test: Retry exhaustion marks Failed
    - **Property 13: Retry exhaustion marks recipients as Failed**
    - Generate deliveries at max retries; verify all Announced/Pending recipients become Failed
    - **Validates: Requirements 6.4, 6.6**

  - [x] 6.5 Write property test: Ack removes fully-delivered from tracking
    - **Property 14: Ack removes fully-delivered from tracking**
    - Generate deliveries where all recipients reach Delivered/Read; verify removal from tracking
    - **Validates: Requirements 6.5**

  - [x] 6.6 Write property test: Initial tracking sets Announced status
    - **Property 15: Initial tracking sets Announced status**
    - Track new deliveries; verify all N recipients have Announced status and correct nextRetryAt
    - **Validates: Requirements 6.1**

- [x] 7. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 8. PeerManager
  - [x] 8.1 Implement PeerManager header and source
    - Create `include/brightchain/gossip/peer_manager.hpp` and `src/gossip/peer_manager.cpp`
    - Implement `PeerInfo` struct, peer registry with `std::shared_mutex` protection
    - Implement `addBootstrapNodes()`, `startDiscovery()`, `connectToPeer()`, `disconnectPeer()`
    - Implement `getConnectedPeers()`, `getPeer()`, `getConnectedPeerIds()`
    - Implement `sendToPeer()`, `broadcastToPeers()` using Boost.Beast WebSocket sessions
    - Implement pool discovery cache: `updatePoolCache()`, `removePoolFromCache()`
    - Implement ECIES challenge/response authentication for node-to-node connections
    - Implement heartbeat ping/pong with configurable timeout
    - Implement reconnection with exponential backoff: `min(1 * 2^n, 60)` seconds
    - _Requirements: 3.1–3.7, 8.3, 8.4_

  - [x] 8.2 Write property test: Peer registry completeness
    - **Property 29: Peer registry completeness**
    - Add peers with all fields; verify retrieval returns complete PeerInfo
    - **Validates: Requirements 3.2**

  - [x] 8.3 Write property test: ECIES challenge/response round-trip
    - **Property 30: ECIES challenge/response round-trip**
    - Generate random EcKeyPairs and challenges; verify encrypt→decrypt produces original bytes
    - **Validates: Requirements 3.4, 14.5**

  - [x] 8.4 Write property test: Peer reconnection backoff
    - **Property 31: Peer reconnection backoff**
    - Generate reconnection attempt numbers; verify delay = `min(1 * 2^n, 60)` seconds
    - **Validates: Requirements 3.5**

- [ ] 9. GossipEngine core
  - [x] 9.1 Implement GossipEngine header and source
    - Create `include/brightchain/gossip/gossip_engine.hpp` and `src/gossip/gossip_engine.cpp`
    - Implement announcement creation methods: `announceBlock()`, `announceRemoval()`, `announceMessage()`, `sendDeliveryAck()`, `announceHeadUpdate()`, `announcePoolCreation()`, `announcePoolRemoval()`, `announcePoolDeletion()`, `announceQuorumProposal()`, `announceQuorumVote()`, `announceCblIndexUpdate()`, `announceCblIndexDelete()`, `announceAclUpdate()`
    - Implement `queueAnnouncement()` with `std::shared_mutex`-protected pending queue
    - Implement `batchFlush()` with `maxBatchSize` enforcement using `boost::asio::steady_timer`
    - Implement `forwardAnnouncement()` with TTL decrement and `selectRandomPeers()` using Fisher-Yates shuffle
    - Implement `handleAnnouncement()` with validation, local delivery matching, and forwarding logic
    - Implement event handler registration: `onAnnouncement()`, `onMessageDelivery()`, `onDeliveryAck()`, `onQuorumProposal()`, `onQuorumVote()`
    - Implement `encryptAndSendBatch()` using existing ECIES for sensitive metadata
    - Implement `start()`/`stop()` lifecycle with io_context integration
    - _Requirements: 1.1–1.12, 5.1–5.8, 7.1–7.4, 8.1–8.2, 9.1–9.5, 13.1–13.6_

  - [x] 9.2 Write property test: TTL decrement and fanout count
    - **Property 5: TTL decrement and fanout count on forward**
    - Generate announcements with TTL > 0 and peer sets; verify TTL decremented by 1, sent to min(fanout, N) peers
    - **Validates: Requirements 1.3**

  - [x] 9.3 Write property test: Batch size enforcement
    - **Property 6: Batch size enforcement**
    - Generate announcement sequences > maxBatchSize; verify batches respect max size and total equals input
    - **Validates: Requirements 1.6**

  - [x] 9.4 Write property test: Priority-based fanout and TTL
    - **Property 7: Priority-based fanout and TTL assignment**
    - Generate message announcements with priorities; verify TTL and fanout match config
    - **Validates: Requirements 1.2, 5.2, 5.3**

  - [x] 9.5 Write property test: Local recipient matching
    - **Property 8: Local recipient matching determines forwarding behavior**
    - Generate announcements with recipientIds and localUserIds; verify local delivery vs forwarding
    - **Validates: Requirements 5.4, 5.5**

  - [x] 9.6 Write property test: Auto-ack on delivery
    - **Property 9: Auto-ack on delivery with ackRequired**
    - Generate local deliveries with ackRequired=true; verify exactly one ack announcement queued
    - **Validates: Requirements 5.6**

- [x] 10. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 11. DiscoveryProtocol
  - [x] 11.1 Implement DiscoveryProtocol header and source
    - Create `include/brightchain/gossip/discovery_protocol.hpp` and `src/gossip/discovery_protocol.cpp`
    - Implement `DiscoveryConfig`, `LocationRecord`, `DiscoveryResult`, `CblSearchResult` structs
    - Implement `discoverBlock()` with cache check → Bloom filter pre-check → concurrent peer queries (limited to `maxConcurrentQueries`)
    - Implement result caching with configurable TTL (default 60s) using `std::shared_mutex`
    - Implement `getLocalBloomFilter()` from DiskBlockStore contents
    - Implement `searchCblMetadata()` for file name, MIME type, and tag filtering
    - Sort discovery results by peer latency ascending
    - _Requirements: 4.1–4.8_

  - [x] 11.2 Write property test: Discovery results sorted by latency
    - **Property 27: Discovery results sorted by latency**
    - Generate discovery results with multiple locations; verify ascending latency order
    - **Validates: Requirements 4.5**

  - [x] 11.3 Write property test: Bloom filter pre-check filtering
    - **Property 28: Bloom filter pre-check filtering**
    - Generate peer sets with Bloom filters; verify only peers with mightContain==true are queried
    - **Validates: Requirements 4.3**

- [ ] 12. ECIES batch encryption integration
  - [x] 12.1 Implement ECIES batch encryption in GossipEngine
    - Wire `encryptAndSendBatch()` to use existing `Ecies::encryptBasic()` / `Ecies::decrypt()` for batches containing `messageDelivery` or `deliveryAck` metadata
    - Implement plaintext fallback with warning log when peer public key is unavailable
    - Create `tests/gossip/ecies_batch_test.cpp` with unit tests for encryption edge cases
    - _Requirements: 5.7, 5.8, 13.4_

  - [x] 12.2 Write property test: ECIES batch encryption round-trip
    - **Property 10: Sensitive batch ECIES encryption round-trip**
    - Generate batches with sensitive metadata and random EcKeyPairs; verify encrypt→decrypt produces original batch
    - **Validates: Requirements 5.7**

- [ ] 13. Head registry synchronization
  - [x] 13.1 Implement head sync logic in GossipEngine
    - Wire `announceHeadUpdate()` to create `head_update` announcements with `HeadUpdateMetadata` and optional `WriteProof`
    - Implement `handleAnnouncement()` case for `head_update`: verify writeProof signature if present, then call `HeadRegistry::setHead()`
    - Signature verification: ECDSA over `dbName + collectionName + blockId` using `signerPublicKey`
    - Create `tests/gossip/head_sync_test.cpp` with unit tests for signature verification
    - _Requirements: 7.1–7.4, 13.5_

  - [x] 13.2 Write property test: Head update announcement creation
    - **Property 16: Head update announcement creation**
    - Generate dbName/collectionName/blockId triples; verify announcement fields match
    - **Validates: Requirements 7.1**

  - [x] 13.3 Write property test: Head update signature verification
    - **Property 17: Head update signature verification**
    - Generate writeProofs with valid and invalid signatures; verify accept/reject behavior
    - **Validates: Requirements 7.3**

- [ ] 14. Pool lifecycle via gossip
  - [x] 14.1 Implement pool lifecycle in GossipEngine and PeerManager
    - Wire `announcePoolCreation()`, `announcePoolRemoval()`, `announcePoolDeletion()` to create correct announcement types
    - Wire `handleAnnouncement()` for `pool_announce` → `PeerManager::updatePoolCache()` and `pool_deleted` → `PeerManager::removePoolFromCache()`
    - Implement ECIES-encrypted `encryptedMetadata` for pools with `encrypted == true`
    - Create `tests/gossip/pool_lifecycle_test.cpp` with unit tests for cache operations
    - _Requirements: 8.1–8.5_

  - [x] 14.2 Write property test: Pool cache reflects announcements
    - **Property 18: Pool cache reflects announcements**
    - Generate pool_announce and pool_deleted sequences; verify cache state matches
    - **Validates: Requirements 8.3, 8.4**

  - [x] 14.3 Write property test: Encrypted pool metadata
    - **Property 19: Encrypted pool metadata in announcements**
    - Generate encrypted pools; verify encryptedMetadata is non-empty and decryptable by authorized member
    - **Validates: Requirements 8.5**

- [ ] 15. Quorum proposal and vote propagation
  - [x] 15.1 Implement quorum gossip in GossipEngine
    - Wire `announceQuorumProposal()` and `announceQuorumVote()` to create announcements with high-priority fanout/TTL
    - Wire `handleAnnouncement()` for `quorum_proposal` → trigger proposal handlers, `quorum_vote` → trigger vote handlers
    - Preserve `encryptedShare` bytes through serialization round-trip
    - Create `tests/gossip/quorum_gossip_test.cpp` with unit tests for share preservation
    - _Requirements: 9.1–9.5_

  - [x] 15.2 Write property test: Quorum uses high-priority config
    - **Property 20: Quorum announcements use high-priority config**
    - Generate quorum announcements; verify TTL and fanout match `messagePriority.high`
    - **Validates: Requirements 9.1, 9.2**

  - [x] 15.3 Write property test: Encrypted share preservation
    - **Property 21: Encrypted share preservation in approve votes**
    - Generate quorum_vote with encryptedShare; verify bytes preserved through toJson→fromJson
    - **Validates: Requirements 9.5**

- [x] 16. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 17. WebSocketServer
  - [x] 17.1 Implement WebSocketServer header and source
    - Create `include/brightchain/gossip/websocket_server.hpp` and `src/gossip/websocket_server.cpp`
    - Implement Boost.Beast WebSocket acceptor on configurable address:port
    - Implement `/ws/node/:nodeId` endpoint with ECIES challenge/response authentication
    - Implement `/ws/client?token=<jwt>` endpoint with JWT authentication using jwt-cpp
    - Implement client event subscriptions for: pool:changed, pool:created, pool:deleted, energy:updated, storage:alert, peer:connected, peer:disconnected
    - Implement access tier filtering: Admin events → Admin/System only, Pool-Scoped → Read permission, Member-Scoped → target member only
    - Implement ping/pong heartbeat with configurable timeout; close on pong timeout
    - Implement JWT expiry handling: send `auth:token_expiring` event, close with code 4002 after grace period
    - Create `tests/gossip/websocket_server_test.cpp` with unit tests for auth flows
    - _Requirements: 10.1–10.7, 14.3_

  - [x] 17.2 Write property test: WebSocket event access tier filtering
    - **Property 22: WebSocket event access tier filtering**
    - Generate events with access tiers and clients with MemberTypes; verify delivery rules
    - **Validates: Requirements 10.5**

- [ ] 18. REST Introspection API
  - [x] 18.1 Implement IntrospectionApi header and source
    - Create `include/brightchain/gossip/introspection_api.hpp` and `src/gossip/introspection_api.cpp`
    - Implement Boost.Beast HTTP request handler with route registration
    - Implement `GET /api/introspection/status` — nodeId, healthy, uptime, version, capabilities, partitionMode
    - Implement `GET /api/introspection/peers` — Admin/System only, returns connected peer info
    - Implement `GET /api/introspection/pools` — filtered by requesting member's Read permissions
    - Implement `GET /api/introspection/stats` — Admin/System only, returns block store statistics
    - Implement `POST /api/introspection/discover-pools` — queries peers, deduplicates, filters by ACL
    - Implement JWT validation on all endpoints; return 401 for missing/expired tokens, 403 for insufficient permissions
    - Response envelope: `{"message": "...", "data": {...}}` matching TypeScript format
    - Create `tests/gossip/introspection_api_test.cpp` with unit tests for endpoint responses
    - _Requirements: 11.1–11.7, 14.4_

  - [x] 18.2 Write property test: REST admin access control
    - **Property 23: REST admin access control**
    - Generate requests with different MemberTypes to admin endpoints; verify 200 vs 403
    - **Validates: Requirements 11.2, 11.4, 11.7**

  - [x] 18.3 Write property test: REST JWT authentication
    - **Property 24: REST JWT authentication**
    - Generate requests with missing, expired, and malformed tokens; verify 401 responses
    - **Validates: Requirements 11.6**

  - [x] 18.4 Write property test: REST pool filtering by permissions
    - **Property 25: REST pool filtering by permissions**
    - Generate members with varying Read permissions; verify response contains exactly permitted pools
    - **Validates: Requirements 11.3**

- [ ] 19. UPnP Manager
  - [ ] 19.1 Implement UpnpManager header and source
    - Create `include/brightchain/gossip/upnp_manager.hpp` and `src/gossip/upnp_manager.cpp`
    - Implement `UpnpConfig` with defaults: disabled, httpPort=3000, wsPort=3000, ttlSeconds=3600, refreshIntervalMs=1800000, retryAttempts=3, retryDelayMs=5000
    - Implement `initialize()` using miniupnpc: discover IGD, get external IP, create port mappings
    - Implement `shutdown()` to remove all mappings
    - Implement refresh timer with configurable interval
    - Implement retry with exponential backoff: `retryDelayMs * 2^n` up to `retryAttempts`
    - Validate `refreshIntervalMs < ttlSeconds * 1000`
    - Provide external IP and mapped ports to PeerManager
    - Create `tests/gossip/upnp_manager_test.cpp` with unit tests for config validation
    - _Requirements: 12.1–12.7_

  - [ ] 19.2 Write property test: UPnP refresh interval less than TTL
    - **Property 32: UPnP refresh interval less than TTL**
    - Generate UpnpConfig values; verify `refreshIntervalMs < ttlSeconds * 1000` constraint
    - **Validates: Requirements 12.3**

  - [ ] 19.3 Write property test: UPnP retry backoff
    - **Property 33: UPnP retry backoff**
    - Generate retry attempt numbers; verify delay = `retryDelayMs * 2^n`
    - **Validates: Requirements 12.4**

- [ ] 20. Checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 21. Integration wiring and existing component connections
  - [ ] 21.1 Wire GossipEngine to DiskBlockStore and BrightChainDB
    - In `handleAnnouncement()` for `add` type: retrieve block from announcing peer via PeerManager, store in local DiskBlockStore
    - In `handleAnnouncement()` for `remove` type: remove block from local DiskBlockStore if present
    - Wire `head_update` handling to call `HeadRegistry::setHead()` after signature verification
    - Use existing `Member` class for node identity in all announcement signing
    - Use existing `CBL`/`ExtendedCBL` for message content storage during delivery
    - _Requirements: 13.1–13.6_

  - [ ] 21.2 Wire PeerManager ↔ WebSocketServer ↔ GossipEngine
    - Connect WebSocketServer incoming messages to `GossipEngine::handleAnnouncement()`
    - Connect GossipEngine outgoing batches to PeerManager's `sendToPeer()`/`broadcastToPeers()`
    - Connect PeerManager connection events to WebSocketServer session management
    - Wire UPnP external IP/ports into PeerManager for peer advertisement
    - Wire RetryService to GossipEngine for re-announcement on retry
    - _Requirements: 3.1, 10.1, 10.3, 12.6_

  - [ ] 21.3 Wire IntrospectionApi to all components
    - Connect IntrospectionApi to GossipEngine, PeerManager, DiskBlockStore, DiscoveryProtocol
    - Register HTTP routes on the Boost.Beast HTTP server alongside WebSocket upgrade handling
    - _Requirements: 11.1–11.5, 14.4_

- [ ] 22 . E2E testing - Full typescript server running, gossip server roundtrip testing
  - Write round trip full cross-suite integration tests
  - Ensure tests pass

- [ ] 23. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.


## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document (33 total)
- Unit tests validate specific examples, edge cases, and integration points
- All 12 announcement types and 33 correctness properties are covered
- The gossip library (`brightchain_gossip`) is a separate CMake target linking against the existing `brightchain` library
