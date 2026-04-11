# Requirements Document

## Introduction

This document specifies the requirements for the BrightChain Windows Client, a WinUI 3 desktop application targeting Windows 10 (1809+) and Windows 11. The application provides a full-featured interface to the BrightChain distributed storage and communication system. It integrates both a local C++ BrightChain SDK (via C++/CLI bridge) for cryptographic operations and block storage, and the DigitalBurnbag Vault HTTP API as the server-side backend for file operations, folder management, access control, sharing, destruction, canary protocols, quorum governance, audit logging, and notifications.

The client enables users to securely authenticate, exchange encrypted messages, share files (locally and via the Vault API), manage hierarchical folders with composable ACLs, schedule cryptographic destruction with blockchain proof, configure dead man's switch canary protocols, participate in quorum-governed operations, review audit logs, and access their distributed storage through a virtual drive interface (Windows Cloud Files API / CfApi for File Explorer integration).

## Glossary

- **BrightChain_Client**: The WinUI 3 (Windows App SDK) desktop application that provides the user interface
- **Member**: A BrightChain user with cryptographic identity (secp256k1 keys derived from BIP39 mnemonic)
- **Block**: A fixed-size unit of encrypted data storage (512B to 256MB)
- **CBL**: Constituent Block List — a structured block containing references to data blocks
- **SuperCBL**: A hierarchical CBL that references sub-CBLs for large files
- **BlockStore**: The local disk-based storage system for blocks
- **DPAPI_Keyring**: Windows Data Protection API (DPAPI) and Windows Hello for protecting private keys with hardware-backed security
- **CLI_Bridge**: The C++/CLI bridge layer between C# and the C++ BrightChain SDK
- **Virtual_Drive**: A Cloud Files API (CfApi) based virtual filesystem integrated with File Explorer
- **Magnet_URL**: A URI scheme for identifying content by cryptographic hash
- **ECIES**: Elliptic Curve Integrated Encryption Scheme for end-to-end encryption
- **Quorum_Node**: A network node participating in BrightChain consensus operations
- **Vault_API**: The DigitalBurnbag Vault HTTP API (`/burnbag/*`) providing server-side file storage, folder management, ACLs, sharing, destruction, canary, quorum, audit, and notifications
- **Vault**: A server-side encrypted file container — each file version is stored in an independent Vault with AES-256-GCM encryption, Merkle commitment tree, and blockchain ledger entry
- **Vault_Client**: The C# HttpClient layer in the app that communicates with the Vault API
- **Destruction_Proof**: A blockchain-recorded cryptographic proof that a file's encryption keys have been securely erased, making the data permanently unrecoverable
- **Non_Access_Proof**: A cryptographic proof (Bloom filter witness + Merkle proof) verifiable against the blockchain ledger, proving the server has not accessed a file's encrypted content
- **Canary_Binding**: A dead man's switch configuration that monitors user activity through a third-party provider and triggers protocol actions (destruction, distribution, disclosure) when conditions are met or not met
- **Canary_Provider**: A third-party service (GitHub, Fitbit, Slack, etc.) monitored by a canary binding for activity signals
- **Quorum_Request**: A multi-party approval request for sensitive operations on quorum-governed files or folders
- **Permission_Flag**: An atomic permission unit (read, write, delete, share, admin, preview, comment, download, manage_versions) composable into Permission Sets
- **Permission_Set**: A named collection of Permission Flags (built-in: viewer, commenter, editor, owner; or custom)
- **ACL**: Access Control List — a composable POSIX-style permission document applied to files or folders, with folder ACLs cascading to descendants unless overridden
- **Share_Link**: An external share link with three security modes: server_proxied, ephemeral_key_pair, or recipient_public_key
- **TCBL**: Turing Complete BrightChain Language — an export format for folder contents
- **Key_Wrapping**: The process of encrypting a file's symmetric AES-256-GCM key under each authorized recipient's ECIES public key
- **Upload_Session**: A server-side chunked upload session with resume support, quota checks, and session expiration
- **CfApi**: Windows Cloud Files API — the platform API for creating cloud file placeholders in File Explorer with on-demand hydration

