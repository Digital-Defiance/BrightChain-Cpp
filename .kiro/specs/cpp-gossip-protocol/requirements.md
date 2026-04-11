# Requirements Document

## Introduction

This document specifies the requirements for implementing the BrightChain gossip protocol in C++, enabling C++ nodes to fully participate in the BrightChain peer-to-peer network. The implementation must be wire-compatible with the existing TypeScript implementation so that C++ and TypeScript nodes can interoperate seamlessly. The scope covers the core gossip engine, peer management, block discovery, message delivery, head registry synchronization, pool lifecycle, quorum propagation, WebSocket communication, REST introspection API, NAT traversal, and extensibility for web applications.

The server is designed to be future-forward: it serves as a full application server capable of hosting web applications such as BrightChat, BrightMail, and BrightHub. This is achieved through pluggable HTTP route modules, static file serving, a modular middleware pipeline, and WebSocket channel namespacing.

## Glossary

- **Gossip_Engine**: The core C++ component responsible for creating, batching, forwarding, and receiving block announcements using epidemic-style propagation with configurable fanout and TTL parameters.
- **Block_Announcement**: A JSON message describing a block event (add, remove, ack, head_update, pool_announce, pool_remove, pool_deleted, cbl_index_update, cbl_index_delete, acl_update, quorum_proposal, quorum_vote) propagated through the gossip network.
- **Peer_Manager**: The C++ component responsible for discovering, connecting to, authenticating, and maintaining WebSocket connections with other BrightChain nodes.
- **Discovery_Protocol**: The C++ component that locates blocks across the network using Bloom filter pre-checks, concurrent query limiting, result caching, and latency-based node preference.
- **Retry_Service**: The C++ component that tracks pending message deliveries and retries unacknowledged deliveries with exponential backoff.
- **WebSocket_Server**: The C++ component providing node-to-node WebSocket communication at `/ws/node/:nodeId` with ECIES authentication and client WebSocket communication at `/ws/client` with JWT authentication.
- **REST_API**: The C++ HTTP server component providing introspection endpoints for node status, peers, pools, stats, and energy.
- **UPnP_Manager**: The C++ component that manages UPnP/NAT-PMP port mappings for node accessibility from external networks.
- **Route_Module**: A pluggable HTTP handler module that registers a set of API endpoints under a URL prefix, enabling web applications like BrightChat or BrightMail to extend the server's REST API.
- **Middleware_Pipeline**: An ordered chain of request-processing functions that execute before route handlers, providing cross-cutting concerns such as authentication, CORS, rate limiting, and logging.
- **WebSocket_Namespace**: A channel prefix under `/ws/app/{namespace}` that isolates real-time WebSocket event streams for a specific application module, separate from gossip protocol traffic.
- **Static_File_Server**: The server component that serves HTML, JavaScript, CSS, and asset files from configured filesystem directories, enabling web frontends to be hosted directly by the node.
- **Fanout**: The number of randomly selected peers to which an announcement is forwarded at each hop.
- **TTL**: Time-to-live; the number of remaining hops before an announcement stops propagating. Decremented by 1 at each forwarding node.
- **Bloom_Filter**: A probabilistic data structure used to test whether a block might exist on a peer, reducing unnecessary network queries.
- **ECIES_Authenticator**: The authentication mechanism using Elliptic Curve Integrated Encryption Scheme challenge/response for node-to-node WebSocket connections.
- **DiskBlockStore**: The existing C++ block storage component with hierarchical directory structure.
- **BrightChainDB**: The existing C++ document database with collections, queries, and head registry.
- **HeadRegistry**: The existing C++ component that maps collection keys to their latest block IDs, persisted in head-registry.json.
- **Member**: The existing C++ class representing a network participant with cryptographic key pairs (secp256k1), signing, and verification capabilities.
- **Pool**: A named grouping of blocks with access control, encryption, and membership management.
- **Quorum**: A governance mechanism where proposals are submitted and voted on by authorized members using Paillier homomorphic encryption.

