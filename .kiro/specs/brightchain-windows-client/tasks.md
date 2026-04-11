# Implementation Plan: BrightChain Windows Client

## Overview

This implementation plan breaks down the BrightChain Windows Client into discrete coding tasks. The approach prioritizes building foundational components first (C++/CLI SDK bridge, DPAPI security, block storage), then layering features (authentication, messaging, file sharing), then the Vault API layer (HTTP client, folders, ACLs, sharing, destruction, canary, quorum, audit, notifications, quota), and finally integrating the virtual drive via Windows Cloud Files API (CfApi). Each task builds incrementally on previous work. The application targets WinUI 3 (Windows App SDK) with CommunityToolkit.Mvvm for MVVM, System.Text.Json for serialization, and HttpClient for Vault API communication.

## Tasks

- [ ] 1. Set up project structure and C++/CLI SDK bridge foundation
  - [ ] 1.1 Create WinUI 3 solution structure with C++/CLI bridge project
    - Create WinUI 3 (Windows App SDK) C# application project with MSIX packaging
    - Create C++/CLI class library project for BrightChain SDK bridge (compiled with /clr)
    - Configure project references: C# app → C++/CLI bridge → native C++ SDK
    - Set up CommunityToolkit.Mvvm NuGet package
    - Set up xUnit + FsCheck NuGet packages for property-based testing
    - Configure target platforms: Windows 10 (1809+) and Windows 11
    - _Requirements: 15.1, 15.7_

  - [ ] 1.2 Implement BrightChainSdkBridge C++/CLI wrapper
    - Create BrightChainSdkBridge managed ref class with /clr compilation
    - Implement ValidateMnemonic wrapping Member::validateMnemonic()
    - Implement GenerateMnemonic wrapping Member::generateMnemonic()
    - Implement Login wrapping Member::fromMnemonic() with out parameters for publicKey and memberId
    - Implement CreateMember wrapping Member creation with out parameters
    - Implement SignData and VerifySignature wrapping signing operations
    - _Requirements: 1.2, 1.4, 2.2, 2.4, 15.7_

  - [ ]* 1.3 Write property tests for mnemonic validation
    - **Property 2: Mnemonic Validation Correctness**
    - **Validates: Requirements 2.2**

  - [ ]* 1.4 Write property tests for key derivation determinism
    - **Property 3: Key Derivation Determinism**
    - **Validates: Requirements 1.4, 2.4**

- [ ] 2. Implement security layer (DPAPI / Windows Hello)
  - [ ] 2.1 Implement DpapiKeyring with Windows Hello and DPAPI fallback
    - Create DpapiKeyring implementing IDpapiKeyring
    - Detect Windows Hello availability via KeyCredentialManager.IsSupportedAsync()
    - Implement EncryptAsync using Windows Hello credential guard when available
    - Implement DecryptAsync with biometric, PIN, or password authentication
    - Implement DPAPI user-scope fallback via ProtectedData.Protect/Unprotect
    - Implement DeleteKeyAsync for secure key erasure
    - Implement HasKey for key existence check
    - _Requirements: 3.1, 3.2, 3.3, 3.5, 3.6_

  - [ ]* 2.2 Write property tests for DPAPI encryption round-trip
    - **Property 4: DPAPI Encryption Round-Trip**
    - **Validates: Requirements 1.5, 3.2**

- [ ] 3. Checkpoint - Ensure SDK bridge and security tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 4. Implement BlockStore service layer
  - [ ] 4.1 Implement BlockStoreBridge C++/CLI wrapper
    - Create BlockStoreBridge managed ref class wrapping DiskBlockStore
    - Implement StoreBlock with SHA3-512 checksum computation
    - Implement StoreBlockWithMetadata for blocks with associated metadata
    - Implement GetBlock with integrity verification
    - Implement HasBlock, DeleteBlock operations
    - Implement GetMetadata, GetStats operations
    - _Requirements: 11.1, 11.2, 11.3, 11.4, 11.5, 11.7_

  - [ ] 4.2 Implement BlockStoreService C# wrapper
    - Create BlockStoreService implementing IBlockStoreService as ObservableObject
    - Implement Store, Retrieve, Exists, Delete operations delegating to BlockStoreBridge
    - Implement GetStorageStats with category breakdown
    - Implement CleanupAsync with CleanupPolicy support
    - _Requirements: 8.1, 8.2, 8.5, 8.6_

  - [ ]* 4.3 Write property tests for BlockStore operations
    - **Property 30: BlockStore Directory Structure**
    - **Property 31: BlockStore Checksum Verification**
    - **Property 32: BlockStore Block Size Support**
    - **Property 33: BlockStore Metadata Creation**
    - **Property 34: BlockStore Statistics Accuracy**
    - **Validates: Requirements 11.1, 11.2, 11.3, 11.4, 11.5, 11.7**

  - [ ]* 4.4 Write property tests for block integrity
    - **Property 23: Block Integrity Verification**
    - **Validates: Requirements 8.7**