## Requirements

### Requirement 1: User Registration

**User Story:** As a new user, I want to create a BrightChain account with a secure mnemonic backup, so that I can access the system and recover my account if needed.

#### Acceptance Criteria

1. WHEN a user initiates registration, THE BrightChain_Client SHALL display a registration form requesting name and email
2. WHEN the user submits valid registration details, THE BrightChain_Client SHALL generate a new BIP39 12-word mnemonic
3. WHEN a mnemonic is generated, THE BrightChain_Client SHALL display the mnemonic words clearly and require user confirmation
4. WHEN the user confirms the mnemonic, THE BrightChain_Client SHALL derive secp256k1 keys using BIP44 path m/44'/0'/0'/0/0
5. WHEN keys are derived, THE BrightChain_Client SHALL encrypt the private key using DPAPI_Keyring and store it locally
6. WHEN registration completes, THE BrightChain_Client SHALL create a Member record and establish an authenticated session
7. IF the user cancels registration, THEN THE BrightChain_Client SHALL discard any generated keys and return to the welcome screen

### Requirement 2: User Authentication

**User Story:** As a returning user, I want to log in using my mnemonic phrase, so that I can access my BrightChain account and data.

#### Acceptance Criteria

1. WHEN a user initiates login, THE BrightChain_Client SHALL display a login form requesting name, email, and mnemonic
2. WHEN a mnemonic is entered, THE BrightChain_Client SHALL validate it against BIP39 word list and checksum
3. IF the mnemonic is invalid, THEN THE BrightChain_Client SHALL display an error message and prevent login
4. WHEN a valid mnemonic is submitted, THE BrightChain_Client SHALL derive keys and verify against stored encrypted key
5. WHEN authentication succeeds, THE BrightChain_Client SHALL establish a session, obtain a JWT from the Vault_API, and navigate to the main interface
6. WHEN a session is established, THE BrightChain_Client SHALL load the user's Member profile and preferences
7. WHEN the user logs out, THE BrightChain_Client SHALL clear the session, invalidate the JWT, and return to the login screen

### Requirement 3: Secure Key Management

**User Story:** As a user, I want my private keys protected by hardware-backed security, so that my cryptographic identity remains secure even if my device is compromised.

#### Acceptance Criteria

1. THE DPAPI_Keyring SHALL use Windows Hello (TPM 2.0) or DPAPI to generate a protection key for encrypting user private keys
2. WHEN a Member private key needs storage, THE DPAPI_Keyring SHALL encrypt it using the Windows Hello credential guard or DPAPI with user-scope protection
3. WHEN a cryptographic operation requires the private key, THE DPAPI_Keyring SHALL decrypt it on-demand with Windows Hello biometric, PIN, or password authentication
4. THE BrightChain_Client SHALL never store unencrypted private keys in memory longer than necessary for the operation
5. IF Windows Hello is unavailable, THEN THE BrightChain_Client SHALL fall back to DPAPI with user-scope protection and appropriate access controls
6. WHEN the user deletes their account, THE BrightChain_Client SHALL securely erase all stored key material

### Requirement 4: Secure Messaging — Conversation Management

**User Story:** As a user, I want to manage encrypted conversations with other BrightChain members, so that I can communicate privately.

#### Acceptance Criteria

1. WHEN a user views the messaging interface, THE BrightChain_Client SHALL display a list of existing conversations
2. WHEN a user initiates a new conversation, THE BrightChain_Client SHALL allow selection of recipients by Member ID or contact name
3. WHEN a conversation is created, THE BrightChain_Client SHALL generate a unique conversation identifier
4. WHEN displaying conversations, THE BrightChain_Client SHALL show the most recent message preview and timestamp
5. WHEN a user selects a conversation, THE BrightChain_Client SHALL load and display the message history
6. WHEN a user deletes a conversation, THE BrightChain_Client SHALL remove local message data but preserve blocks in BlockStore