## Requirements

### Requirement 1: Core Gossip Engine

**User Story:** As a C++ node operator, I want the node to propagate block announcements through the network using epidemic-style gossip, so that all nodes learn about new blocks, removals, and other events without centralized coordination.

#### Acceptance Criteria

1. THE Gossip_Engine SHALL support all 12 Block_Announcement types: add, remove, ack, pool_deleted, cbl_index_update, cbl_index_delete, head_update, acl_update, pool_announce, pool_remove, quorum_proposal, and quorum_vote.
2. WHEN a Block_Announcement is created, THE Gossip_Engine SHALL assign a TTL value based on the announcement type and priority configuration.
3. WHEN the Gossip_Engine forwards a Block_Announcement, THE Gossip_Engine SHALL decrement the TTL by 1 and select a random subset of connected peers equal to the configured fanout value using Fisher-Yates shuffle.
4. WHEN a Block_Announcement with TTL equal to 0 is received, THE Gossip_Engine SHALL process the announcement locally and not forward the announcement to any peer.
5. THE Gossip_Engine SHALL batch pending announcements and flush them at a configurable interval with a default of 1000 milliseconds.
6. THE Gossip_Engine SHALL enforce a configurable maximum batch size with a default of 100 announcements per batch.
7. THE Gossip_Engine SHALL use default gossip configuration values of fanout 3 and TTL 3 for block-level announcements, fanout 5 and TTL 5 for normal-priority message delivery, and fanout 7 and TTL 7 for high-priority message delivery.
8. WHEN a GossipConfig is provided, THE Gossip_Engine SHALL validate that all fanout values are positive integers greater than or equal to 1 and all TTL values are positive integers greater than or equal to 1.
9. IF an invalid GossipConfig is provided, THEN THE Gossip_Engine SHALL reject the configuration and report a descriptive error.
10. THE Gossip_Engine SHALL serialize Block_Announcement objects to JSON using the same field names and structure as the TypeScript implementation for wire compatibility.
11. WHEN the Gossip_Engine receives a JSON-serialized Block_Announcement from a peer, THE Gossip_Engine SHALL deserialize the announcement and validate all required fields according to the announcement type.
12. FOR ALL valid Block_Announcement objects, serializing to JSON then deserializing from JSON SHALL produce an equivalent Block_Announcement object (round-trip property).

### Requirement 2: Block Announcement Validation

**User Story:** As a C++ node operator, I want incoming announcements to be validated before processing, so that malformed or malicious announcements are rejected.

#### Acceptance Criteria

1. THE Gossip_Engine SHALL validate that messageDelivery metadata is present only on add-type announcements.
2. THE Gossip_Engine SHALL validate that deliveryAck metadata is present only on ack-type announcements.
3. WHEN a pool_deleted announcement is received, THE Gossip_Engine SHALL validate that a valid poolId is present and that messageDelivery and deliveryAck metadata are absent.
4. WHEN a cbl_index_update or cbl_index_delete announcement is received, THE Gossip_Engine SHALL validate that a valid poolId and a cblIndexEntry with non-empty magnetUrl, blockId1, and blockId2 are present.
5. WHEN a head_update announcement is received, THE Gossip_Engine SHALL validate that a non-empty blockId and headUpdate metadata with non-empty dbName and collectionName are present.
6. WHEN an acl_update announcement is received, THE Gossip_Engine SHALL validate that a valid poolId and a non-empty aclBlockId are present.
7. WHEN a pool_announce announcement is received, THE Gossip_Engine SHALL validate that a valid poolId and poolAnnouncement metadata with numeric blockCount, numeric totalSize, and boolean encrypted fields are present.
8. WHEN a quorum_proposal announcement is received, THE Gossip_Engine SHALL validate that quorumProposal metadata is present with a non-empty proposalId, a description of 4096 characters or fewer, a non-empty proposerMemberId, and a requiredThreshold of 1 or greater.
9. WHEN a quorum_vote announcement is received, THE Gossip_Engine SHALL validate that quorumVote metadata is present with a non-empty proposalId, a non-empty voterMemberId, a decision of either approve or reject, and an optional comment of 1024 characters or fewer.
10. IF a Block_Announcement fails validation, THEN THE Gossip_Engine SHALL discard the announcement and not forward the announcement to any peer.