- [ ] 5. Implement CryptoBridge and CryptoService
  - [ ] 5.1 Implement CryptoBridge C++/CLI wrapper
    - Create CryptoBridge managed ref class wrapping ECIES operations
    - Implement EncryptData for single recipient public key
    - Implement EncryptDataForRecipients for multiple recipient public keys
    - Implement DecryptData with private key
    - Implement CreateCbl for CBL creation with checksums, creator ID, signature
    - Implement GetCblAddresses for CBL address extraction
    - _Requirements: 5.1, 5.4, 6.3, 6.4_

  - [ ] 5.2 Implement CryptoService C# wrapper
    - Create CryptoService implementing ICryptoService
    - Implement Encrypt, Decrypt delegating to CryptoBridge
    - Implement Sign, Verify delegating to BrightChainSdkBridge
    - _Requirements: 5.1, 5.4_

  - [ ]* 5.3 Write property tests for encryption round-trip
    - **Property 8: Message Encryption Round-Trip**
    - **Property 14: Block Encryption for Recipients**
    - **Validates: Requirements 5.1, 5.4, 6.3**

- [ ] 6. Checkpoint - Ensure storage and crypto tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 7. Implement authentication flow
  - [ ] 7.1 Implement AuthManager
    - Create AuthManager implementing IAuthManager as ObservableObject
    - Implement RegisterAsync: generate mnemonic via SdkBridge, derive keys, encrypt private key via DpapiKeyring, create Member, obtain JWT from VaultApiClient
    - Implement LoginAsync: validate mnemonic, derive keys, verify against stored encrypted key, establish session with JWT
    - Implement LogoutAsync: clear session, invalidate JWT, clear sensitive data from memory
    - Implement ValidateMnemonic delegating to SdkBridge
    - _Requirements: 1.1-1.7, 2.1-2.7_

  - [ ] 7.2 Implement MemberModel and SessionState data models
    - Create MemberModel record with Id, MemberId, Name, Email, Type, PublicKey, dates
    - Create SessionState record with MemberId, EncryptedPrivateKey, LoginTime, LastActivity, Jwt, JwtExpiry
    - Implement System.Text.Json serialization attributes
    - _Requirements: 1.6, 2.5, 2.6_

  - [ ] 7.3 Implement registration UI flow (WinUI 3 XAML)
    - Create RegistrationPage with name/email form using WinUI 3 controls
    - Create MnemonicDisplayPage for showing generated 12 words
    - Create MnemonicConfirmationPage for user verification
    - Create RegistrationViewModel using CommunityToolkit.Mvvm [RelayCommand] and [ObservableProperty]
    - Wire pages to AuthManager via ViewModel
    - _Requirements: 1.1, 1.2, 1.3, 1.7_

  - [ ] 7.4 Implement login UI flow (WinUI 3 XAML)
    - Create LoginPage with mnemonic input, name, and email fields
    - Add real-time validation feedback for invalid mnemonics
    - Implement navigation to MainPage on success via Frame navigation
    - Create LoginViewModel with CommunityToolkit.Mvvm
    - _Requirements: 2.1, 2.3, 2.5_

  - [ ]* 7.5 Write property tests for mnemonic generation validity
    - **Property 1: Mnemonic Generation Validity**
    - **Validates: Requirements 1.2**

  - [ ]* 7.6 Write unit tests for authentication flows
    - Test registration success flow
    - Test login with valid/invalid mnemonic
    - Test logout clears session and JWT
    - _Requirements: 1.1-1.7, 2.1-2.7_

- [ ] 8. Implement messaging foundation
  - [ ] 8.1 Implement Conversation and Message data models
    - Create Conversation record with Id, Participants, CreatedAt, LastMessageAt, LastMessagePreview, UnreadCount
    - Create Message record with Id, ConversationId, SenderId, Timestamp, Content, CblChecksum, Status
    - Create MessageContent record with Text and Attachments
    - Create AttachmentReference record with Filename, MimeType, Size, CblChecksum
    - Implement System.Text.Json serialization
    - _Requirements: 4.1, 4.4, 5.5_

  - [ ] 8.2 Implement MessageManager
    - Create MessageManager implementing IMessageManager as ObservableObject
    - Implement LoadConversationsAsync from local storage
    - Implement CreateConversationAsync with unique Guid generation
    - Implement SendMessageAsync: encrypt with ECIES via CryptoService, store as blocks, create CBL
    - Implement LoadMessagesAsync with decryption and pagination
    - Implement DeleteConversationAsync preserving blocks in BlockStore
    - _Requirements: 4.1-4.6, 5.1-5.7_

  - [ ]* 8.3 Write property tests for conversation management
    - **Property 5: Conversation ID Uniqueness**
    - **Property 6: Conversation Display Completeness**
    - **Property 7: Conversation Deletion Block Preservation**
    - **Validates: Requirements 4.3, 4.4, 4.6**

  - [ ]* 8.4 Write property tests for message operations
    - **Property 9: Message Storage Block Creation**
    - **Property 10: CBL Block Reference Integrity**
    - **Property 11: Message Display Completeness**
    - **Property 12: Attachment Block Separation**
    - **Validates: Requirements 5.2, 5.3, 5.5, 5.7**

- [ ] 9. Implement messaging UI (WinUI 3)
  - [ ] 9.1 Create ConversationListPage
    - Display list of conversations with previews using ListView
    - Show most recent message and timestamp
    - Support creating new conversations
    - Create ConversationListViewModel with CommunityToolkit.Mvvm
    - _Requirements: 4.1, 4.2, 4.4_

  - [ ] 9.2 Create ConversationDetailPage
    - Display message history with sender and timestamp using ItemsRepeater
    - Implement message composition and sending
    - Handle decryption errors gracefully with error indicator
    - Create ConversationDetailViewModel
    - _Requirements: 4.5, 5.5, 5.6_

  - [ ] 9.3 Create RecipientSelectionDialog
    - Allow selection of recipients by Member ID or contact name using AutoSuggestBox
    - Support multiple recipient selection for group conversations
    - Implement as ContentDialog
    - _Requirements: 4.2_