### Requirement 5: Secure Messaging — Message Exchange

**User Story:** As a user, I want to send and receive end-to-end encrypted messages, so that only intended recipients can read my communications.

#### Acceptance Criteria

1. WHEN a user composes a message, THE BrightChain_Client SHALL encrypt it using ECIES with recipient public keys
2. WHEN a message is sent, THE BrightChain_Client SHALL store it as blocks in the local BlockStore
3. WHEN a message is sent, THE BrightChain_Client SHALL create a CBL referencing the message blocks
4. WHEN a message is received, THE BrightChain_Client SHALL decrypt it using the recipient's private key
5. WHEN displaying messages, THE BrightChain_Client SHALL show sender identity, timestamp, and decrypted content
6. IF decryption fails, THEN THE BrightChain_Client SHALL display an error indicator for the affected message
7. WHEN a message includes attachments, THE BrightChain_Client SHALL handle them as separate encrypted blocks

### Requirement 6: File Sharing — Upload

**User Story:** As a user, I want to upload files to the Vault and share them with other BrightChain members, so that I can securely distribute content.

#### Acceptance Criteria

1. WHEN a user initiates file upload, THE BrightChain_Client SHALL allow selection of files from the local filesystem
2. WHEN a file is selected, THE Vault_Client SHALL initialize a chunked upload session via POST `/burnbag/upload/init` with file name, MIME type, total size, and optional target folder ID
3. WHEN an upload session is initialized, THE Vault_Client SHALL upload chunks sequentially via PUT `/burnbag/upload/:sessionId/chunk/:index` with integrity checksums
4. WHEN all chunks are uploaded, THE Vault_Client SHALL finalize the upload via POST `/burnbag/upload/:sessionId/finalize`
5. WHEN upload completes, THE BrightChain_Client SHALL display the file metadata returned by the server including the file ID and vault creation ledger entry hash
6. WHEN uploading, THE BrightChain_Client SHALL display chunk-level progress and allow cancellation
7. IF upload fails or connection drops, THEN THE Vault_Client SHALL query session status via GET `/burnbag/upload/:sessionId/status` and resume from the last received chunk
8. IF the server returns 413 (quota exceeded), THEN THE BrightChain_Client SHALL display a quota exceeded message and direct the user to storage quota settings

### Requirement 7: File Sharing — Download and Management

**User Story:** As a user, I want to download and manage files from the Vault, so that I can access content shared with me and manage my own files.

#### Acceptance Criteria

1. WHEN a user requests a file download, THE Vault_Client SHALL retrieve the file via GET `/burnbag/files/:id` as a binary stream
2. WHEN a user requests a specific version, THE Vault_Client SHALL download it via GET `/burnbag/files/:id/versions/:versionId/download`
3. WHEN downloading, THE BrightChain_Client SHALL display progress and estimated completion time
4. WHEN a user views file details, THE Vault_Client SHALL retrieve metadata via GET `/burnbag/files/:id/metadata` including version history, ACL ID, and destruction schedule
5. WHEN a user searches for files, THE Vault_Client SHALL query GET `/burnbag/files/search` with the user's filters (query, tags, MIME type, folder, date range, size range)
6. WHEN a user soft-deletes a file, THE Vault_Client SHALL send DELETE `/burnbag/files/:id` and update the local file listing
7. WHEN a user restores a soft-deleted file, THE Vault_Client SHALL send POST `/burnbag/files/:id/restore`
8. WHEN a user requests a non-access proof, THE Vault_Client SHALL retrieve it via GET `/burnbag/files/:id/non-access-proof` and display the verification result
9. IF blocks are unavailable or download fails, THEN THE BrightChain_Client SHALL report the error with actionable recovery suggestions

### Requirement 8: Storage Management

**User Story:** As a user, I want to manage both local block storage and server-side storage quota, so that I can control disk usage and data retention.

#### Acceptance Criteria