### Requirement 3: Peer Management and Discovery

**User Story:** As a C++ node operator, I want the node to discover and maintain connections with other BrightChain nodes, so that the gossip network remains connected and resilient.

#### Acceptance Criteria

1. THE Peer_Manager SHALL discover peers using three mechanisms: local network multicast, bootstrap node lists, and DNS seed queries.
2. THE Peer_Manager SHALL maintain a registry of connected peers including nodeId, address, HTTP port, WebSocket port, last-seen timestamp, capabilities, and connection status.
3. WHEN a new peer is discovered, THE Peer_Manager SHALL initiate a WebSocket connection to the peer at the `/ws/node/:nodeId` endpoint.
4. THE Peer_Manager SHALL authenticate node-to-node WebSocket connections using ECIES challenge/response where the connecting node proves possession of the private key corresponding to its advertised public key.
5. WHEN a peer connection is lost, THE Peer_Manager SHALL attempt reconnection with exponential backoff starting at 1 second and capping at 60 seconds.
6. THE Peer_Manager SHALL send periodic heartbeat (ping) messages to connected peers and mark peers as disconnected when no response is received within a configurable timeout.
7. THE Peer_Manager SHALL track per-peer latency measurements for use by the Discovery_Protocol in latency-based node preference.

### Requirement 4: Block Discovery Protocol

**User Story:** As a C++ node operator, I want the node to efficiently locate blocks across the network, so that block retrieval does not require querying every peer.

#### Acceptance Criteria

1. THE Discovery_Protocol SHALL maintain a Bloom filter representing the blocks stored locally and provide the Bloom filter to peers on request.
2. WHEN discovering a block, THE Discovery_Protocol SHALL first check a local result cache with a configurable TTL defaulting to 60 seconds.
3. WHEN the cache does not contain a result, THE Discovery_Protocol SHALL query each connected peer's Bloom filter and only send direct queries to peers whose Bloom filter indicates the block might be present.
4. THE Discovery_Protocol SHALL limit concurrent peer queries to a configurable maximum defaulting to 10.
5. THE Discovery_Protocol SHALL return discovery results sorted by peer latency in ascending order.
6. THE Discovery_Protocol SHALL support pool-scoped discovery using pool-scoped Bloom filters when a poolId is provided.
7. THE Discovery_Protocol SHALL use a configurable Bloom filter false positive rate defaulting to 0.01 and a configurable hash count defaulting to 7.
8. THE Discovery_Protocol SHALL support CBL metadata search across peers, filtering by file name substring, MIME type exact match, and tag intersection.

### Requirement 5: Message Delivery via Gossip

**User Story:** As a C++ node operator, I want the node to deliver messages through the gossip network with priority-based fanout and delivery acknowledgments, so that messages reach their intended recipients reliably.

#### Acceptance Criteria

1. WHEN a message is announced, THE Gossip_Engine SHALL create add-type Block_Announcements with MessageDeliveryMetadata containing messageId, recipientIds, priority, blockIds, cblBlockId, and ackRequired fields.
2. WHEN a message announcement with normal priority is created, THE Gossip_Engine SHALL apply fanout of 5 and TTL of 5 from the gossip configuration.
3. WHEN a message announcement with high priority is created, THE Gossip_Engine SHALL apply fanout of 7 and TTL of 7 from the gossip configuration.
4. WHEN a message announcement is received and the recipientIds match a local user, THE Gossip_Engine SHALL trigger message delivery handlers and not forward the announcement further.
5. WHEN a message announcement is received and the recipientIds do not match any local user, THE Gossip_Engine SHALL forward the announcement with decremented TTL.
6. WHEN ackRequired is true and a message is delivered to a local recipient, THE Gossip_Engine SHALL automatically create and queue an ack-type Block_Announcement with DeliveryAckMetadata.
7. WHEN an announcement contains messageDelivery or deliveryAck metadata, THE Gossip_Engine SHALL encrypt the batch per-peer using ECIES with the peer's public key before sending.
8. IF ECIES encryption is unavailable for a peer due to missing public key, THEN THE Gossip_Engine SHALL send the announcement in plaintext and log a warning.