- [ ] 10. Checkpoint - Ensure authentication and messaging tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 11. Implement file sharing
  - [ ] 11.1 Implement FileReference and CBL data models
    - Create FileReference record with Type, Checksum, Filename, MimeType, Size, CreatedAt, CreatorId
    - Create CblReference and SuperCblReference records
    - Create MagnetUrl record with Parse and ToString methods
    - Implement System.Text.Json serialization
    - _Requirements: 6.5, 7.1, 7.2_

  - [ ] 11.2 Implement FileShareManager
    - Create FileShareManager implementing IFileShareManager as ObservableObject
    - Implement UploadFileAsync: chunked upload via VaultApiClient (POST init, PUT chunks, POST finalize)
    - Implement ResumeUploadAsync: query session status, resume from last chunk
    - Implement DownloadFileAsync via GET `/burnbag/files/:id`
    - Implement DownloadVersionAsync via GET `/burnbag/files/:id/versions/:versionId/download`
    - Implement SearchFilesAsync via GET `/burnbag/files/search`
    - Implement GetFileMetadataAsync, SoftDeleteAsync, RestoreAsync, GetNonAccessProofAsync
    - Implement ParseReference for Magnet URLs and CBL files
    - Implement GenerateMagnetUrl
    - Implement GetMissingBlocks
    - Handle 413 quota exceeded on upload
    - _Requirements: 6.1-6.8, 7.1-7.9_

  - [ ]* 11.3 Write property tests for file operations
    - **Property 13: File Block Size Selection**
    - **Property 15: Reference Generation Parseability**
    - **Property 16: Magnet URL Parse Round-Trip**
    - **Property 17: CBL Parse Correctness**
    - **Property 18: File Split-Reassemble Round-Trip**
    - **Validates: Requirements 6.2, 6.5, 7.1, 7.2, 7.5**

- [ ] 12. Implement file sharing UI (WinUI 3)
  - [ ] 12.1 Create FileUploadPage
    - Implement file picker via Windows.Storage.Pickers.FileOpenPicker
    - Display chunk-level upload progress with ProgressBar and cancellation
    - Show file metadata and vault creation ledger entry hash on completion
    - Display quota exceeded message on 413 with link to quota settings
    - Create FileUploadViewModel
    - _Requirements: 6.1, 6.6, 6.7, 6.8_

  - [ ] 12.2 Create FileDownloadPage
    - Implement Magnet URL and CBL file input
    - Display download progress with ProgressBar and estimated time
    - Handle missing blocks gracefully with recovery suggestions
    - Create FileDownloadViewModel
    - _Requirements: 7.1, 7.2, 7.6, 7.9_

  - [ ] 12.3 Create FileListPage
    - Display local files with metadata using DataGrid or ListView
    - Support sharing via Magnet URL or CBL export
    - Create FileListViewModel
    - _Requirements: 6.5_

- [ ] 13. Implement storage management
  - [ ] 13.1 Implement StorageManager
    - Create StorageManager implementing IStorageManager as ObservableObject
    - Implement GetUsageByCategory (Messages, Files, System, Cache)
    - Implement SetStorageLimit with enforcement during block creation
    - Implement PerformCleanupAsync with orphan detection
    - Implement VerifyIntegrityAsync with checksum validation
    - Implement RefreshServerQuotaAsync delegating to QuotaManager
    - _Requirements: 8.1-8.7_

  - [ ] 13.2 Create StorageSettingsPage (WinUI 3)
    - Display local storage usage with category breakdown using charts
    - Display server-side quota from Vault API
    - Allow setting local storage limits with NumberBox
    - Provide cleanup and integrity check actions
    - Create StorageSettingsViewModel
    - _Requirements: 8.1, 8.2, 8.3, 8.4, 8.5_

  - [ ]* 13.3 Write property tests for storage management
    - **Property 19: Storage Categorization Consistency**
    - **Property 20: Storage Limit Enforcement**
    - **Property 21: Orphan Cleanup Safety**
    - **Property 22: CBL Reference Consistency After Deletion**
    - **Validates: Requirements 8.2, 8.3, 8.5, 8.6**

