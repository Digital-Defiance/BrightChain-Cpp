# Implementation Plan: BrightChain macOS Client

## Overview

This implementation plan breaks down the BrightChain macOS Client into discrete coding tasks. The approach prioritizes building foundational components first (SDK bridge, security, storage), then layering features (authentication, messaging, file sharing), then the Vault API layer (HTTP client, folders, ACLs, sharing, destruction, canary, quorum, audit, notifications, quota), and finally integrating the network-backed virtual drive via File Provider. Each task builds incrementally on previous work.

## Tasks

- [x] 1. Set up project structure and SDK bridge foundation
  - [x] 1.1 Create Xcode project structure with SwiftUI app and File Provider extension targets
    - Create main app target with SwiftUI lifecycle
    - Create File Provider extension target for virtual drive
    - Configure bridging header for Objective-C++ integration
    - _Requirements: 1.1, 9.1_

  - [x] 1.2 Implement BrightChainSDKWrapper Objective-C++ bridge
    - Create BrightChainSDKWrapper.h/.mm with member operations
    - Implement validateMnemonic, generateMnemonic methods
    - Implement loginWithMnemonic, createMemberWithMnemonic methods
    - Implement signData, verifySignature methods
    - _Requirements: 1.2, 1.4, 2.2, 2.4_

  - [x] 1.3 Write property tests for mnemonic validation
    - **Property 2: Mnemonic Validation Correctness**
    - **Validates: Requirements 2.2**

  - [x] 1.4 Write property tests for key derivation determinism
    - **Property 3: Key Derivation Determinism**
    - **Validates: Requirements 1.4, 2.4**

- [x] 2. Implement security layer
  - [x] 2.1 Enhance SecureEnclaveKeyring implementation
    - Implement getOrCreateEnclaveKey with secp256r1
    - Implement encrypt/decrypt using ECIES with enclave key
    - Implement deleteKey for account deletion
    - Implement hasKey for key existence check
    - _Requirements: 3.1, 3.2, 3.3, 3.6_

  - [x] 2.2 Implement KeychainService fallback
    - Create KeychainService for Secure Enclave fallback
    - Implement secure storage with appropriate access controls
    - Implement key retrieval with biometric/password authentication
    - _Requirements: 3.5_

  - [x] 2.3 Write property tests for Secure Enclave encryption round-trip
    - **Property 4: Secure Enclave Encryption Round-Trip**
    - **Validates: Requirements 1.5, 3.2**

- [x] 3. Checkpoint - Ensure SDK bridge and security tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Implement BlockStore service layer
  - [x] 4.1 Implement BlockStoreWrapper Objective-C++ bridge
    - Create BlockStoreWrapper.h/.mm wrapping DiskBlockStore
    - Implement storeBlock with checksum computation
    - Implement getBlock with integrity verification
    - Implement hasBlock, deleteBlock operations
    - Implement getMetadata, getStats operations
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.7_

  - [x] 4.2 Implement BlockStoreService Swift wrapper
    - Create BlockStoreService conforming to BlockStoreServiceProtocol
    - Implement store, retrieve, exists, delete operations
    - Implement getStorageStats with category breakdown
    - Implement cleanup with policy support
    - _Requirements: 8.1, 8.2, 8.5, 8.6_

  - [x] 4.3 Write property tests for BlockStore operations
    - **Property 30: BlockStore Directory Structure**
    - **Property 31: BlockStore Checksum Verification**
    - **Property 32: BlockStore Block Size Support**
    - **Property 33: BlockStore Metadata Creation**
    - **Property 34: BlockStore Statistics Accuracy**
    - **Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.7**

  - [x] 4.4 Write property tests for block integrity
    - **Property 23: Block Integrity Verification**
    - **Validates: Requirements 8.7**

- [x] 5. Implement CryptoWrapper and CryptoService
  - [x] 5.1 Implement CryptoWrapper Objective-C++ bridge
    - Create CryptoWrapper.h/.mm wrapping ECIES operations
    - Implement encryptData for single and multiple recipients
    - Implement decryptData with private key
    - Implement CBL creation and parsing operations
    - _Requirements: 5.1, 5.4, 6.3, 6.4_

  - [x] 5.2 Implement CryptoService Swift wrapper
    - Create CryptoService conforming to CryptoServiceProtocol
    - Implement encrypt, decrypt, sign, verify operations
    - _Requirements: 5.1, 5.4_

  - [x] 5.3 Write property tests for encryption round-trip
    - **Property 8: Message Encryption Round-Trip**
    - **Property 14: Block Encryption for Recipients**
    - **Validates: Requirements 5.1, 5.4, 6.3**