### Requirement 6: Delivery Retry Service

**User Story:** As a C++ node operator, I want unacknowledged message deliveries to be retried automatically with exponential backoff, so that transient network failures do not cause permanent message loss.

#### Acceptance Criteria

1. WHEN a message delivery is tracked, THE Retry_Service SHALL set all recipients to Announced status and schedule the first retry after a configurable initial timeout defaulting to 30 seconds.
2. THE Retry_Service SHALL use exponential backoff with a configurable multiplier defaulting to 2, computing delay as initialTimeout multiplied by multiplier raised to the power of (retryCount minus 1).
3. THE Retry_Service SHALL cap the backoff delay at a configurable maximum defaulting to 240 seconds.
4. THE Retry_Service SHALL attempt a configurable maximum number of retries defaulting to 5.
5. WHEN a delivery acknowledgment is received, THE Retry_Service SHALL update the recipient status and remove the delivery from tracking when all recipients reach Delivered or Read status.
6. WHEN the maximum retry count is exhausted, THE Retry_Service SHALL mark all unacknowledged recipients as Failed, update the metadata store, and emit a message-failed event.
7. THE Retry_Service SHALL check for pending retries every 1 second.
8. THE Retry_Service SHALL validate delivery status transitions using the state machine: Announced transitions to Pending, Pending transitions to Delivered or Failed or Bounced, and Delivered transitions to Read.

### Requirement 7: Head Registry Synchronization

**User Story:** As a C++ node operator, I want head registry updates to propagate across nodes via gossip, so that all nodes maintain a consistent view of collection state.

#### Acceptance Criteria

1. WHEN a local HeadRegistry entry is updated, THE Gossip_Engine SHALL create a head_update Block_Announcement containing the dbName, collectionName, and new blockId.
2. WHEN a head_update announcement is received from a peer, THE Gossip_Engine SHALL update the local HeadRegistry with the new blockId for the specified dbName and collectionName.
3. WHEN a head_update announcement includes a writeProof, THE Gossip_Engine SHALL verify the signature using the signer's public key before applying the update.
4. IF a head_update writeProof signature verification fails, THEN THE Gossip_Engine SHALL discard the announcement and not apply the update.

### Requirement 8: Pool Lifecycle via Gossip

**User Story:** As a C++ node operator, I want pool creation, updates, and deletions to propagate through the gossip network, so that all nodes can discover and track available pools.

#### Acceptance Criteria

1. WHEN a pool is created or updated locally, THE Gossip_Engine SHALL create a pool_announce Block_Announcement with PoolAnnouncementMetadata containing blockCount, totalSize, and encrypted status.
2. WHEN a pool is deleted locally, THE Gossip_Engine SHALL create a pool_remove Block_Announcement with the poolId.
3. WHEN a pool_announce announcement is received, THE Peer_Manager SHALL update the local pool discovery cache with the pool metadata and hosting node information.
4. WHEN a pool_deleted announcement is received, THE Peer_Manager SHALL remove the pool from the local pool discovery cache.
5. WHEN a pool has encryption enabled, THE Gossip_Engine SHALL include ECIES-encrypted pool metadata in the encryptedMetadata field of the PoolAnnouncementMetadata so that only authorized members can read pool details.

### Requirement 9: Quorum Proposal and Vote Propagation

**User Story:** As a C++ node operator, I want quorum proposals and votes to propagate through the gossip network, so that distributed governance decisions can be made across the network.

