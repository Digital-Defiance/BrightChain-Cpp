#!/bin/bash
# BrightChain C++ - Master Build Script
# Usage: ./brightchain.sh [command]

set -e

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
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

cmd_test() {
    print_header "Running Tests"
    if [ ! -f "${BUILD_DIR}/tests/brightchain_tests" ]; then
        print_error "Tests not built. Building first..."
        cmd_build
    fi
    "${BUILD_DIR}/tests/brightchain_tests"
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

cmd_help() {
    cat << EOF
BrightChain C++ - Master Build Script

Usage: $0 [command]

Commands:
  configure       Configure CMake build
  build           Build the project
  clean           Remove build directory
  rebuild         Clean + configure + build
  
  test            Run all tests (98+ tests)
  test-suite <S>  Run specific test suite (e.g., MemberTest)
  test-voting     Run all voting & Paillier tests
  test-xplatform  Generate C++ test vectors for cross-platform verification
  
  examples        Run all examples
  example <name>  Run specific example (member, block_storage, ecies, block_types)
  
  verify          Full verification (rebuild + test + examples)
  quick           Quick build and test
  
  help            Show this help message

Examples:
  $0 build                    # Build everything
  $0 test                     # Run all tests
  $0 test-voting              # Run voting/Paillier tests only
  $0 test-xplatform           # Generate cross-platform test vectors
  $0 test-suite MemberTest    # Run member tests
  $0 example member           # Run member example
  $0 verify                   # Full verification
  $0 quick                    # Quick build & test

EOF
}

# Main
case "${1:-help}" in
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