1. WHEN a user views storage settings, THE BrightChain_Client SHALL display local storage used and available, and server-side quota from GET `/burnbag/quota`
2. WHEN viewing storage, THE BrightChain_Client SHALL categorize local usage by content type (messages, files, system) and server usage by category (files, versions)
3. WHEN a user sets local storage limits, THE BrightChain_Client SHALL enforce them during new block creation
4. WHEN local storage limits are approached, THE BrightChain_Client SHALL notify the user and suggest cleanup
5. WHEN a user initiates cleanup, THE BrightChain_Client SHALL identify and offer to remove orphaned blocks
6. WHEN blocks are deleted, THE BrightChain_Client SHALL update all affected CBL references
7. THE BrightChain_Client SHALL maintain block integrity by verifying checksums periodically

### Requirement 9: Virtual Drive — Mount and Access

**User Story:** As a user, I want to access my BrightChain and Vault files through a virtual drive in File Explorer, so that I can use them with any Windows application.

#### Acceptance Criteria

1. WHEN the user enables virtual drive, THE Virtual_Drive SHALL register a CfApi sync root with File Explorer
2. WHEN registered, THE Virtual_Drive SHALL display files from the Vault_API folder hierarchy (via GET `/burnbag/folders/:id`) and local CBL references as cloud file placeholders
3. WHEN a file is opened, THE Virtual_Drive SHALL hydrate the placeholder by downloading and decrypting content on-demand via the Vault_API or local BlockStore
4. WHEN a file is read, THE Virtual_Drive SHALL cache decrypted content locally for performance
5. WHEN the user disables virtual drive, THE Virtual_Drive SHALL unregister the CfApi sync root and release resources
6. IF registration fails, THEN THE BrightChain_Client SHALL display the error and offer troubleshooting steps
7. THE Virtual_Drive SHALL support standard file operations (read, list, stat) for accessible content

### Requirement 10: Virtual Drive — Content Discovery

**User Story:** As a user, I want to add content to my virtual drive using various reference formats, so that I can easily access shared files.

#### Acceptance Criteria

1. WHEN a user imports a Magnet URL, THE BrightChain_Client SHALL add the referenced content to the virtual drive
2. WHEN a user imports a CBL file, THE BrightChain_Client SHALL parse it and add content to the virtual drive
3. WHEN a user imports a SuperCBL, THE BrightChain_Client SHALL resolve the hierarchy and add all content
4. WHEN content is added, THE Virtual_Drive SHALL display it with original filename and metadata
5. WHEN content blocks are not locally available, THE Virtual_Drive SHALL show the file as a dehydrated placeholder
6. WHEN unavailable content is accessed, THE BrightChain_Client SHALL offer to fetch missing blocks
7. THE BrightChain_Client SHALL maintain a catalog of all imported content references

### Requirement 11: BlockStore Service

**User Story:** As a user, I want reliable local block storage, so that my data persists and remains accessible.

#### Acceptance Criteria

1. THE BlockStore SHALL store blocks in a hierarchical directory structure by block size and checksum
2. WHEN a block is stored, THE BlockStore SHALL compute and verify its SHA3-512 checksum
3. WHEN a block is requested, THE BlockStore SHALL retrieve it by checksum and verify integrity
4. THE BlockStore SHALL support multiple block sizes (512B, 1KB, 4KB, 1MB, 64MB, 256MB)
5. WHEN storing blocks, THE BlockStore SHALL create associated metadata files
6. IF a block fails integrity check, THEN THE BlockStore SHALL report corruption and attempt recovery
7. THE BlockStore SHALL provide statistics on stored blocks by size and type

### Requirement 12: Network Connectivity

**User Story:** As a user, I want to connect to BrightChain network nodes and the Vault API, so that I can exchange blocks and access server-side features.

#### Acceptance Criteria

