# Implementation Plan: BrightChain macOS Client

## Overview

This implementation plan breaks down the BrightChain macOS Client into discrete coding tasks. The approach prioritizes building foundational components first (SDK bridge, security, storage), then layering features (authentication, messaging, file sharing), and finally integrating the virtual drive. Each task builds incrementally on previous work.

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

- [x] 15. Implement virtual drive
  - [x] 15.1 Implement ContentCatalog
    - Create ContentCatalog for tracking imported references
    - Implement add, remove, find operations
    - Implement persistence to local storage
    - _Requirements: 10.7_

  - [x] 15.2 Implement VirtualDriveManager
    - Create VirtualDriveManager conforming to VirtualDriveManagerProtocol
    - Implement mount/unmount operations
    - Implement importReference for Magnet URLs, CBLs, SuperCBLs
    - Implement removeContent
    - Track availability status based on local blocks
    - _Requirements: 9.1, 9.5, 10.1-10.6_

  - [x] 15.3 Implement BrightChainFileProviderExtension
    - Create File Provider extension with NSFileProviderExtension
    - Implement item enumeration for catalog entries
    - Implement providePlaceholder for file metadata
    - Implement startProvidingItem with block decryption and streaming
    - Implement stopProvidingItem for resource cleanup
    - _Requirements: 9.2, 9.3, 9.4, 9.7_

  - [x] 15.4 Write property tests for virtual drive
    - **Property 24: Virtual Drive File Listing Completeness**
    - **Property 25: Virtual Drive File Operations**
    - **Property 26: Content Import Catalog Addition**
    - **Property 27: Metadata Preservation on Import**
    - **Property 28: Availability Status Accuracy**
    - **Property 29: Catalog Completeness**
    - **Validates: Requirements 9.2, 9.7, 10.1-10.5, 10.7**

- [x] 16. Implement virtual drive UI
  - [x] 16.1 Create VirtualDriveSettingsView
    - Display mount status and mount point configuration
    - Provide mount/unmount controls
    - Show troubleshooting for mount failures
    - _Requirements: 9.1, 9.5, 9.6_

  - [x] 16.2 Create ContentImportView
    - Support importing Magnet URLs, CBL files, SuperCBLs
    - Display import status and availability
    - Offer to fetch missing blocks
    - _Requirements: 10.1, 10.2, 10.3, 10.5, 10.6_

- [ ] 17. Implement network connectivity
  - [ ] 17.1 Implement NetworkManager
    - Create NetworkManager conforming to NetworkManagerProtocol
    - Implement connect/disconnect with configured endpoints
    - Implement requestBlock from peers
    - Implement announceBlock to peers
    - Implement syncPendingOperations on reconnect
    - Manage pending operations queue
    - _Requirements: 12.1-12.7_

  - [ ] 17.2 Create NetworkStatusView
    - Display connection status
    - Show connected peers
    - Allow endpoint configuration
    - _Requirements: 12.2, 12.7_

  - [ ] 17.3 Write property tests for network operations
    - **Property 35: Block Serving Policy Enforcement**
    - **Property 37: Failed Operation Queuing**
    - **Validates: Requirements 12.4, 14.4**

- [x] 18. Implement application settings
  - [x] 18.1 Implement AppSettings model and persistence
    - Create AppSettings with all configuration options
    - Implement persistence using UserDefaults or file storage
    - Implement default values
    - _Requirements: 13.1-13.7_

  - [x] 18.2 Create SettingsView
    - Organize settings into logical categories
    - Implement storage, network, virtual drive settings sections
    - Apply settings without restart when possible
    - _Requirements: 13.1, 13.2, 13.6_

  - [x] 18.3 Write property tests for settings persistence
    - **Property 36: Settings Persistence Round-Trip**
    - **Validates: Requirements 13.7**

- [x] 19. Implement error handling and logging
  - [x] 19.1 Implement BrightChainError and error handling infrastructure
    - Create BrightChainError enum with all error categories
    - Implement user-friendly messages and recovery suggestions
    - Implement error logging without exposing sensitive data
    - _Requirements: 14.1, 14.2, 14.3_

  - [x] 19.2 Create ErrorAlertView and error presentation
    - Display user-friendly error messages
    - Provide actionable recovery suggestions
    - Support diagnostic export for critical errors
    - _Requirements: 14.1, 14.2, 14.7_

  - [x] 19.3 Implement operation logging and diagnostics
    - Create logging service with configurable levels
    - Implement log viewer in settings
    - Implement diagnostic export
    - _Requirements: 14.6, 14.7_

- [x] 20. Implement main application shell
  - [x] 20.1 Create MainView with navigation
    - Implement tab-based or sidebar navigation
    - Include messaging, files, storage, settings sections
    - Display network status indicator
    - _Requirements: 4.1, 12.2_

  - [x] 20.2 Wire all components together
    - Initialize all managers and services at app launch
    - Configure dependency injection
    - Handle app lifecycle events
    - _Requirements: 12.1, 13.6_

- [ ] 21. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- All tasks are required for comprehensive testing from the start
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The File Provider extension requires separate entitlements and provisioning


## Future Features (Not Yet Specified)

The following features are planned for future iterations and will require their own requirements/design work:

### Network Economy Layer
- **UPnP Port Forwarding**: Auto-expose ports to the internet for peer-to-peer connectivity with other BrightChain nodes
- **Joules Accounting System**: Users elect a percentage of their storage space and network capacity to contribute to the network, measured in "Joules" (similar to Ethereum Gas)
- **Resource Allocation Settings**: UI for users to configure how much storage/bandwidth they contribute
- **Accounting Ledger Integration**: Track Joules earned (from serving blocks/storage to others) and spent (for using others' resources)
- **Node Discovery & Quorum Participation**: Connect to and participate in the broader BrightChain network

*Note: The Joules economy and accounting ledger are documented in the main BrightChain specification (not available in this workspace).*