- [ ] 14. Checkpoint - Ensure file sharing and storage tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 15. Implement Vault API client foundation
  - [ ] 15.1 Implement VaultEndpoint and VaultApiException types
    - Create VaultEndpoint class/struct for all `/burnbag/*` routes with HTTP method, path, and body
    - Create VaultApiException with VaultApiErrorKind enum mapping all HTTP errors (400, 401, 403, 404, 409, 410, 413, 422)
    - Implement hex ID validation helper (`^[0-9a-fA-F]{32}$`)
    - _Requirements: 16.3, 16.6_

  - [ ] 15.2 Implement VaultApiClient core HTTP client
    - Create VaultApiClient implementing IVaultApiClient
    - Implement JWT Bearer header attachment on every authenticated request
    - Implement JWT refresh on 401 with single retry
    - Implement HTTP error → VaultApiException mapping with server message preservation
    - Implement exponential backoff retry (up to 3 times) for non-destructive requests
    - Implement JSON serialization/deserialization via System.Text.Json
    - Implement configurable base URL and timeout from AppSettings
    - _Requirements: 16.1, 16.2, 16.3, 16.4, 16.5, 16.7_

  - [ ] 15.3 Implement Vault API data models
    - Create all System.Text.Json-serializable models: VaultFileMetadata, FileVersion, FileSearchResult, FileSearchFilters
    - Create upload models: UploadSession, UploadChunkResult, UploadSessionStatus
    - Create folder models: VaultFolder, FolderContents, BreadcrumbItem, SortField, SortDirection, ItemType
    - Create ACL models: PermissionFlag, PermissionLevel, AclEntry, AclDocument, EffectivePermissions, PermissionSet, AclTargetType
    - Create sharing models: ShareLinkMode, ShareLink, SharedFile, ShareAuditEntry
    - Create destruction models: DestructionProof, VerificationBundle, DestructionResult, BatchDestructionResult, ProofVerification, NonAccessProof
    - Create canary models: CanaryBinding, CreateCanaryBindingRequest, UpdateCanaryBindingRequest, RecipientList, Recipient, DryRunResult
    - Create quorum models: QuorumOperationType, QuorumRequest, QuorumApprovalResult, QuorumRejectionResult, QuorumExecutionResult
    - Create audit models: AuditEntry, AuditDetails, AuditFilters, ComplianceReportRequest, ComplianceReport and sub-records
    - Create notification model: VaultNotification
    - Create quota models: QuotaInfo, QuotaBreakdownItem
    - Create TCBL export models: TcblExportRequest, TcblExportResult, SkippedFile
    - _Requirements: 16.5, 17-26_

  - [ ] 15.4 Implement MockVaultApiClient for testing
    - Create MockVaultApiClient implementing IVaultApiClient
    - Support configurable responses per endpoint path
    - Support configurable error injection
    - Track request count and last request for assertions
    - _Requirements: 16 (testing infrastructure)_

  - [ ]* 15.5 Write property tests for Vault API client
    - **Property 38: JWT Header Attachment**
    - **Property 39: HTTP Error Code to Typed Error Mapping**
    - **Property 40: Vault API Model JSON Round-Trip**
    - **Property 41: Hex ID Format Validation**
    - **Validates: Requirements 16.1, 16.3, 16.5, 16.6**

  - [ ]* 15.6 Write unit tests for Vault API client
    - Test JWT attachment to requests
    - Test JWT refresh on 401
    - Test exponential backoff retry (1, 2, 3 failures)
    - Test configurable base URL and timeout
    - Test invalid hex ID rejection
    - _Requirements: 16.1-16.7_

- [ ] 16. Checkpoint - Ensure Vault API client tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 17. Implement Vault folder management
  - [ ] 17.1 Implement VaultFolderManager
    - Create VaultFolderManager implementing IVaultFolderManager as ObservableObject
    - Implement GetRootFolderAsync via GET `/burnbag/folders/root`
    - Implement GetFolderContentsAsync via GET `/burnbag/folders/:id` with sort params
    - Implement CreateFolderAsync via POST `/burnbag/folders`
    - Implement GetBreadcrumbPathAsync via GET `/burnbag/folders/:id/path`
    - Implement MoveItemAsync via POST `/burnbag/folders/:id/move`
    - _Requirements: 17.1-17.7_

  - [ ] 17.2 Create FolderBrowserPage (WinUI 3)
    - Display folder contents with files and subfolders using TreeView or ListView
    - Show breadcrumb navigation path using BreadcrumbBar
    - Support creating new folders via ContentDialog
    - Support moving files/folders via drag-and-drop or context menu
    - Display quorum-governed indicator on governed folders
    - Handle circular reference error (409) gracefully
    - Create FolderBrowserViewModel
    - _Requirements: 17.1-17.7_

  - [ ]* 17.3 Write unit tests for folder management
    - Test fetch root folder contents
    - Test navigate into subfolder with sorting
    - Test create folder
    - Test move file to different folder
    - Test handle circular reference error (409)
    - Test display breadcrumb path
    - _Requirements: 17.1-17.7_

- [ ] 18. Implement Vault ACL management
  - [ ] 18.1 Implement VaultAclManager
    - Create VaultAclManager implementing IVaultAclManager as ObservableObject
    - Implement GetAclAsync via GET `/burnbag/acl/:targetType/:targetId`
    - Implement SetAclAsync via PUT `/burnbag/acl/:targetType/:targetId`
    - Implement GetEffectivePermissionsAsync via GET `/burnbag/acl/:targetType/:targetId/effective/:principalId`
    - Implement CreatePermissionSetAsync via POST `/burnbag/acl/permission-sets`
    - Implement ListPermissionSetsAsync via GET `/burnbag/acl/permission-sets`
    - _Requirements: 18.1-18.7_

  - [ ] 18.2 Create AclEditorPage (WinUI 3)
    - Display current ACL entries for a file or folder
    - Allow assigning built-in levels (viewer, commenter, editor, owner) and custom Permission Sets via ComboBox
    - Show effective permissions for a principal with inheritance source
    - Inform user that folder ACLs cascade to descendants via InfoBar
    - Disable ACL editing when user lacks `admin` flag
    - Create AclEditorViewModel
    - _Requirements: 18.1-18.7_

  - [ ]* 18.3 Write property tests for ACL management
    - **Property 43: Permission-Gated UI Control State**
    - **Validates: Requirements 18.7, 19.8**

  - [ ]* 18.4 Write unit tests for ACL management
    - Test fetch ACL for file/folder
    - Test set ACL with built-in levels
    - Test create custom permission set
    - Test fetch effective permissions
    - Test disable ACL editing without admin flag
    - _Requirements: 18.1-18.7_