1. WHEN the application starts, THE BrightChain_Client SHALL attempt to connect to configured network nodes and verify Vault_API reachability
2. WHEN connected, THE BrightChain_Client SHALL display network status and Vault_API connectivity in the interface
3. WHEN blocks are needed, THE BrightChain_Client SHALL request them from connected peers or the Vault_API
4. WHEN local blocks are requested by peers, THE BrightChain_Client SHALL serve them according to sharing policy
5. IF network connection fails, THEN THE BrightChain_Client SHALL operate in offline mode with local data
6. WHEN network is restored, THE BrightChain_Client SHALL sync pending operations with peers and the Vault_API
7. THE BrightChain_Client SHALL allow configuration of network endpoints and Vault_API base URL

### Requirement 13: Application Settings

**User Story:** As a user, I want to configure application behavior, so that I can customize my experience.

#### Acceptance Criteria

1. THE BrightChain_Client SHALL provide a settings interface accessible from the navigation pane
2. WHEN viewing settings, THE BrightChain_Client SHALL organize options into logical categories
3. THE BrightChain_Client SHALL allow configuration of storage paths and limits
4. THE BrightChain_Client SHALL allow configuration of network endpoints and Vault_API base URL
5. THE BrightChain_Client SHALL allow configuration of virtual drive sync root path for CfApi integration
6. WHEN settings are changed, THE BrightChain_Client SHALL apply them without requiring restart when possible
7. THE BrightChain_Client SHALL persist settings across application sessions

### Requirement 14: Error Handling and Recovery

**User Story:** As a user, I want clear error messages and recovery options, so that I can resolve issues without data loss.

#### Acceptance Criteria

1. WHEN an error occurs, THE BrightChain_Client SHALL display a user-friendly error message
2. WHEN displaying errors, THE BrightChain_Client SHALL provide actionable recovery suggestions
3. IF a cryptographic operation fails, THEN THE BrightChain_Client SHALL log details for debugging without exposing sensitive data
4. WHEN network operations fail, THE BrightChain_Client SHALL queue them for retry when connectivity returns
5. IF data corruption is detected, THEN THE BrightChain_Client SHALL isolate affected data and notify the user
6. THE BrightChain_Client SHALL maintain operation logs accessible through the settings interface
7. WHEN critical errors occur, THE BrightChain_Client SHALL offer to export diagnostic information

### Requirement 15: Windows Platform Integration

**User Story:** As a user, I want the BrightChain client to integrate with Windows platform conventions, so that the application feels native and consistent with other Windows applications.

#### Acceptance Criteria

1. THE BrightChain_Client SHALL compile and run on Windows 10 version 1809 and later, and Windows 11, using WinUI 3 (Windows App SDK)
2. THE BrightChain_Client SHALL use the WinUI 3 NavigationView with a left navigation pane for primary navigation
3. THE BrightChain_Client SHALL support Windows light and dark themes, following the system theme by default
4. THE BrightChain_Client SHALL support Windows system accent colors for visual consistency
5. WHEN biometric authentication is required, THE BrightChain_Client SHALL use Windows Hello (facial recognition, fingerprint, or PIN)
6. THE BrightChain_Client SHALL support standard Windows accessibility features including high contrast mode, screen reader compatibility, and keyboard navigation
7. THE BrightChain_Client SHALL use the C++/CLI bridge layer to expose C++ BrightChain SDK functionality to the C# application layer

### Requirement 16: Vault API Client Layer

**User Story:** As a developer, I want a well-structured HTTP client for the Vault API, so that all Vault operations are handled consistently with proper authentication, error mapping, and retry logic.

#### Acceptance Criteria

1. THE Vault_Client SHALL attach a valid JWT `Authorization: Bearer <token>` header to every authenticated Vault_API request
2. WHEN the JWT expires or a 401 response is received, THE Vault_Client SHALL attempt token refresh before retrying the request
3. THE Vault_Client SHALL map Vault_API HTTP error responses (400, 401, 403, 404, 409, 410, 413, 422) to typed C# exceptions with the server's `message` field preserved
4. WHEN a network error occurs on a non-destructive request, THE Vault_Client SHALL retry with exponential backoff up to 3 times before surfacing the error
5. THE Vault_Client SHALL serialize all request bodies as JSON and deserialize all response bodies from JSON using System.Text.Json
6. THE Vault_Client SHALL validate that all ID fields in requests are 32-character hex strings before sending
7. THE Vault_Client SHALL support configurable base URL and timeout values from application settings