- [x] 6. Checkpoint - Ensure storage and crypto tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Implement authentication flow
  - [x] 7.1 Implement AuthManager
    - Create AuthManager conforming to AuthManagerProtocol
    - Implement register with mnemonic generation and key storage
    - Implement login with mnemonic validation and key verification
    - Implement logout with session clearing
    - _Requirements: 1.1-1.7, 2.1-2.7_

  - [x] 7.2 Implement MemberModel and SessionState data models
    - Create MemberModel with all required fields
    - Create SessionState for session management
    - Implement Codable conformance for persistence
    - _Requirements: 1.6, 2.5, 2.6_

  - [x] 7.3 Implement registration UI flow
    - Create RegistrationView with name/email form
    - Create MnemonicDisplayView for showing generated words
    - Create MnemonicConfirmationView for user verification
    - Wire views to AuthManager
    - _Requirements: 1.1, 1.2, 1.3, 1.7_

  - [x] 7.4 Implement login UI flow
    - Enhance LoginView with mnemonic input
    - Add validation feedback for invalid mnemonics
    - Implement navigation to main interface on success
    - _Requirements: 2.1, 2.3, 2.5_

  - [x] 7.5 Write property tests for mnemonic generation validity
    - **Property 1: Mnemonic Generation Validity**
    - **Validates: Requirements 1.2**

  - [x] 7.6 Write unit tests for authentication flows
    - Test registration success flow
    - Test login with valid/invalid mnemonic
    - Test logout clears session
    - _Requirements: 1.1-1.7, 2.1-2.7_

- [x] 8. Implement messaging foundation
  - [x] 8.1 Implement Conversation and Message data models
    - Create Conversation model with participants and metadata
    - Create Message model with content and status
    - Create MessageContent and AttachmentReference models
    - Implement Codable conformance
    - _Requirements: 4.1, 4.4, 5.5_

  - [x] 8.2 Implement MessageManager
    - Create MessageManager conforming to MessageManagerProtocol
    - Implement loadConversations from local storage
    - Implement createConversation with unique ID generation
    - Implement sendMessage with encryption and block storage
    - Implement loadMessages with decryption
    - Implement deleteConversation preserving blocks
    - _Requirements: 4.1-4.6, 5.1-5.7_

  - [x] 8.3 Write property tests for conversation management
    - **Property 5: Conversation ID Uniqueness**
    - **Property 6: Conversation Display Completeness**
    - **Property 7: Conversation Deletion Block Preservation**
    - **Validates: Requirements 4.3, 4.4, 4.6**

  - [x] 8.4 Write property tests for message operations
    - **Property 9: Message Storage Block Creation**
    - **Property 10: CBL Block Reference Integrity**
    - **Property 11: Message Display Completeness**
    - **Property 12: Attachment Block Separation**
    - **Validates: Requirements 5.2, 5.3, 5.5, 5.7**

- [x] 9. Implement messaging UI
  - [x] 9.1 Create ConversationListView
    - Display list of conversations with previews
    - Show most recent message and timestamp
    - Support creating new conversations
    - _Requirements: 4.1, 4.2, 4.4_

  - [x] 9.2 Create ConversationDetailView
    - Display message history with sender and timestamp
    - Implement message composition and sending
    - Handle decryption errors gracefully
    - _Requirements: 4.5, 5.5, 5.6_

  - [x] 9.3 Create RecipientSelectionView
    - Allow selection of recipients by Member ID or contact name
    - Support multiple recipient selection for group conversations
    - _Requirements: 4.2_

- [x] 10. Checkpoint - Ensure authentication and messaging tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 11. Implement file sharing
  - [x] 11.1 Implement FileReference and CBL data models
    - Create FileReference with type, checksum, metadata
    - Create CBLReference and SuperCBLReference models
    - Create MagnetURL parser and generator
    - _Requirements: 6.5, 7.1, 7.2_

  - [x] 11.2 Implement FileShareManager
    - Create FileShareManager conforming to FileShareManagerProtocol
    - Implement splitFileIntoBlocks with appropriate block sizes
    - Implement uploadFile with encryption and CBL creation
    - Implement downloadFile with block retrieval and reassembly
    - Implement parseReference for Magnet URLs and CBL files
    - Implement generateMagnetURL
    - _Requirements: 6.1-6.7, 7.1-7.7_

  - [x] 11.3 Write property tests for file operations
    - **Property 13: File Block Size Selection**
    - **Property 15: Reference Generation Parseability**
    - **Property 16: Magnet URL Parse Round-Trip**
    - **Property 17: CBL Parse Correctness**
    - **Property 18: File Split-Reassemble Round-Trip**
    - **Validates: Requirements 6.2, 6.5, 7.1, 7.2, 7.5**

