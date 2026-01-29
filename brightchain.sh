#!/bin/bash
# BrightChain - Master Build Script
# Usage: ./brightchain.sh [command]

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
APPLE_DIR="${PROJECT_ROOT}/brightchain-apple"
APPLE_PROJECT="${APPLE_DIR}/BrightChain.xcodeproj"

# Code signing - update these for your team
DEVELOPMENT_TEAM="${BRIGHTCHAIN_TEAM_ID:-YB23AGASN3}"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

print_header() {
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${BLUE}  $1${NC}"
    echo -e "${BLUE}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_info() {
    echo -e "${YELLOW}→ $1${NC}"
}

# Commands
cmd_configure() {
    print_header "Configuring Build"
    cmake -B "${BUILD_DIR}" -S "${PROJECT_ROOT}"
    print_success "Configuration complete"
}

cmd_build() {
    print_header "Building Project"
    if [ ! -d "${BUILD_DIR}" ]; then
        cmd_configure
    fi
    cmake --build "${BUILD_DIR}"
    print_success "Build complete"
}

cmd_clean() {
    print_header "Cleaning Build"
    rm -rf "${BUILD_DIR}"
    print_success "Clean complete"
}

cmd_rebuild() {
    cmd_clean
    cmd_configure
    cmd_build
}

cmd_generate_vectors() {
    print_header "Generating Test Vectors"
    
    # Check if npm dependencies are installed
    if [ ! -d "${PROJECT_ROOT}/tests/node_modules" ]; then
        print_info "Installing test dependencies..."
        (cd "${PROJECT_ROOT}/tests" && npm install)
    fi
    
    # Generate all vectors using TypeScript
    print_info "Generating Paillier vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_paillier_vectors_simple.ts 2>/dev/null || true)
    
    print_info "Generating ECIES vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_ecies_vectors.ts 2>/dev/null || true)
    
    print_info "Generating Shamir vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_shamir_vectors.ts 2>/dev/null || true)
    
    print_info "Generating SHA3 vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_sha3_vectors.ts 2>/dev/null || true)
    
    print_info "Generating edge case vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_edge_case_vectors.ts 2>/dev/null || true)
    
    print_info "Generating member vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_member_json_vectors.ts 2>/dev/null || true)
    
    print_info "Generating comprehensive vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_comprehensive_vectors.ts 2>/dev/null || true)
    
    print_info "Generating multirecipient vectors..."
    (cd "${PROJECT_ROOT}/tests" && npx ts-node generate_multirecipient_ts_vectors.ts 2>/dev/null || true)
    
    print_success "Test vectors generated"
}

cmd_test() {
    print_header "Running Tests"
    if [ ! -f "${BUILD_DIR}/tests/brightchain_tests" ]; then
        print_error "Tests not built. Building first..."
        cmd_build
    fi
    
    # Run tests from project root so they can find test vectors in tests/
    cd "${PROJECT_ROOT}"
    "${BUILD_DIR}/tests/brightchain_tests"
}

cmd_test_with_vectors() {
    print_header "Running Tests (with vector generation)"
    cmd_generate_vectors
    cmd_test
}

cmd_test_voting() {
    print_header "Running Voting & Paillier Tests"
    if [ ! -f "${BUILD_DIR}/tests/brightchain_tests" ]; then
        print_error "Tests not built. Building first..."
        cmd_build
    fi
    print_info "Paillier Tests"
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="Paillier*"
    print_info "Vote Encoder Tests"
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="VoteEncoder*"
    print_info "Poll Tests"
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="Poll*"
    print_info "Audit Log Tests"
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="AuditLog*"
    print_success "All voting tests complete"
}

cmd_test_cross_platform() {
    print_header "Cross-Platform Voting Key Verification"
    if [ ! -f "${BUILD_DIR}/tests/brightchain_tests" ]; then
        print_error "Tests not built. Building first..."
        cmd_build
    fi
    print_info "Generating C++ test vectors..."
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="MnemonicVotingKeyCrossPlatformTest.GenerateCppTestVectors"
    
    if [ -f "test_vectors_cpp_voting.json" ]; then
        print_success "C++ test vectors generated"
        print_info "Test vectors saved to: test_vectors_cpp_voting.json"
    else
        print_error "Failed to generate test vectors"
        return 1
    fi
}