### Requirement 17: Server-Side Folder Management

**User Story:** As a user, I want to organize my Vault files in hierarchical folders, so that I can keep my content structured and easy to navigate.

#### Acceptance Criteria

1. WHEN a user views their files, THE BrightChain_Client SHALL display the root folder contents via GET `/burnbag/folders/root`
2. WHEN a user navigates into a subfolder, THE Vault_Client SHALL fetch folder contents via GET `/burnbag/folders/:id` with optional sort field and direction
3. WHEN a user creates a folder, THE Vault_Client SHALL send POST `/burnbag/folders` with name and parent folder ID
4. WHEN displaying a folder, THE BrightChain_Client SHALL show the breadcrumb path via GET `/burnbag/folders/:id/path`
5. WHEN a user moves a file or folder, THE Vault_Client SHALL send POST `/burnbag/folders/:id/move` with item type and new parent ID
6. IF a circular folder reference would result from a move, THEN THE BrightChain_Client SHALL display an error and prevent the operation
7. WHEN a folder has `quorumGoverned: true`, THE BrightChain_Client SHALL indicate this visually and route sensitive operations through the Quorum workflow

### Requirement 18: Composable ACL Management

**User Story:** As a user, I want to control who can access my files and folders with fine-grained permissions, so that I can share content securely with appropriate access levels.

#### Acceptance Criteria

1. WHEN a user views permissions for a file or folder, THE Vault_Client SHALL retrieve the ACL via GET `/burnbag/acl/:targetType/:targetId`
2. WHEN a user sets permissions, THE Vault_Client SHALL replace the ACL via PUT `/burnbag/acl/:targetType/:targetId` with the full entries array
3. WHEN assigning permissions, THE BrightChain_Client SHALL offer built-in levels (viewer, commenter, editor, owner) and any custom Permission Sets
4. WHEN a user creates a custom Permission Set, THE Vault_Client SHALL send POST `/burnbag/acl/permission-sets` with name and selected flags
5. WHEN displaying effective permissions for a principal, THE Vault_Client SHALL query GET `/burnbag/acl/:targetType/:targetId/effective/:principalId` and show the resolved flags, level, and inheritance source
6. WHEN a folder ACL is set, THE BrightChain_Client SHALL inform the user that the ACL cascades to all descendants unless overridden
7. IF the user lacks the `admin` flag on a target, THEN THE BrightChain_Client SHALL disable ACL editing for that target

### Requirement 19: Three-Tier File Sharing

**User Story:** As a user, I want to share files internally with platform members and externally via links or magnet URLs, so that I can distribute content with appropriate security for each recipient.

#### Acceptance Criteria

1. WHEN a user shares a file internally, THE Vault_Client SHALL send POST `/burnbag/share/internal` with file ID, recipient ID, and permission level
2. WHEN a user creates an external share link, THE Vault_Client SHALL send POST `/burnbag/share/link` with file ID, security mode (server_proxied, ephemeral_key_pair, or recipient_public_key), optional password, optional expiration, and optional max access count
3. WHEN displaying share link options, THE BrightChain_Client SHALL explain the security trade-offs of each mode to the user
4. WHEN a user views files shared with them, THE Vault_Client SHALL retrieve the list via GET `/burnbag/share/shared-with-me`
5. WHEN a user revokes a share link, THE Vault_Client SHALL send DELETE `/burnbag/share/link/:id`
6. WHEN a user requests a magnet URL for a file, THE Vault_Client SHALL retrieve it via GET `/burnbag/share/:fileId/magnet`
7. WHEN a user views sharing activity for a file, THE Vault_Client SHALL retrieve the audit trail via GET `/burnbag/share/:fileId/audit` and display access events with timestamps and IP addresses
8. IF the user lacks the `share` flag on a file, THEN THE BrightChain_Client SHALL disable sharing controls for that file

### Requirement 20: Cryptographic Destruction