- [x] 12. Implement file sharing UI
  - [x] 12.1 Create FileUploadView
    - Implement file picker integration
    - Display upload progress with cancellation
    - Show generated reference on completion
    - _Requirements: 6.1, 6.6, 6.7_

  - [x] 12.2 Create FileDownloadView
    - Implement Magnet URL and CBL file input
    - Display download progress and estimated time
    - Handle missing blocks gracefully
    - _Requirements: 7.1, 7.2, 7.6, 7.7_

  - [x] 12.3 Create FileListView
    - Display local files with metadata
    - Support sharing via Magnet URL or CBL export
    - _Requirements: 6.5_

- [x] 13. Implement storage management
  - [x] 13.1 Implement StorageManager
    - Create StorageManager conforming to StorageManagerProtocol
    - Implement getUsageByCategory
    - Implement setStorageLimit with enforcement
    - Implement performCleanup with orphan detection
    - Implement verifyIntegrity with checksum validation
    - _Requirements: 8.1-8.7_

  - [x] 13.2 Create StorageSettingsView
    - Display storage usage with category breakdown
    - Allow setting storage limits
    - Provide cleanup and integrity check actions
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [x] 13.3 Write property tests for storage management
    - **Property 19: Storage Categorization Consistency**
    - **Property 20: Storage Limit Enforcement**
    - **Property 21: Orphan Cleanup Safety**
    - **Property 22: CBL Reference Consistency After Deletion**
    - **Validates: Requirements 8.2, 8.3, 8.5, 8.6**

- [x] 14. Checkpoint - Ensure file sharing and storage tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 15. Implement Vault API client foundation
  - [x] 15.1 Implement VaultEndpoint and VaultAPIError types
    - Create VaultEndpoint enum/struct for all `/burnbag/*` routes
    - Create VaultAPIError enum with all HTTP error mappings (400, 401, 403, 404, 409, 410, 413, 422)
    - Implement hex ID validation helper (`^[0-9a-fA-F]{32}$`)
    - _Requirements: 16.3, 16.6_

  - [x] 15.2 Implement VaultAPIClient core HTTP client
    - Create VaultAPIClient conforming to VaultAPIClientProtocol
    - Implement JWT Bearer header attachment on every authenticated request
    - Implement JWT refresh on 401 with single retry
    - Implement HTTP error → VaultAPIError mapping with server message preservation
    - Implement exponential backoff retry (up to 3 times) for non-destructive requests
    - Implement JSON serialization/deserialization for request/response bodies
    - Implement configurable base URL and timeout from AppSettings
    - _Requirements: 16.1, 16.2, 16.3, 16.4, 16.5, 16.7_

  - [x] 15.3 Implement Vault API data models
    - Create all Vault API Codable models: VaultFileMetadata, FileVersion, FileSearchResult, FileSearchFilters
    - Create upload models: UploadSession, UploadChunkResult, UploadSessionStatus
    - Create folder models: VaultFolder, FolderContents, BreadcrumbItem, SortField, SortDirection, ItemType
    - Create ACL models: PermissionFlag, PermissionLevel, ACLEntry, ACLDocument, EffectivePermissions, PermissionSet, ACLTargetType
    - Create sharing models: ShareLinkMode, ShareLink, SharedFile, ShareAuditEntry
    - Create destruction models: DestructionProof, VerificationBundle, DestructionResult, BatchDestructionResult, ProofVerification, NonAccessProof
    - Create canary models: CanaryBinding, CreateCanaryBindingRequest, UpdateCanaryBindingRequest, RecipientList, Recipient, DryRunResult
    - Create quorum models: QuorumOperationType, QuorumRequest, QuorumApprovalResult, QuorumRejectionResult, QuorumExecutionResult
    - Create audit models: AuditEntry, AuditDetails, AuditFilters, ComplianceReportRequest, ComplianceReport and sub-models
    - Create notification model: VaultNotification
    - Create quota models: QuotaInfo, QuotaBreakdownItem
    - Create TCBL export models: TCBLExportRequest, TCBLExportResult, SkippedFile
    - _Requirements: 16.5, 17-26_

  - [x] 15.4 Implement MockVaultAPIClient for testing
    - Create MockVaultAPIClient conforming to VaultAPIClientProtocol
    - Support configurable responses per endpoint path
    - Support configurable error injection
    - Track request count and last request for assertions
    - _Requirements: 16 (testing infrastructure)_

  - [x] 15.5 Write property tests for Vault API client
    - **Property 38: JWT Header Attachment**
    - **Property 39: HTTP Error Code to Typed Error Mapping**
    - **Property 40: Vault API Model JSON Round-Trip**
    - **Property 41: Hex ID Format Validation**
    - **Validates: Requirements 16.1, 16.3, 16.5, 16.6**

  - [x] 15.6 Write unit tests for Vault API client
    - Test JWT attachment to requests
    - Test JWT refresh on 401
    - Test exponential backoff retry (1, 2, 3 failures)
    - Test configurable base URL and timeout
    - Test invalid hex ID rejection
    - _Requirements: 16.1-16.7_