- [ ] 19. Implement Vault file sharing (three-tier)
  - [ ] 19.1 Update FileShareManager for Vault API upload/download
    - Implement UploadFileAsync via Vault chunked upload (POST init, PUT chunks, POST finalize)
    - Implement ResumeUploadAsync via GET session status + resume from last chunk
    - Implement DownloadFileAsync via GET `/burnbag/files/:id`
    - Implement DownloadVersionAsync via GET `/burnbag/files/:id/versions/:versionId/download`
    - Implement SearchFilesAsync via GET `/burnbag/files/search`
    - Implement GetFileMetadataAsync via GET `/burnbag/files/:id/metadata`
    - Implement SoftDeleteAsync via DELETE `/burnbag/files/:id`
    - Implement RestoreAsync via POST `/burnbag/files/:id/restore`
    - Implement GetNonAccessProofAsync via GET `/burnbag/files/:id/non-access-proof`
    - Handle 413 quota exceeded on upload
    - _Requirements: 6.1-6.8, 7.1-7.9_

  - [ ] 19.2 Implement VaultShareManager
    - Create VaultShareManager implementing IVaultShareManager as ObservableObject
    - Implement ShareInternalAsync via POST `/burnbag/share/internal`
    - Implement CreateShareLinkAsync via POST `/burnbag/share/link` (server_proxied, ephemeral_key_pair, recipient_public_key modes)
    - Implement GetSharedWithMeAsync via GET `/burnbag/share/shared-with-me`
    - Implement RevokeShareLinkAsync via DELETE `/burnbag/share/link/:id`
    - Implement GetMagnetUrlAsync via GET `/burnbag/share/:fileId/magnet`
    - Implement GetShareAuditAsync via GET `/burnbag/share/:fileId/audit`
    - _Requirements: 19.1-19.8_

  - [ ] 19.3 Create VaultFileDetailPage (WinUI 3)
    - Display file metadata, version history, ACL ID, destruction schedule
    - Show non-access proof verification result
    - Provide share, delete, restore, destroy actions gated by permissions
    - Create VaultFileDetailViewModel
    - _Requirements: 7.4, 7.8, 19.8_

  - [ ] 19.4 Create SharePage (WinUI 3)
    - Support internal sharing with recipient selection and permission level via ComboBox
    - Support external share link creation with mode selection and security trade-off explanation via TeachingTip
    - Display shared-with-me list
    - Show share audit trail with timestamps and IP addresses
    - Disable sharing controls when user lacks `share` flag
    - Create ShareViewModel
    - _Requirements: 19.1-19.8_

  - [ ] 19.5 Update FileUploadPage for Vault chunked upload
    - Display chunk-level progress during upload with ProgressBar
    - Support cancellation via CancellationToken and resume of interrupted uploads
    - Show file metadata and vault creation ledger entry hash on completion
    - Display quota exceeded message on 413 with link to quota settings
    - _Requirements: 6.1-6.8_

  - [ ]* 19.6 Write property tests for file upload
    - **Property 42: Upload Chunk Checksum Integrity**
    - **Validates: Requirements 6.3**

  - [ ]* 19.7 Write unit tests for Vault file sharing
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

- [ ] 20. Implement cryptographic destruction
  - [ ] 20.1 Implement VaultDestructionManager
    - Create VaultDestructionManager implementing IVaultDestructionManager as ObservableObject
    - Implement DestroyImmediatelyAsync via POST `/burnbag/destroy/:fileId`
    - Implement ScheduleDestructionAsync via POST `/burnbag/destroy/:fileId/schedule`
    - Implement CancelScheduledDestructionAsync via DELETE `/burnbag/destroy/:fileId/schedule`
    - Implement DestroyBatchAsync via POST `/burnbag/destroy/batch`
    - Implement VerifyProofAsync via POST `/burnbag/destroy/:fileId/verify`
    - _Requirements: 20.1-20.7_

  - [ ] 20.2 Create DestructionPage (WinUI 3)
    - Display destruction proof (merkle root, bloom witness, ledger entry hash) after immediate destruction
    - Show scheduled destruction date in file metadata views with prominent InfoBar
    - Support batch destruction with per-file result display
    - Support destruction proof verification with validity and ledger confirmation display
    - Route quorum-governed files through Quorum workflow before destruction
    - Create DestructionViewModel
    - _Requirements: 20.1-20.7_

  - [ ]* 20.3 Write property tests for destruction routing
    - **Property 44: Quorum Routing for Governed Targets**
    - **Validates: Requirements 20.7, 22.1**

  - [ ]* 20.4 Write unit tests for destruction
    - Test immediate destruction with proof display
    - Test schedule future destruction
    - Test cancel scheduled destruction
    - Test batch destruction with per-file results
    - Test verify destruction proof
    - Test route quorum-governed file through quorum
    - _Requirements: 20.1-20.7_