#### Acceptance Criteria

1. WHEN a quorum proposal is submitted locally, THE Gossip_Engine SHALL create a quorum_proposal Block_Announcement with QuorumProposalMetadata and propagate the announcement using high-priority fanout and TTL.
2. WHEN a quorum vote is cast locally, THE Gossip_Engine SHALL create a quorum_vote Block_Announcement with QuorumVoteMetadata and propagate the announcement using high-priority fanout and TTL.
3. WHEN a quorum_proposal announcement is received, THE Gossip_Engine SHALL trigger quorum proposal handlers so that the local node can track and display the proposal.
4. WHEN a quorum_vote announcement is received, THE Gossip_Engine SHALL trigger quorum vote handlers so that the local node can tally votes.
5. WHEN a quorum_vote with decision approve includes an encryptedShare, THE Gossip_Engine SHALL preserve the encrypted share data for the proposer to collect and reconstruct the secret using Shamir's Secret Sharing.

### Requirement 10: WebSocket Communication

**User Story:** As a C++ node operator, I want the node to communicate with peers and clients over WebSocket connections, so that real-time gossip propagation and client event subscriptions are supported.

#### Acceptance Criteria

1. THE WebSocket_Server SHALL listen for node-to-node connections at the `/ws/node/:nodeId` endpoint and authenticate connections using ECIES challenge/response.
2. THE WebSocket_Server SHALL listen for client connections at the `/ws/client` endpoint and authenticate connections using JWT tokens passed as a query parameter.
3. WHEN a node-to-node WebSocket connection is established, THE WebSocket_Server SHALL exchange gossip announcements as JSON-serialized messages.
4. THE WebSocket_Server SHALL support client event subscriptions for event types: pool:changed, pool:created, pool:deleted, energy:updated, storage:alert, peer:connected, and peer:disconnected.
5. THE WebSocket_Server SHALL filter events by access tier: Admin events delivered only to Admin or System members, Pool-Scoped events delivered only to members with Read permission, and Member-Scoped events delivered only to the target member.
6. THE WebSocket_Server SHALL send periodic ping frames to connected peers and close connections that do not respond with a pong within the configured timeout.
7. WHEN a client JWT token expires during an active session, THE WebSocket_Server SHALL send an auth:token_expiring event and close the connection with close code 4002 after a grace period.

### Requirement 11: REST Introspection API

**User Story:** As a C++ node operator, I want the node to expose REST endpoints for status, peers, pools, and statistics, so that monitoring tools and the Lumen GUI client can inspect node state.

#### Acceptance Criteria

1. THE REST_API SHALL expose a GET /api/introspection/status endpoint returning nodeId, healthy status, uptime, version, capabilities, and partitionMode accessible to any authenticated member.
2. THE REST_API SHALL expose a GET /api/introspection/peers endpoint returning connected peer information including nodeId, connected status, lastSeen timestamp, and latencyMs, restricted to Admin and System members.
3. THE REST_API SHALL expose a GET /api/introspection/pools endpoint returning pool summaries filtered by the requesting member's Read permissions.
4. THE REST_API SHALL expose a GET /api/introspection/stats endpoint returning block store statistics including totalCapacity, currentUsage, availableSpace, and blockCounts, restricted to Admin and System members.
5. THE REST_API SHALL expose a POST /api/introspection/discover-pools endpoint that queries connected peers for pool lists, deduplicates results, filters by ACL and encryption, and returns aggregated pool information.
6. THE REST_API SHALL require a valid JWT bearer token on all endpoints and return HTTP 401 for missing or expired tokens.
7. THE REST_API SHALL return HTTP 403 when a User-type member attempts to access an Admin-restricted endpoint.

### Requirement 12: NAT Traversal

**User Story:** As a C++ node operator, I want the node to automatically configure port mappings on the local router, so that the node is accessible from external networks without manual configuration.

#### Acceptance Criteria