- [x] 16. Checkpoint - Ensure Vault API client tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [x] 17. Implement Vault folder management
  - [x] 17.1 Implement VaultFolderManager
    - Create VaultFolderManager conforming to VaultFolderManagerProtocol
    - Implement getRootFolder via GET `/burnbag/folders/root`
    - Implement getFolderContents via GET `/burnbag/folders/:id` with sort params
    - Implement createFolder via POST `/burnbag/folders`
    - Implement getBreadcrumbPath via GET `/burnbag/folders/:id/path`
    - Implement moveItem via POST `/burnbag/folders/:id/move`
    - _Requirements: 17.1-17.7_

  - [x] 17.2 Create FolderBrowserView
    - Display folder contents with files and subfolders
    - Show breadcrumb navigation path
    - Support creating new folders
    - Support moving files/folders via drag or action menu
    - Display quorum-governed indicator on governed folders
    - Handle circular reference error (409) gracefully
    - _Requirements: 17.1-17.7_

  - [x] 17.3 Write unit tests for folder management
    - Test fetch root folder contents
    - Test navigate into subfolder with sorting
    - Test create folder
    - Test move file to different folder
    - Test handle circular reference error (409)
    - Test display breadcrumb path
    - _Requirements: 17.1-17.7_

- [x] 18. Implement Vault ACL management
  - [x] 18.1 Implement VaultACLManager
    - Create VaultACLManager conforming to VaultACLManagerProtocol
    - Implement getACL via GET `/burnbag/acl/:targetType/:targetId`
    - Implement setACL via PUT `/burnbag/acl/:targetType/:targetId`
    - Implement getEffectivePermissions via GET `/burnbag/acl/:targetType/:targetId/effective/:principalId`
    - Implement createPermissionSet via POST `/burnbag/acl/permission-sets`
    - Implement listPermissionSets via GET `/burnbag/acl/permission-sets`
    - _Requirements: 18.1-18.7_

  - [x] 18.2 Create ACLEditorView
    - Display current ACL entries for a file or folder
    - Allow assigning built-in levels (viewer, commenter, editor, owner) and custom Permission Sets
    - Show effective permissions for a principal with inheritance source
    - Inform user that folder ACLs cascade to descendants
    - Disable ACL editing when user lacks `admin` flag
    - _Requirements: 18.1-18.7_

  - [x] 18.3 Write property tests for ACL management
    - **Property 43: Permission-Gated UI Control State**
    - **Validates: Requirements 18.7, 19.8**

  - [x] 18.4 Write unit tests for ACL management
    - Test fetch ACL for file/folder
    - Test set ACL with built-in levels
    - Test create custom permission set
    - Test fetch effective permissions
    - Test disable ACL editing without admin flag
    - _Requirements: 18.1-18.7_