- [ ] 21. Implement canary protocols
  - [ ] 21.1 Implement CanaryManager
    - Create CanaryManager implementing ICanaryManager as ObservableObject
    - Implement ListBindingsAsync via GET `/burnbag/canary/bindings`
    - Implement ListRecipientListsAsync via GET `/burnbag/canary/recipients`
    - Implement CreateBindingAsync via POST `/burnbag/canary/bindings`
    - Implement CreateRecipientListAsync via POST `/burnbag/canary/recipients`
    - Implement DryRunAsync via POST `/burnbag/canary/bindings/:id/dry-run`
    - Implement UpdateBindingAsync via PATCH `/burnbag/canary/bindings/:id`
    - Implement DeleteBindingAsync via DELETE `/burnbag/canary/bindings/:id`
    - Implement UpdateRecipientListAsync via PATCH `/burnbag/canary/recipients/:id`
    - _Requirements: 21.1-21.8_

  - [ ] 21.2 Create CanarySettingsPage (WinUI 3)
    - Display list of canary bindings with condition, provider, and enabled status using ListView
    - Support creating new bindings with provider selection grouped by category via ComboBox
    - Display dry run simulation results (files affected, recipient count, actions) in ContentDialog
    - Support updating and deleting bindings
    - Manage recipient lists (create, update with name, email, optional public key)
    - Show timeout duration and last signal timestamp for absence bindings with prominent styling
    - Create CanarySettingsViewModel
    - _Requirements: 21.1-21.8_

  - [ ]* 21.3 Write property tests for canary protocols
    - **Property 45: Canary Provider Category Completeness**
    - **Validates: Requirements 21.3**

  - [ ]* 21.4 Write unit tests for canary protocols
    - Test list canary bindings
    - Test create canary binding
    - Test dry run simulation
    - Test update and delete binding
    - Test create and update recipient list
    - Test display absence binding with timeout
    - _Requirements: 21.1-21.8_

- [ ] 22. Implement quorum-governed operations
  - [ ] 22.1 Implement QuorumManager
    - Create QuorumManager implementing IQuorumManager as ObservableObject
    - Implement SubmitRequestAsync via POST `/quorum/request`
    - Implement ApproveAsync via POST `/quorum/:requestId/approve` with ECDSA signature from CryptoService
    - Implement RejectAsync via POST `/quorum/:requestId/reject`
    - _Requirements: 22.1-22.6_

  - [ ] 22.2 Create QuorumApprovalPage (WinUI 3)
    - Display quorum request details (operation type, target, required/current approvals, expiration)
    - Support approve action with ECDSA signature from CryptoService
    - Support reject action with optional reason via TextBox in ContentDialog
    - Display execution result when approval threshold is reached
    - Handle expired request (410) gracefully with InfoBar
    - Create QuorumApprovalViewModel
    - _Requirements: 22.1-22.6_

  - [ ]* 22.3 Write unit tests for quorum operations
    - Test submit quorum request
    - Test approve with ECDSA signature
    - Test reject with reason
    - Test display execution result on threshold reached
    - Test handle expired request (410)
    - _Requirements: 22.1-22.6_

- [ ] 23. Implement audit log viewing
  - [ ] 23.1 Implement AuditManager
    - Create AuditManager implementing IAuditManager as ObservableObject
    - Implement QueryAuditLogAsync via GET `/burnbag/audit` with filters
    - Implement ExportAuditLogAsync via GET `/burnbag/audit/export`
    - Implement GenerateComplianceReportAsync via POST `/burnbag/audit/compliance-report`
    - _Requirements: 23.1-23.5_

  - [ ] 23.2 Create AuditLogPage (WinUI 3)
    - Display audit entries with actor ID, target ID, operation type, details, ledger entry hash, timestamp using DataGrid or ListView
    - Support filtering by actor, target, operation type, date range, pagination via filter controls
    - Support exporting audit log via FileSavePicker
    - Display compliance report with summary, access patterns, destruction events, sharing activity, non-access proofs
    - Create AuditLogViewModel
    - _Requirements: 23.1-23.5_

  - [ ]* 23.3 Write property tests for audit display
    - **Property 46: Audit Entry Display Completeness**
    - **Validates: Requirements 23.2**

  - [ ]* 23.4 Write unit tests for audit operations
    - Test query audit log with filters
    - Test export audit log
    - Test generate compliance report
    - Test display all report sections
    - _Requirements: 23.1-23.5_

- [ ] 24. Implement notifications and quota
  - [ ] 24.1 Implement NotificationManager
    - Create NotificationManager implementing INotificationManager as ObservableObject
    - Implement PollNotificationsAsync via GET `/burnbag/notifications`
    - Implement MarkAsReadAsync via POST `/burnbag/notifications/read`
    - Track UnreadCount as [ObservableProperty]
    - _Requirements: 24.1-24.7_

  - [ ] 24.2 Implement QuotaManager
    - Create QuotaManager implementing IQuotaManager as ObservableObject
    - Implement FetchQuotaAsync via GET `/burnbag/quota`
    - Track CurrentQuota as [ObservableProperty]
    - _Requirements: 25.1-25.5_

  - [ ] 24.3 Create NotificationCenterPage (WinUI 3)
    - Display unread notifications with type-specific icons using ListView
    - Provide navigation action for quorum_request → QuorumApprovalPage
    - Provide navigation action for share_received → shared file view
    - Display canary alerts with elevated visual prominence via InfoBar with Severity=Warning
    - Show unread count badge on notification center icon using InfoBadge
    - Support marking notifications as read
    - Create NotificationCenterViewModel
    - _Requirements: 24.1-24.7_

  - [ ] 24.4 Create QuotaPage (WinUI 3)
    - Display used bytes, quota bytes, and percentage used with ProgressBar
    - Show usage breakdown by category (files, versions) using chart or grouped list
    - Display visual warning indicator when usage exceeds 80% via InfoBar with Severity=Warning
    - Create QuotaViewModel
    - _Requirements: 25.1-25.5_

  - [ ]* 24.5 Write property tests for notifications
    - **Property 47: Notification Type Navigation Routing**
    - **Property 48: Unread Notification Count Accuracy**
    - **Validates: Requirements 24.3, 24.4, 24.7**

  - [ ]* 24.6 Write property tests for quota
    - **Property 49: Quota Usage Percentage Calculation**
    - **Property 50: Quota Warning Threshold**
    - **Validates: Requirements 25.1, 25.3**

  - [ ]* 24.7 Write unit tests for notifications and quota
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