1. THE UPnP_Manager SHALL support UPnP and NAT-PMP protocols with auto-detection fallback.
2. WHEN UPnP is enabled, THE UPnP_Manager SHALL create port mappings for the configured HTTP and WebSocket ports with a configurable TTL defaulting to 3600 seconds.
3. THE UPnP_Manager SHALL refresh port mappings at a configurable interval defaulting to 1800 seconds, which is less than the mapping TTL.
4. WHEN port mapping creation fails, THE UPnP_Manager SHALL retry with exponential backoff up to a configurable maximum of 3 attempts.
5. WHEN the node shuts down, THE UPnP_Manager SHALL remove all port mappings before closing.
6. THE UPnP_Manager SHALL provide the external IP address and mapped ports to the Peer_Manager for peer advertisement.
7. THE UPnP_Manager SHALL be disabled by default and require explicit opt-in configuration.

### Requirement 13: Integration with Existing C++ Components

**User Story:** As a C++ node operator, I want the gossip protocol to integrate with the existing block store, database, member management, and cryptography components, so that the node functions as a complete BrightChain participant.

#### Acceptance Criteria

1. WHEN a block add announcement is received and the block is not present locally, THE Gossip_Engine SHALL retrieve the block from the announcing peer and store the block in the local DiskBlockStore.
2. WHEN a block remove announcement is received, THE Gossip_Engine SHALL remove the block from the local DiskBlockStore if present.
3. THE Gossip_Engine SHALL use the existing Member class for node identity, signing announcements with the node's private key, and verifying announcement signatures with peer public keys.
4. THE Gossip_Engine SHALL use the existing ECIES implementation for encrypting sensitive announcement batches per-peer and for node-to-node WebSocket authentication.
5. THE Gossip_Engine SHALL use the existing BrightChainDB and HeadRegistry for persisting and synchronizing collection state across nodes.
6. THE Gossip_Engine SHALL use the existing CBL and ExtendedCBL block types for message content storage and retrieval during message delivery.

### Requirement 15: Pluggable HTTP Route Modules

**User Story:** As a web application developer, I want to register custom HTTP route modules on the server, so that applications like BrightChat, BrightMail, and BrightHub can each expose their own REST API endpoints without modifying the core server code.

#### Acceptance Criteria

1. THE REST_API SHALL provide a route registration interface that accepts a route prefix string and a handler module, so that application modules can register their own endpoints at runtime before the server starts.
2. WHEN multiple application modules register routes with distinct prefixes, THE REST_API SHALL dispatch incoming HTTP requests to the correct module based on longest-prefix matching.
3. WHEN an application module registers a route prefix that conflicts with an existing prefix, THE REST_API SHALL reject the registration and return a descriptive error.
4. THE REST_API SHALL isolate route modules so that a failure in one module's handler does not crash the server or affect other modules.
5. WHEN a request arrives for a path that matches no registered route prefix, THE REST_API SHALL return HTTP 404 with a JSON error response.
6. THE REST_API SHALL pass a request context object to each route handler containing the parsed JWT claims, request method, path, query parameters, headers, and body.

### Requirement 16: Static File Serving

**User Story:** As a web application developer, I want the server to serve static HTML, JavaScript, CSS, and asset files from configurable directories, so that web frontends like BrightChat and BrightHub can be served directly by the node without a separate web server.

#### Acceptance Criteria

1. THE REST_API SHALL support registering static file directories mapped to URL path prefixes, so that requests to a prefix are served from the corresponding filesystem directory.
2. WHEN a static file request matches a registered prefix, THE REST_API SHALL serve the file with the correct Content-Type header based on file extension (html, js, css, json, png, jpg, svg, ico, woff, woff2).
3. WHEN a static file request targets a directory path, THE REST_API SHALL serve the index.html file from that directory if it exists.
4. WHEN a requested static file does not exist, THE REST_API SHALL return HTTP 404.
5. THE REST_API SHALL prevent path traversal attacks by rejecting any resolved path that escapes the configured static root directory.
6. THE REST_API SHALL support configuring Cache-Control headers per static file mount with a default of 3600 seconds for immutable assets and no-cache for HTML files.