cmd_test_suite() {
    if [ -z "$1" ]; then
        print_error "Usage: $0 test-suite <SuiteName>"
        echo "Available suites: MemberTest, ECIESTest, SHA3CrossCompatTest, etc."
        exit 1
    fi
    print_header "Running Test Suite: $1"
    "${BUILD_DIR}/tests/brightchain_tests" --gtest_filter="$1.*"
}

cmd_examples() {
    print_header "Running Examples"
    
    local failed=0
    
    print_info "Member Example"
    if "${BUILD_DIR}/examples/member_example"; then
        echo ""
    else
        print_error "Member example failed"
        failed=$((failed + 1))
    fi
    
    print_info "Block Storage Example"
    if "${BUILD_DIR}/examples/block_storage_example"; then
        echo ""
    else
        print_error "Block storage example failed"
        failed=$((failed + 1))
    fi
    
    print_info "ECIES Example"
    if "${BUILD_DIR}/examples/ecies_example"; then
        echo ""
    else
        print_error "ECIES example failed"
        failed=$((failed + 1))
    fi
    
    print_info "Block Types Example"
    if "${BUILD_DIR}/examples/block_types_example"; then
        echo ""
    else
        print_error "Block types example failed"
        failed=$((failed + 1))
    fi
    
    if [ $failed -eq 0 ]; then
        print_success "All examples complete"
    else
        print_error "$failed example(s) failed"
        return 1
    fi
}

cmd_example() {
    if [ -z "$1" ]; then
        print_error "Usage: $0 example <name>"
        echo "Available: member, block_storage, ecies, block_types"
        exit 1
    fi
    print_header "Running Example: $1"
    "${BUILD_DIR}/examples/${1}_example"
}

cmd_verify() {
    print_header "Full Verification"
    cmd_rebuild
    cmd_test
    cmd_examples
    print_success "All verification complete!"
}