- [x] 19. Implement Vault file sharing (three-tier)
  - [x] 19.1 Update FileShareManager for Vault API upload/download
    - Implement uploadFile via Vault chunked upload (POST init, PUT chunks, POST finalize)
    - Implement resumeUpload via GET session status + resume from last chunk
    - Implement downloadFile via GET `/burnbag/files/:id`
    - Implement downloadVersion via GET `/burnbag/files/:id/versions/:versionId/download`
    - Implement searchFiles via GET `/burnbag/files/search`
    - Implement getFileMetadata via GET `/burnbag/files/:id/metadata`
    - Implement softDelete via DELETE `/burnbag/files/:id`
    - Implement restore via POST `/burnbag/files/:id/restore`
    - Implement getNonAccessProof via GET `/burnbag/files/:id/non-access-proof`
    - Handle 413 quota exceeded on upload
    - _Requirements: 6.1-6.8, 7.1-7.9_

  - [x] 19.2 Implement VaultShareManager
    - Create VaultShareManager conforming to VaultShareManagerProtocol
    - Implement shareInternal via POST `/burnbag/share/internal`
    - Implement createShareLink via POST `/burnbag/share/link` (server_proxied, ephemeral_key_pair, recipient_public_key modes)
    - Implement getSharedWithMe via GET `/burnbag/share/shared-with-me`
    - Implement revokeShareLink via DELETE `/burnbag/share/link/:id`
    - Implement getMagnetURL via GET `/burnbag/share/:fileId/magnet`
    - Implement getShareAudit via GET `/burnbag/share/:fileId/audit`
    - _Requirements: 19.1-19.8_

  - [x] 19.3 Create VaultFileDetailView
    - Display file metadata, version history, ACL ID, destruction schedule
    - Show non-access proof verification result
    - Provide share, delete, restore, destroy actions gated by permissions
    - _Requirements: 7.4, 7.8, 19.8_

  - [x] 19.4 Create ShareView
    - Support internal sharing with recipient selection and permission level
    - Support external share link creation with mode selection and security trade-off explanation
    - Display shared-with-me list
    - Show share audit trail with timestamps and IP addresses
    - Disable sharing controls when user lacks `share` flag
    - _Requirements: 19.1-19.8_

  - [x] 19.5 Update FileUploadView for Vault chunked upload
    - Display chunk-level progress during upload
    - Support cancellation and resume of interrupted uploads
    - Show file metadata and vault creation ledger entry hash on completion
    - Display quota exceeded message on 413 with link to quota settings
    - _Requirements: 6.1-6.8_

  - [x] 19.6 Write property tests for file upload
    - **Property 42: Upload Chunk Checksum Integrity**
    - **Validates: Requirements 6.3**

  - [x] 19.7 Write unit tests for Vault file sharing
    - Test upload small file via Vault chunked upload
    - Test upload large file with multiple chunks
    - Test resume interrupted upload from session status
    - Test download file from Vault API
    - Test download specific version
    - Test soft-delete and restore file
    - Test handle 413 quota exceeded on upload
    - Test share file internally
    - Test create external share link (all three modes)
    - Test revoke share link
    - Test fetch shared-with-me list
    - Test fetch magnet URL
    - Test fetch share audit trail
    - Test disable sharing without share flag
    - _Requirements: 6.1-6.8, 7.1-7.9, 19.1-19.8_

- [x] 20. Implement cryptographic destruction
  - [x] 20.1 Implement VaultDestructionManager
    - Create VaultDestructionManager conforming to VaultDestructionManagerProtocol
    - Implement destroyImmediately via POST `/burnbag/destroy/:fileId`
    - Implement scheduleDestruction via POST `/burnbag/destroy/:fileId/schedule`
    - Implement cancelScheduledDestruction via DELETE `/burnbag/destroy/:fileId/schedule`
    - Implement destroyBatch via POST `/burnbag/destroy/batch`
    - Implement verifyProof via POST `/burnbag/destroy/:fileId/verify`
    - _Requirements: 20.1-20.7_

  - [x] 20.2 Create DestructionView
    - Display destruction proof (merkle root, bloom witness, ledger entry hash) after immediate destruction
    - Show scheduled destruction date in file metadata views
    - Support batch destruction with per-file result display
    - Support destruction proof verification with validity and ledger confirmation display
    - Route quorum-governed files through Quorum workflow before destruction
    - _Requirements: 20.1-20.7_

  - [x] 20.3 Write property tests for destruction routing
    - **Property 44: Quorum Routing for Governed Targets**
    - **Validates: Requirements 20.7, 22.1**

  - [x] 20.4 Write unit tests for destruction
    - Test immediate destruction with proof display
    - Test schedule future destruction
    - Test cancel scheduled destruction
    - Test batch destruction with per-file results
    - Test verify destruction proof
    - Test route quorum-governed file through quorum
    - _Requirements: 20.1-20.7_

- [x] 21. Implement canary protocols
  - [x] 21.1 Implement CanaryManager
    - Create CanaryManager conforming to CanaryManagerProtocol
    - Implement listBindings via GET `/burnbag/canary/bindings`
    - Implement listRecipientLists via GET `/burnbag/canary/recipients`
    - Implement createBinding via POST `/burnbag/canary/bindings`
    - Implement createRecipientList via POST `/burnbag/canary/recipients`
    - Implement dryRun via POST `/burnbag/canary/bindings/:id/dry-run`
    - Implement updateBinding via PATCH `/burnbag/canary/bindings/:id`
    - Implement deleteBinding via DELETE `/burnbag/canary/bindings/:id`
    - Implement updateRecipientList via PATCH `/burnbag/canary/recipients/:id`
    - _Requirements: 21.1-21.8_

  - [x] 21.2 Create CanarySettingsView
    - Display list of canary bindings with condition, provider, and enabled status
    - Support creating new bindings with provider selection grouped by category
    - Display dry run simulation results (files affected, recipient count, actions)
    - Support updating and deleting bindings
    - Manage recipient lists (create, update with name, email, optional public key)
    - Show timeout duration and last signal timestamp for absence bindings
    - _Requirements: 21.1-21.8_

  - [x] 21.3 Write property tests for canary protocols
    - **Property 45: Canary Provider Category Completeness**
    - **Validates: Requirements 21.3**

  - [x] 21.4 Write unit tests for canary protocols
    - Test list canary bindings
    - Test create canary binding
    - Test dry run simulation
    - Test update and delete binding
    - Test create and update recipient list
    - Test display absence binding with timeout
    - _Requirements: 21.1-21.8_