- [ ] 25. Implement TCBL folder export
  - [ ] 25.1 Implement TCBL export in VaultFolderManager
    - Add ExportTcblAsync method via POST `/burnbag/folders/:id/export-tcbl` with MIME type filters, max depth, exclusion patterns
    - _Requirements: 26.1-26.5_

  - [ ] 25.2 Create TcblExportPage (WinUI 3)
    - Present export options (MIME type filters, max depth, exclusion patterns) via form controls
    - Save TCBL data to user-selected file location via FileSavePicker
    - Display total file count and size on completion
    - Display skipped files with reasons
    - Handle 422 (no exportable files) with informative InfoBar message
    - Create TcblExportViewModel
    - _Requirements: 26.1-26.5_

  - [ ]* 25.3 Write unit tests for TCBL export
    - Test export folder with default options
    - Test export with MIME type filters and depth limit
    - Test display skipped files with reasons
    - Test handle 422 no exportable files
    - _Requirements: 26.1-26.5_

- [ ] 26. Checkpoint - Ensure all Vault API layer tests pass
  - Ensure all tests pass, ask the user if questions arise.

- [ ] 27. Implement CfApi-backed virtual drive
  - [ ] 27.1 Implement CfApiProvider (Windows Cloud Files API)
    - Create CfApiProvider implementing ICfApiProvider
    - Implement RegisterSyncRootAsync via CfRegisterSyncRoot P/Invoke
    - Implement UnregisterSyncRootAsync via CfUnregisterSyncRoot P/Invoke
    - Implement CreatePlaceholderAsync via CfCreatePlaceholders P/Invoke
    - Implement HydratePlaceholderAsync via CfHydratePlaceholder P/Invoke
    - Implement DehydratePlaceholderAsync via CfDehydratePlaceholder P/Invoke
    - Define P/Invoke signatures for cfapi.h functions
    - _Requirements: 9.1, 9.5_

  - [ ] 27.2 Implement VirtualDriveManager (Vault API-backed)
    - Create VirtualDriveManager implementing IVirtualDriveManager as ObservableObject
    - Implement RegisterAsync: register CfApi sync root at user-configured path
    - Implement UnregisterAsync: unregister sync root and release resources
    - Back file listing from Vault folder hierarchy via VaultFolderManager
    - Implement on-demand file hydration: download and decrypt via Vault API when placeholder is opened
    - Implement local cache for downloaded content with eviction policy
    - Implement ImportReferenceAsync for Magnet URL and CBL imports
    - Implement RemoveContentAsync for removing virtual drive entries
    - Maintain ContentCatalog of all imported content references
    - _Requirements: 9.1-9.7, 10.1-10.7_

  - [ ] 27.3 Create VirtualDriveSettingsPage (WinUI 3)
    - Display sync root registration status and path
    - Provide register/unregister controls
    - Show Vault API connectivity status
    - Show troubleshooting for registration failures via InfoBar
    - Allow configuring sync root path via FolderPicker
    - Create VirtualDriveSettingsViewModel
    - _Requirements: 9.1, 9.5, 9.6, 13.5_

  - [ ]* 27.4 Write property tests for virtual drive
    - **Property 24: Virtual Drive File Listing Completeness** (from Vault folder hierarchy)
    - **Property 25: Virtual Drive File Operations** (read/stat via Vault API download)
    - **Validates: Requirements 9.2, 9.7**

  - [ ]* 27.5 Write unit tests for virtual drive
    - Test register CfApi sync root
    - Test unregister CfApi sync root
    - Test list files from Vault folder hierarchy as placeholders
    - Test hydrate placeholder on file open via Vault API download
    - Test dehydrate placeholder to free local space
    - Test import Magnet URL to virtual drive
    - Test import CBL to virtual drive
    - Test handle Vault API errors during hydration
    - _Requirements: 9.1-9.7, 10.1-10.7_

- [ ] 28. Implement network connectivity
  - [ ] 28.1 Implement NetworkManager
    - Create NetworkManager implementing INetworkManager as ObservableObject
    - Implement ConnectAsync/DisconnectAsync with configured endpoints
    - Implement CheckVaultApiReachabilityAsync
    - Implement RequestBlockAsync from peers
    - Implement AnnounceBlockAsync to peers
    - Implement SyncPendingOperationsAsync on reconnect
    - Manage PendingOperation queue for offline operations
    - _Requirements: 12.1-12.7_

  - [ ] 28.2 Create NetworkStatusPage (WinUI 3)
    - Display connection status and Vault API reachability with status indicators
    - Show connected peers in ListView
    - Allow endpoint configuration via TextBox and NumberBox
    - Create NetworkStatusViewModel
    - _Requirements: 12.2, 12.7_

  - [ ]* 28.3 Write property tests for network operations
    - **Property 35: Block Serving Policy Enforcement**
    - **Property 37: Failed Operation Queuing**
    - **Validates: Requirements 12.4, 14.4**