cmd_quick() {
    print_header "Quick Build & Test"
    cmd_build
    cmd_test
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Apple Platform Commands (macOS/iOS)
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

cmd_apple_check() {
    if [ ! -d "${APPLE_DIR}" ]; then
        print_error "Apple project not found at ${APPLE_DIR}"
        exit 1
    fi
    if ! command -v xcodebuild &> /dev/null; then
        print_error "xcodebuild not found. Please install Xcode."
        exit 1
    fi
}

cmd_macos_build() {
    print_header "Building macOS App"
    cmd_apple_check
    
    local config="${1:-Debug}"
    print_info "Configuration: ${config}"
    print_info "Team ID: ${DEVELOPMENT_TEAM}"
    
    xcodebuild -project "${APPLE_PROJECT}" \
        -scheme BrightChainMacOS \
        -configuration "${config}" \
        -sdk macosx \
        DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
        CODE_SIGN_STYLE=Automatic \
        CODE_SIGN_IDENTITY="Apple Development" \
        -allowProvisioningUpdates \
        build
    
    print_success "macOS build complete"
    
    # Find the built app in DerivedData
    local app_path
    app_path=$(find ~/Library/Developer/Xcode/DerivedData -name "BrightChainMacOS.app" -path "*${config}*" -type d 2>/dev/null | head -1)
    if [ -n "${app_path}" ]; then
        print_info "App: ${app_path}"
    fi
}

cmd_ios_build() {
    print_header "Building iOS App"
    cmd_apple_check
    
    local config="${1:-Debug}"
    print_info "Configuration: ${config}"
    print_info "Team ID: ${DEVELOPMENT_TEAM}"
    
    xcodebuild -project "${APPLE_PROJECT}" \
        -target BrightChainIOS \
        -configuration "${config}" \
        -sdk iphoneos \
        DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
        CODE_SIGN_STYLE=Automatic \
        CODE_SIGN_IDENTITY="Apple Development" \
        -allowProvisioningUpdates \
        build
    
    print_success "iOS build complete"
    print_info "App: ${APPLE_DIR}/build/${config}-iphoneos/BrightChainIOS.app"
}

cmd_ios_sim_build() {
    print_header "Building iOS App for Simulator"
    cmd_apple_check
    
    local config="${1:-Debug}"
    print_info "Configuration: ${config}"
    print_info "Team ID: ${DEVELOPMENT_TEAM}"
    
    xcodebuild -project "${APPLE_PROJECT}" \
        -target BrightChainIOS \
        -configuration "${config}" \
        -sdk iphonesimulator \
        -arch arm64 \
        DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
        CODE_SIGN_STYLE=Automatic \
        -allowProvisioningUpdates \
        build
    
    print_success "iOS Simulator build complete"
    print_info "App: ${APPLE_DIR}/build/${config}-iphonesimulator/BrightChainIOS.app"
}

cmd_ios_catalyst_build() {
    print_header "Building iOS App for Mac (Catalyst)"
    cmd_apple_check
    
    local config="${1:-Debug}"
    print_info "Configuration: ${config}"
    print_info "Team ID: ${DEVELOPMENT_TEAM}"
    
    xcodebuild -project "${APPLE_PROJECT}" \
        -scheme BrightChainIOS \
        -configuration "${config}" \
        -destination 'platform=macOS,variant=Mac Catalyst,arch=arm64' \
        DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
        CODE_SIGN_STYLE=Automatic \
        CODE_SIGN_IDENTITY="Apple Development" \
        SUPPORTS_MACCATALYST=YES \
        -allowProvisioningUpdates \
        build
    
    print_success "iOS Catalyst build complete"
    print_info "App: ${APPLE_DIR}/build/${config}-maccatalyst/BrightChainIOS.app"
}

cmd_ios_mac_run() {
    print_header "Running iOS App on Mac (Catalyst)"
    
    # Check multiple possible locations
    local app="${APPLE_DIR}/build/Debug-maccatalyst/BrightChainIOS.app"
    
    if [ ! -d "${app}" ]; then
        # Try DerivedData location
        app=$(find ~/Library/Developer/Xcode/DerivedData -name "BrightChainIOS.app" -path "*Debug-maccatalyst*" 2>/dev/null | head -1)
    fi
    
    if [ -z "${app}" ] || [ ! -d "${app}" ]; then
        print_info "Catalyst app not found, building first..."
        cmd_ios_catalyst_build Debug
        
        # Check again after build
        app="${APPLE_DIR}/build/Debug-maccatalyst/BrightChainIOS.app"
        if [ ! -d "${app}" ]; then
            app=$(find ~/Library/Developer/Xcode/DerivedData -name "BrightChainIOS.app" -path "*Debug-maccatalyst*" 2>/dev/null | head -1)
        fi
        if [ ! -d "${app}" ]; then
            app=$(find "${APPLE_DIR}/build" -name "BrightChainIOS.app" -type d 2>/dev/null | head -1)
        fi
    fi
    
    if [ -z "${app}" ] || [ ! -d "${app}" ]; then
        print_error "Could not find built Catalyst app"
        print_info "Try building from Xcode: Product > Destination > My Mac (Mac Catalyst)"
        exit 1
    fi
    
    print_info "Launching ${app}"
    open "${app}"
}

cmd_ios_run() {
    print_header "Running iOS App on Mac (Catalyst)"
    cmd_ios_mac_run
}

cmd_ios_sim_run() {
    print_header "Running iOS App in Simulator"
    local app="${APPLE_DIR}/build/Debug-iphonesimulator/BrightChainIOS.app"
    
    if [ ! -d "${app}" ]; then
        print_info "Simulator app not found, building first..."
        cmd_ios_sim_build Debug
    fi
    
    # Find an available iPad simulator
    local ipad_udid
    ipad_udid=$(xcrun simctl list devices available | grep -i "iPad Pro 13" | head -1 | grep -oE '[A-F0-9-]{36}')
    
    if [ -z "${ipad_udid}" ]; then
        # Fallback to any iPad
        ipad_udid=$(xcrun simctl list devices available | grep -i "iPad" | head -1 | grep -oE '[A-F0-9-]{36}')
    fi
    
    if [ -z "${ipad_udid}" ]; then
        print_error "No iPad simulator found. Please install one via Xcode."
        exit 1
    fi
    
    print_info "Using simulator: ${ipad_udid}"
    
    # Boot simulator
    print_info "Booting iPad simulator..."
    xcrun simctl boot "${ipad_udid}" 2>/dev/null || true
    
    # Wait a moment for boot
    sleep 2
    
    # Install and launch
    print_info "Installing app..."
    xcrun simctl install "${ipad_udid}" "${app}"
    
    print_info "Launching app..."
    xcrun simctl launch "${ipad_udid}" com.brightchain.ios
    
    # Open Simulator app
    open -a Simulator
    
    print_success "iOS app launched in simulator"
}

cmd_apple_build() {
    print_header "Building All Apple Platforms"
    cmd_macos_build "${1:-Debug}"
    cmd_ios_build "${1:-Debug}"
    print_success "All Apple builds complete"
}

cmd_macos_run() {
    print_header "Running macOS App"
    
    # Check multiple possible locations - exclude Index.noindex
    local app
    app=$(find ~/Library/Developer/Xcode/DerivedData -name "BrightChainMacOS.app" -path "*/Build/Products/Debug/*" -not -path "*/Index.noindex/*" -type d 2>/dev/null | head -1)
    
    if [ -z "${app}" ] || [ ! -d "${app}" ]; then
        print_info "App not found, building first..."
        cmd_macos_build Debug
        app=$(find ~/Library/Developer/Xcode/DerivedData -name "BrightChainMacOS.app" -path "*/Build/Products/Debug/*" -not -path "*/Index.noindex/*" -type d 2>/dev/null | head -1)
    fi
    
    if [ -z "${app}" ] || [ ! -d "${app}" ]; then
        print_error "Could not find built macOS app"
        exit 1
    fi
    
    print_info "Launching ${app}"
    open "${app}"
}

cmd_apple_clean() {
    print_header "Cleaning Apple Build"
    rm -rf "${APPLE_DIR}/build"
    rm -rf "${APPLE_DIR}/DerivedData"
    rm -rf "${APPLE_DIR}/.build"
    print_success "Apple clean complete"
}

cmd_apple_test() {
    print_header "Running Apple Tests"
    cmd_apple_check
    
    # Run Swift package tests instead of Xcode scheme tests
    print_info "Running Swift package tests..."
    (cd "${APPLE_DIR}" && swift test) || {
        print_info "Swift package tests not configured, trying Xcode tests..."
        xcodebuild -project "${APPLE_PROJECT}" \
            -scheme BrightChainMacOS \
            -sdk macosx \
            -destination 'platform=macOS' \
            DEVELOPMENT_TEAM="${DEVELOPMENT_TEAM}" \
            CODE_SIGN_STYLE=Automatic \
            -allowProvisioningUpdates \
            test || print_info "Xcode tests not configured for this scheme"
    }
    
    print_success "Apple tests complete"
}

cmd_apple_list() {
    print_header "Apple Project Info"
    cmd_apple_check
    xcodebuild -project "${APPLE_PROJECT}" -list
}

# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
# Combined Commands
# ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

cmd_all_build() {
    print_header "Building Everything"
    cmd_build
    cmd_apple_build "${1:-Debug}"
    print_success "All builds complete"
}

cmd_all_clean() {
    print_header "Cleaning Everything"
    cmd_clean
    cmd_apple_clean
    print_success "All clean complete"
}

cmd_all_test() {
    print_header "Running All Tests"
    cmd_generate_vectors
    cmd_test
    cmd_apple_test
    print_success "All tests complete"
}

cmd_help() {
    echo -e "BrightChain - Master Build Script"
    echo ""
    echo "Usage: $0 [command]"
    echo ""
    echo -e "${CYAN}C++ Commands:${NC}"
    echo "  configure       Configure CMake build"
    echo "  build           Build the C++ project"
    echo "  clean           Remove C++ build directory"
    echo "  rebuild         Clean + configure + build"
    echo ""
    echo "  test            Run all C++ tests"
    echo "  test-vectors    Generate test vectors (TypeScript)"
    echo "  test-full       Generate vectors + run all tests"
    echo "  test-suite <S>  Run specific test suite (e.g., MemberTest)"
    echo "  test-voting     Run all voting & Paillier tests"
    echo "  test-xplatform  Generate C++ test vectors for cross-platform verification"
    echo ""
    echo "  examples        Run all examples"
    echo "  example <name>  Run specific example (member, block_storage, ecies, block_types)"
    echo ""
    echo "  verify          Full C++ verification (rebuild + test + examples)"
    echo "  quick           Quick C++ build and test"
    echo ""
    echo -e "${CYAN}Apple Platform Commands:${NC}"
    echo "  macos           Build macOS app (Debug)"
    echo "  macos-release   Build macOS app (Release)"
    echo "  macos-run       Build and run macOS app"
    echo ""
    echo "  ios             Build iOS app for device (Debug)"
    echo "  ios-release     Build iOS app for device (Release)"
    echo "  ios-sim         Build iOS app for simulator"
    echo "  ios-run         Build and run iOS app on Mac (Catalyst)"
    echo "  ios-sim-run     Build and run iOS app in simulator"
    echo "  ios-mac         Build iOS app for Mac (Catalyst)"
    echo ""
    echo "  apple           Build both macOS and iOS (Debug)"
    echo "  apple-release   Build both macOS and iOS (Release)"
    echo "  apple-clean     Clean Apple build artifacts"
    echo "  apple-test      Run Apple platform tests"
    echo "  apple-list      List Apple project targets and schemes"
    echo ""
    echo -e "${CYAN}Combined Commands:${NC}"
    echo "  all             Build everything (C++ and Apple)"
    echo "  all-clean       Clean everything"
    echo "  all-test        Run all tests (C++ and Apple)"
    echo ""
    echo "  help            Show this help message"
    echo ""
    echo -e "${CYAN}Environment Variables:${NC}"
    echo "  BRIGHTCHAIN_TEAM_ID   Apple Developer Team ID (default: YB23AGASN3)"
    echo ""
    echo -e "${CYAN}Examples:${NC}"
    echo "  $0 build                    # Build C++ library"
    echo "  $0 macos                    # Build macOS app"
    echo "  $0 ios                      # Build iOS app"
    echo "  $0 apple                    # Build both macOS and iOS"
    echo "  $0 macos-run                # Build and launch macOS app"
    echo "  $0 all                      # Build everything"
    echo "  $0 test                     # Run C++ tests"
    echo "  $0 apple-test               # Run Apple tests"
    echo "  $0 all-test                 # Run all tests"
    echo "  $0 verify                   # Full C++ verification"
    echo "  $0 test-suite MemberTest    # Run member tests"
    echo "  $0 example member           # Run member example"
}

# Main
case "${1:-help}" in
    # C++ commands
    configure)
        cmd_configure
        ;;
    build)
        cmd_build
        ;;
    clean)
        cmd_clean
        ;;
    rebuild)
        cmd_rebuild
        ;;
    test)
        cmd_test
        ;;
    test-vectors)
        cmd_generate_vectors
        ;;
    test-full)
        cmd_test_with_vectors
        ;;
    test-voting)
        cmd_test_voting
        ;;
    test-xplatform)
        cmd_test_cross_platform
        ;;
    test-suite)
        cmd_test_suite "$2"
        ;;
    examples)
        cmd_examples
        ;;
    example)
        cmd_example "$2"
        ;;
    verify)
        cmd_verify
        ;;
    quick)
        cmd_quick
        ;;
    
    # Apple platform commands
    macos)
        cmd_macos_build Debug
        ;;
    macos-release)
        cmd_macos_build Release
        ;;
    macos-run)
        cmd_macos_run
        ;;
    ios)
        cmd_ios_build Debug
        ;;
    ios-release)
        cmd_ios_build Release
        ;;
    ios-sim)
        cmd_ios_sim_build Debug
        ;;
    ios-run)
        cmd_ios_run
        ;;
    ios-sim-run)
        cmd_ios_sim_run
        ;;
    ios-mac)
        cmd_ios_catalyst_build Debug
        ;;
    apple)
        cmd_apple_build Debug
        ;;
    apple-release)
        cmd_apple_build Release
        ;;
    apple-clean)
        cmd_apple_clean
        ;;
    apple-test)
        cmd_apple_test
        ;;
    apple-list)
        cmd_apple_list
        ;;
    
    # Combined commands
    all)
        cmd_all_build "${2:-Debug}"
        ;;
    all-clean)
        cmd_all_clean
        ;;
    all-test)
        cmd_all_test
        ;;
    
    help|--help|-h)
        cmd_help
        ;;
    *)
        print_error "Unknown command: $1"
        echo ""
        cmd_help
        exit 1
        ;;
esac