### Requirement 17: Modular Middleware Pipeline

**User Story:** As a web application developer, I want a composable middleware pipeline for the REST API, so that cross-cutting concerns like authentication, CORS, rate limiting, and logging can be added or removed without modifying route handlers.

#### Acceptance Criteria

1. THE REST_API SHALL support registering middleware functions that execute in order before the route handler for each incoming request.
2. WHEN a middleware function calls next, THE REST_API SHALL pass control to the next middleware in the pipeline or to the route handler if no middleware remains.
3. WHEN a middleware function writes a response without calling next, THE REST_API SHALL short-circuit the pipeline and return the response immediately.
4. THE REST_API SHALL provide built-in middleware for JWT authentication that extracts and validates the bearer token and populates the request context with member claims.
5. THE REST_API SHALL provide built-in middleware for CORS that sets Access-Control-Allow-Origin, Access-Control-Allow-Methods, Access-Control-Allow-Headers, and Access-Control-Max-Age headers based on a configurable allowed origins list.
6. THE REST_API SHALL provide built-in middleware for rate limiting using a token bucket algorithm with configurable requests-per-second and burst size per client IP.
7. THE REST_API SHALL allow application modules to register their own middleware that applies only to their route prefix scope.

### Requirement 18: WebSocket Channel Namespacing

**User Story:** As a web application developer, I want WebSocket connections to support channel namespaces, so that BrightChat, BrightMail, and other applications can each have their own real-time event streams alongside the gossip protocol without interference.

#### Acceptance Criteria

1. THE WebSocket_Server SHALL support channel namespaces identified by a string prefix in the format `/ws/app/{namespace}`, so that application modules can register their own WebSocket channels.
2. WHEN a client connects to a namespaced WebSocket channel, THE WebSocket_Server SHALL authenticate the connection using the same JWT mechanism as `/ws/client` and route messages only within that namespace.
3. WHEN an application module sends a message on a namespace, THE WebSocket_Server SHALL deliver the message only to clients subscribed to that specific namespace.
4. THE WebSocket_Server SHALL isolate namespace channels so that messages on one namespace are never delivered to clients on a different namespace.
5. THE WebSocket_Server SHALL support namespace-scoped event subscriptions where clients can subscribe to specific event types within their namespace.
6. THE WebSocket_Server SHALL apply the same heartbeat ping/pong and JWT expiry handling to namespaced channels as to the base `/ws/client` channel.
7. THE gossip protocol messages on `/ws/node/:nodeId` SHALL remain unaffected by application namespace channels.

### Requirement 14: Wire Compatibility with TypeScript Implementation

**User Story:** As a network operator running mixed C++ and TypeScript nodes, I want the C++ gossip implementation to be wire-compatible with the TypeScript implementation, so that nodes of either type can communicate without translation.

#### Acceptance Criteria

1. THE Gossip_Engine SHALL serialize Block_Announcement JSON using identical field names, types, and nesting structure as the TypeScript BlockAnnouncement interface.
2. THE Gossip_Engine SHALL serialize GossipConfig JSON using identical field names and default values as the TypeScript GossipConfig interface.
3. THE WebSocket_Server SHALL use the same WebSocket endpoint paths (`/ws/node/:nodeId` and `/ws/client`) and the same message framing as the TypeScript implementation.
4. THE REST_API SHALL use the same endpoint paths, request formats, and response JSON structures as the TypeScript IntrospectionController.
5. THE ECIES_Authenticator SHALL use the same challenge/response protocol and key formats (secp256k1, 33-byte compressed public keys) as the TypeScript implementation.
6. FOR ALL Block_Announcement objects created by the C++ implementation, the TypeScript implementation SHALL accept and correctly process the announcement, and vice versa (interoperability round-trip property).