- [x] 22. Implement quorum-governed operations
  - [x] 22.1 Implement QuorumManager
    - Create QuorumManager conforming to QuorumManagerProtocol
    - Implement submitRequest via POST `/quorum/request`
    - Implement approve via POST `/quorum/:requestId/approve` with ECDSA signature
    - Implement reject via POST `/quorum/:requestId/reject`
    - _Requirements: 22.1-22.6_

  - [x] 22.2 Create QuorumApprovalView
    - Display quorum request details (operation type, target, required/current approvals, expiration)
    - Support approve action with ECDSA signature from CryptoService
    - Support reject action with optional reason
    - Display execution result when approval threshold is reached
    - Handle expired request (410) gracefully
    - _Requirements: 22.1-22.6_

  - [x] 22.3 Write unit tests for quorum operations
    - Test submit quorum request
    - Test approve with ECDSA signature
    - Test reject with reason
    - Test display execution result on threshold reached
    - Test handle expired request (410)
    - _Requirements: 22.1-22.6_

- [x] 23. Implement audit log viewing
  - [x] 23.1 Implement AuditManager
    - Create AuditManager conforming to AuditManagerProtocol
    - Implement queryAuditLog via GET `/burnbag/audit` with filters
    - Implement exportAuditLog via GET `/burnbag/audit/export`
    - Implement generateComplianceReport via POST `/burnbag/audit/compliance-report`
    - _Requirements: 23.1-23.5_

  - [x] 23.2 Create AuditLogView
    - Display audit entries with actor ID, target ID, operation type, details, ledger entry hash, timestamp
    - Support filtering by actor, target, operation type, date range, pagination
    - Support exporting audit log to user-selected file
    - Display compliance report with summary, access patterns, destruction events, sharing activity, non-access proofs
    - _Requirements: 23.1-23.5_

  - [x] 23.3 Write property tests for audit display
    - **Property 46: Audit Entry Display Completeness**
    - **Validates: Requirements 23.2**

  - [x] 23.4 Write unit tests for audit operations
    - Test query audit log with filters
    - Test export audit log
    - Test generate compliance report
    - Test display all report sections
    - _Requirements: 23.1-23.5_

- [x] 24. Implement notifications and quota
  - [x] 24.1 Implement NotificationManager
    - Create NotificationManager conforming to NotificationManagerProtocol
    - Implement pollNotifications via GET `/burnbag/notifications`
    - Implement markAsRead via POST `/burnbag/notifications/read`
    - Track unread count as published property
    - _Requirements: 24.1-24.7_

  - [x] 24.2 Implement QuotaManager
    - Create QuotaManager conforming to QuotaManagerProtocol
    - Implement fetchQuota via GET `/burnbag/quota`
    - Track currentQuota as published property
    - _Requirements: 25.1-25.5_

  - [x] 24.3 Create NotificationCenterView
    - Display unread notifications with type-specific icons
    - Provide navigation action for quorum_request → quorum approval view
    - Provide navigation action for share_received → shared file view
    - Display canary alerts with elevated visual prominence
    - Show unread count badge on notification center icon
    - Support marking notifications as read
    - _Requirements: 24.1-24.7_

  - [x] 24.4 Create QuotaView
    - Display used bytes, quota bytes, and percentage used
    - Show usage breakdown by category (files, versions)
    - Display visual warning indicator when usage exceeds 80%
    - _Requirements: 25.1-25.5_

  - [x] 24.5 Write property tests for notifications
    - **Property 47: Notification Type Navigation Routing**
    - **Property 48: Unread Notification Count Accuracy**
    - **Validates: Requirements 24.3, 24.4, 24.7**

  - [x] 24.6 Write property tests for quota
    - **Property 49: Quota Usage Percentage Calculation**
    - **Property 50: Quota Warning Threshold**
    - **Validates: Requirements 25.1, 25.3**

  - [x] 24.7 Write unit tests for notifications and quota
    - Test poll notifications on app activation
    - Test display unread notifications
    - Test mark notifications as read
    - Test navigate from quorum_request notification
    - Test navigate from share_received notification
    - Test elevated display for canary alerts
    - Test fetch and display quota
    - Test display usage breakdown by category
    - Test show warning at 80% usage
    - Test handle 413 quota exceeded
    - Test refresh quota after upload/destruction
    - _Requirements: 24.1-24.7, 25.1-25.5_