**User Story:** As a user, I want to permanently and verifiably destroy files with blockchain proof, so that I can ensure sensitive data is irrecoverable.

#### Acceptance Criteria

1. WHEN a user initiates immediate destruction of a file, THE Vault_Client SHALL send POST `/burnbag/destroy/:fileId` and display the returned Destruction_Proof (merkle root, bloom witness, ledger entry hash)
2. WHEN a user schedules future destruction, THE Vault_Client SHALL send POST `/burnbag/destroy/:fileId/schedule` with the scheduled timestamp
3. WHEN a user cancels a scheduled destruction, THE Vault_Client SHALL send DELETE `/burnbag/destroy/:fileId/schedule`
4. WHEN a user initiates batch destruction, THE Vault_Client SHALL send POST `/burnbag/destroy/batch` with the list of file IDs and display per-file results
5. WHEN a user verifies a destruction proof, THE Vault_Client SHALL send POST `/burnbag/destroy/:fileId/verify` with the proof and bundle, and display whether the proof is valid and ledger-confirmed
6. WHEN a file has `scheduledDestructionAt` set, THE BrightChain_Client SHALL display the scheduled destruction date prominently in file metadata views
7. IF a file is quorum-governed, THEN THE BrightChain_Client SHALL route the destruction request through the Quorum workflow before calling the destruction endpoint

### Requirement 21: Canary Protocols (Dead Man's Switch)

**User Story:** As a user, I want to configure automated fail-safe protocols that trigger based on my activity, so that my data is protected or distributed according to my wishes if I become unavailable.

#### Acceptance Criteria

1. WHEN a user views canary settings, THE Vault_Client SHALL retrieve all bindings via GET `/burnbag/canary/bindings` and all recipient lists via GET `/burnbag/canary/recipients`
2. WHEN a user creates a canary binding, THE Vault_Client SHALL send POST `/burnbag/canary/bindings` with protocol action, canary condition (presence, duress, or absense), provider, target file/folder IDs, recipient list ID, and timeout
3. WHEN displaying provider options, THE BrightChain_Client SHALL list all supported Canary_Providers grouped by category (Health, Social, Developer, Communication, Productivity, Financial, IoT, Gaming, Special)
4. WHEN a user performs a dry run, THE Vault_Client SHALL send POST `/burnbag/canary/bindings/:id/dry-run` and display the simulated impact (files affected, recipient count, actions description)
5. WHEN a user updates a binding, THE Vault_Client SHALL send PATCH `/burnbag/canary/bindings/:id` with the changed fields
6. WHEN a user deletes a binding, THE Vault_Client SHALL send DELETE `/burnbag/canary/bindings/:id`
7. WHEN a user manages recipient lists, THE Vault_Client SHALL support creating (POST `/burnbag/canary/recipients`) and updating (PATCH `/burnbag/canary/recipients/:id`) lists with name, email, and optional ECIES public key per recipient
8. WHEN displaying a canary binding with `canaryCondition: absense`, THE BrightChain_Client SHALL show the timeout duration and last signal timestamp prominently

### Requirement 22: Quorum-Governed Operations

**User Story:** As a user, I want sensitive operations on governed files to require multi-party approval, so that critical actions cannot be taken unilaterally.

#### Acceptance Criteria

1. WHEN a sensitive operation (destruction, external share, bulk delete, ACL change) is requested on a quorum-governed file or folder, THE BrightChain_Client SHALL submit a Quorum_Request via POST `/quorum/request` with operation type, target ID, target type, and reason
2. WHEN a Quorum_Request is submitted, THE BrightChain_Client SHALL display the request ID, required approvals, current approvals, and expiration date
3. WHEN a user receives a quorum approval notification, THE BrightChain_Client SHALL present the request details and allow the user to approve (POST `/quorum/:requestId/approve` with ECDSA signature) or reject (POST `/quorum/:requestId/reject` with optional reason)
4. WHEN a quorum request reaches the required approval threshold, THE BrightChain_Client SHALL display the execution result returned by the server
5. WHEN a quorum request is rejected, THE BrightChain_Client SHALL notify the requester with the rejection reason
6. IF a quorum request expires, THEN THE BrightChain_Client SHALL notify all participants and mark the request as expired