- [ ] 29. Implement application settings
  - [ ] 29.1 Implement AppSettings model and persistence
    - Create AppSettings record with all configuration options including VaultApiBaseUrl and VaultApiTimeout
    - Implement persistence using Windows.Storage.ApplicationData or System.Text.Json file storage
    - Implement Default static property with sensible defaults (LocalApplicationData paths, 10GB limit, etc.)
    - _Requirements: 13.1-13.7, 16.7_

  - [ ] 29.2 Create SettingsPage (WinUI 3)
    - Organize settings into logical categories using NavigationView or Pivot
    - Implement storage, network, virtual drive, Vault API settings sections
    - Apply settings without restart when possible
    - Create SettingsViewModel
    - _Requirements: 13.1, 13.2, 13.6_

  - [ ]* 29.3 Write property tests for settings persistence
    - **Property 36: Settings Persistence Round-Trip**
    - **Validates: Requirements 13.7**

- [ ] 30. Implement error handling and logging
  - [ ] 30.1 Implement BrightChainError and error handling infrastructure
    - Create BrightChainError class hierarchy with all error categories including VaultApi(VaultApiException)
    - Implement user-friendly messages and recovery suggestions per error type
    - Implement error logging without exposing sensitive data (keys, JWTs redacted)
    - _Requirements: 14.1, 14.2, 14.3_

  - [ ] 30.2 Create ErrorDialog and error presentation (WinUI 3)
    - Display user-friendly error messages via ContentDialog
    - Provide actionable recovery suggestions (retry, refreshJWT, checkNetwork, freeStorage, upgradeQuota, requestQuorumApproval)
    - Support diagnostic export for critical errors via FileSavePicker
    - _Requirements: 14.1, 14.2, 14.7_

  - [ ] 30.3 Implement operation logging and diagnostics
    - Create LoggingService with configurable LogLevel (Debug, Info, Warning, Error)
    - Implement log viewer in settings page
    - Implement diagnostic export
    - _Requirements: 14.6, 14.7_

- [ ] 31. Implement main application shell (WinUI 3)
  - [ ] 31.1 Create MainWindow with NavigationView
    - Implement WinUI 3 NavigationView with left navigation pane
    - Include navigation items: Messaging, Files/Folders, Sharing, Storage, Canary, Audit, Notifications, Settings
    - Display network status and Vault API connectivity indicator in title bar or status area
    - Display unread notification count badge using InfoBadge on Notifications nav item
    - Support Windows light/dark themes following system theme
    - Support Windows system accent colors
    - _Requirements: 4.1, 12.2, 15.2, 15.3, 15.4, 24.7_

  - [ ] 31.2 Wire all components together with dependency injection
    - Initialize all managers and services at app launch (AuthManager, MessageManager, FileShareManager, all Vault API managers)
    - Configure dependency injection via Microsoft.Extensions.DependencyInjection
    - Handle app lifecycle events (poll notifications on activation, refresh quota)
    - _Requirements: 12.1, 13.6, 24.1, 25.5_

- [ ] 32. Windows platform integration
  - [ ] 32.1 Implement Windows platform-specific features
    - Ensure app compiles and runs on Windows 10 (1809+) and Windows 11
    - Implement Windows Hello biometric authentication integration (facial recognition, fingerprint, PIN)
    - Support high contrast mode and screen reader compatibility (AutomationProperties)
    - Support keyboard navigation throughout the application
    - Verify MSIX packaging and deployment
    - _Requirements: 15.1-15.7_

  - [ ]* 32.2 Write smoke tests for Windows platform
    - Test app compiles and launches on Windows 10 (1809+)
    - Test app compiles and launches on Windows 11
    - Test CfApi sync root registers on supported Windows versions
    - Test Windows Hello authentication flow
    - Test DPAPI fallback when Windows Hello unavailable
    - Test NavigationView renders with correct navigation items
    - Test light/dark theme switching
    - _Requirements: 15.1-15.7_

- [ ] 33. Integration tests
  - [ ]* 33.1 Write end-to-end integration tests
    - Test authenticate → upload file → share internally → download
    - Test create folder → upload file to folder → navigate → download
    - Test upload → schedule destruction → verify proof
    - Test create canary binding → dry run → delete
    - Test quorum request → approve → execution
    - Test offline operation and sync on reconnect
    - _Requirements: all_

- [ ] 34. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties adapted for C#/Windows (FsCheck or similar PBT library)
- Unit tests validate specific examples and edge cases (xUnit)
- The C++/CLI bridge projects require /clr compilation and separate project configuration
- The CfApi virtual drive requires Windows 10 1809+ and appropriate P/Invoke declarations
- DPAPI/Windows Hello replaces macOS Secure Enclave for hardware-backed key protection
- WinUI 3 NavigationView replaces macOS sidebar / iOS tab bar navigation
- Tasks 15-26 cover the full Vault API layer which is the primary new work
- The virtual drive is backed by the Vault API folder hierarchy via CfApi cloud file placeholders

## Future Features (Not Yet Specified)

The following features are planned for future iterations and will require their own requirements/design work:

### Network Economy Layer
- **UPnP Port Forwarding**: Auto-expose ports to the internet for peer-to-peer connectivity with other BrightChain nodes
- **Joules Accounting System**: Users elect a percentage of their storage space and network capacity to contribute to the network, measured in "Joules" (similar to Ethereum Gas)
- **Resource Allocation Settings**: UI for users to configure how much storage/bandwidth they contribute
- **Accounting Ledger Integration**: Track Joules earned (from serving blocks/storage to others) and spent (for using others' resources)
- **Node Discovery & Quorum Participation**: Connect to and participate in the broader BrightChain network

*Note: The Joules economy and accounting ledger are documented in the main BrightChain specification (not available in this workspace).*