- [x] 25. Implement TCBL folder export
  - [x] 25.1 Implement TCBL export in VaultFolderManager
    - Implement exportTCBL via POST `/burnbag/folders/:id/export-tcbl` with MIME type filters, max depth, exclusion patterns
    - _Requirements: 26.1-26.5_

  - [x] 25.2 Create TCBLExportView
    - Present export options (MIME type filters, max depth, exclusion patterns)
    - Save TCBL data to user-selected file location
    - Display total file count and size on completion
    - Display skipped files with reasons
    - Handle 422 (no exportable files) with informative message
    - _Requirements: 26.1-26.5_

  - [x] 25.3 Write unit tests for TCBL export
    - Test export folder with default options
    - Test export with MIME type filters and depth limit
    - Test display skipped files with reasons
    - Test handle 422 no exportable files
    - _Requirements: 26.1-26.5_

- [x] 26. Checkpoint - Ensure all Vault API layer tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [-] 27. Implement network-backed virtual drive
  - [x] 27.1 Implement VirtualDriveManager (Vault API-backed)
    - Create VirtualDriveManager conforming to VirtualDriveManagerProtocol
    - Implement mount/unmount via File Provider domain registration (macOS Finder + iOS Files)
    - Back file listing entirely from Vault folder hierarchy via VaultFolderManager
    - Implement on-demand file download via Vault API when File Provider requests content
    - Implement local cache for downloaded content with eviction policy
    - It is important that the cache respects the ledger for files with burnbag/canary protocols that require proof of non-access.
    - _Requirements: 9.1-9.7_

  - [x] 27.2 Implement BrightChainFileProviderExtension (network-backed)
    - Create File Provider extension with NSFileProviderReplicatedExtension (modern API)
    - Implement item enumeration from Vault folder hierarchy (root, subfolders, files)
    - Implement fetchContents to download file data via Vault API on access
    - Implement createItem for uploads from Finder/Files app to Vault
    - Implement modifyItem for renames and moves via VaultFolderManager
    - Implement deleteItem for soft-delete via Vault API
    - Cache decrypted content locally for performance
    - Handle Vault API errors (401, 403, 404) with appropriate File Provider error codes
    - _Requirements: 9.1-9.7, 15.3, 15.4_

  - [x] 27.3 Create VirtualDriveSettingsView
    - Display mount status and File Provider domain registration state
    - Provide mount/unmount controls
    - Show Vault API connectivity status
    - Show troubleshooting for mount failures
    - _Requirements: 9.1, 9.5, 9.6_

  - [x] 27.4 Write property tests for virtual drive
    - **Property 24: Virtual Drive File Listing Completeness** (from Vault folder hierarchy)
    - **Property 25: Virtual Drive File Operations** (read/stat via Vault API download)
    - **Validates: Requirements 9.2, 9.7**

  - [x] 27.5 Write unit tests for virtual drive
    - Test mount virtual drive (macOS File Provider)
    - Test mount virtual drive (iOS File Provider)
    - Test list files from Vault folder hierarchy
    - Test read file content via Vault API download
    - Test create file via File Provider → Vault upload
    - Test move/rename via File Provider → VaultFolderManager
    - Test delete via File Provider → Vault soft-delete
    - Test handle Vault API errors in File Provider context
    - _Requirements: 9.1-9.7_

- [-] 28. Implement network connectivity
  - [x] 28.1 Implement NetworkManager
    - Create NetworkManager conforming to NetworkManagerProtocol
    - Implement connect/disconnect with configured endpoints
    - Implement checkVaultAPIReachability
    - Implement requestBlock from peers
    - Implement announceBlock to peers
    - Implement syncPendingOperations on reconnect
    - Manage pending operations queue
    - _Requirements: 12.1-12.7_

  - [x] 28.2 Create NetworkStatusView
    - Display connection status and Vault API reachability
    - Show connected peers
    - Allow endpoint configuration
    - _Requirements: 12.2, 12.7_

  - [x] 28.3 Write property tests for network operations
    - **Property 35: Block Serving Policy Enforcement**
    - **Property 37: Failed Operation Queuing**
    - **Validates: Requirements 12.4, 14.4**