### Requirement 23: Audit Log Viewing

**User Story:** As a user, I want to view and export the blockchain-backed audit log for my files, so that I can verify access history and generate compliance reports.

#### Acceptance Criteria

1. WHEN a user views the audit log, THE Vault_Client SHALL query GET `/burnbag/audit` with optional filters (actor ID, target ID, operation type, date range, page, page size)
2. WHEN displaying audit entries, THE BrightChain_Client SHALL show actor ID, target ID, operation type, details (IP address, user agent), ledger entry hash, and timestamp
3. WHEN a user exports the audit log, THE Vault_Client SHALL query GET `/burnbag/audit/export` with the same filters and save the result to a user-selected file
4. WHEN a user generates a compliance report, THE Vault_Client SHALL send POST `/burnbag/audit/compliance-report` with date range and selected sections (access patterns, destruction events, sharing activity, non-access proofs) and display the aggregated results
5. WHEN displaying a compliance report, THE BrightChain_Client SHALL show summary statistics, access patterns, destruction event counts with proof validity, sharing activity counts, and non-access proof validity

### Requirement 24: In-App Notifications

**User Story:** As a user, I want to receive in-app notifications for file operations, sharing events, canary alerts, and quorum requests, so that I can respond to important events promptly.

#### Acceptance Criteria

1. WHEN the application becomes active, THE Vault_Client SHALL poll GET `/burnbag/notifications` to retrieve queued notifications
2. WHEN notifications are retrieved, THE BrightChain_Client SHALL display unread notifications in a notification center accessible from the main interface
3. WHEN a notification of type `quorum_request` is received, THE BrightChain_Client SHALL provide a direct action to navigate to the quorum approval view
4. WHEN a notification of type `share_received` is received, THE BrightChain_Client SHALL provide a direct action to navigate to the shared file
5. WHEN a user reads notifications, THE Vault_Client SHALL send POST `/burnbag/notifications/read` with the notification IDs
6. WHEN a canary alert notification is received, THE BrightChain_Client SHALL display it with elevated visual prominence
7. THE BrightChain_Client SHALL display an unread notification count badge on the notification center icon

### Requirement 25: Server-Side Storage Quota Display

**User Story:** As a user, I want to see my server-side storage quota and usage breakdown, so that I can manage my Vault storage effectively.

#### Acceptance Criteria

1. WHEN a user views storage settings, THE Vault_Client SHALL retrieve quota data via GET `/burnbag/quota` and display used bytes, quota bytes, and percentage used
2. WHEN displaying quota, THE BrightChain_Client SHALL show a usage breakdown by category (files, versions) as returned by the server
3. WHEN quota usage exceeds 80% of the limit, THE BrightChain_Client SHALL display a visual warning indicator
4. WHEN quota is exceeded (server returns 413 on upload), THE BrightChain_Client SHALL display a clear quota exceeded message and link to the quota view
5. THE BrightChain_Client SHALL refresh quota data after every completed upload and file destruction

### Requirement 26: TCBL Folder Export

**User Story:** As a user, I want to export folder contents in TCBL format, so that I can archive or transfer my Vault data in an interoperable format.

#### Acceptance Criteria

1. WHEN a user initiates a folder export, THE BrightChain_Client SHALL present options for MIME type filters, maximum depth, and exclusion patterns
2. WHEN the user confirms export, THE Vault_Client SHALL send POST `/burnbag/folders/:id/export-tcbl` with the selected filters
3. WHEN the export completes, THE BrightChain_Client SHALL save the TCBL data to a user-selected file location and display the total file count and size
4. WHEN some files are skipped during export (e.g., quorum approval required), THE BrightChain_Client SHALL display the list of skipped files with reasons
5. IF the folder contains no exportable files, THEN THE BrightChain_Client SHALL display an informative message explaining why the export cannot proceed