- [x] 29. Implement application settings
  - [x] 29.1 Implement AppSettings model and persistence
    - Create AppSettings with all configuration options including vaultAPIBaseURL and vaultAPITimeout
    - Implement persistence using UserDefaults or file storage
    - Implement default values
    - _Requirements: 13.1-13.7, 16.7_

  - [x] 29.2 Create SettingsView
    - Organize settings into logical categories
    - Implement storage, network, virtual drive, Vault API settings sections
    - Apply settings without restart when possible
    - _Requirements: 13.1, 13.2, 13.6_

  - [x] 29.3 Write property tests for settings persistence
    - **Property 36: Settings Persistence Round-Trip**
    - **Validates: Requirements 13.7**

- [x] 30. Implement error handling and logging
  - [x] 30.1 Implement BrightChainError and error handling infrastructure
    - Create BrightChainError enum with all error categories including vaultAPI(VaultAPIError)
    - Implement user-friendly messages and recovery suggestions
    - Implement error logging without exposing sensitive data (keys, JWTs redacted)
    - _Requirements: 14.1, 14.2, 14.3_

  - [x] 30.2 Create ErrorAlertView and error presentation
    - Display user-friendly error messages
    - Provide actionable recovery suggestions (retry, refreshJWT, checkNetwork, freeStorage, upgradeQuota, requestQuorumApproval)
    - Support diagnostic export for critical errors
    - _Requirements: 14.1, 14.2, 14.7_

  - [x] 30.3 Implement operation logging and diagnostics
    - Create logging service with configurable levels
    - Implement log viewer in settings
    - Implement diagnostic export
    - _Requirements: 14.6, 14.7_

- [x] 31. Implement main application shell
  - [x] 31.1 Create MainView with navigation
    - Implement sidebar navigation on macOS, tab bar on iOS
    - Include messaging, files/folders, sharing, storage, canary, audit, notifications, settings sections
    - Display network status and Vault API connectivity indicator
    - Display unread notification count badge
    - _Requirements: 4.1, 12.2, 15.5, 24.7_

  - [x] 31.2 Wire all components together
    - Initialize all managers and services at app launch (including all Vault API managers)
    - Configure dependency injection
    - Handle app lifecycle events (poll notifications on activation, refresh quota)
    - _Requirements: 12.1, 13.6, 24.1, 25.5_

- [x] 32. iOS and Mac Catalyst platform adaptation
  - [x] 32.1 Implement platform-adaptive layouts
    - Ensure sidebar navigation on macOS, tab bar on iOS
    - Adapt file picker and save dialogs per platform
    - Use Face ID on iPhone/iPad, Touch ID on Mac for biometric auth
    - _Requirements: 15.1-15.7_

  - [x] 32.2 Write smoke tests for platform targets
    - Test app compiles and launches on iOS 16+
    - Test app compiles and launches on macOS 13+
    - Test File Provider extension registers on iOS
    - Test File Provider extension registers on macOS
    - Test correct biometric API used per platform
    - Test tab bar navigation on iOS, sidebar on macOS
    - _Requirements: 15.1-15.7_

- [x] 33. Integration tests
  - [x] 33.1 Write end-to-end integration tests
    - Test authenticate → upload file → share internally → download
    - Test create folder → upload file to folder → navigate → download
    - Test upload → schedule destruction → verify proof
    - Test create canary binding → dry run → delete
    - Test quorum request → approve → execution
    - Test offline operation and sync on reconnect
    - _Requirements: all_

- [x] 34. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- All tasks are required for comprehensive testing from the start
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The File Provider extension requires separate entitlements and provisioning
- The virtual drive is entirely network-backed via the Vault API — there is no local-only content catalog or local CBL import for the virtual drive
- Tasks 15-26 cover the full Vault API layer which is the primary new work
- Tasks 29-31 were previously completed (renumbered from 18-20)


## Future Features (Not Yet Specified)

The following features are planned for future iterations and will require their own requirements/design work:

### Network Economy Layer
- **UPnP Port Forwarding**: Auto-expose ports to the internet for peer-to-peer connectivity with other BrightChain nodes
- **Joules Accounting System**: Users elect a percentage of their storage space and network capacity to contribute to the network, measured in "Joules" (similar to Ethereum Gas)
- **Resource Allocation Settings**: UI for users to configure how much storage/bandwidth they contribute
- **Accounting Ledger Integration**: Track Joules earned (from serving blocks/storage to others) and spent (for using others' resources)
- **Node Discovery & Quorum Participation**: Connect to and participate in the broader BrightChain network

*Note: The Joules economy and accounting ledger are documented in the main BrightChain specification (not available in this workspace).*
