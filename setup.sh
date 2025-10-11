#!/bin/bash
# setup.sh - Setup script for ftwin third-party dependencies
# This script applies patches to vendored third-party code

set -e  # Exit on error
set -u  # Exit on undefined variable

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Script directory (works even if script is sourced or symlinked)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PATCHES_DIR="${SCRIPT_DIR}/patches"
THIRD_PARTY_DIR="${SCRIPT_DIR}/third-party"

# Function to print colored messages
print_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Function to check if a patch has already been applied
is_patch_applied() {
    local target_dir="$1"
    local patch_file="$2"

    # Try to apply in reverse and check-only mode
    # If it succeeds, the patch is already applied
    if git -C "${target_dir}" apply --reverse --check "${patch_file}" &>/dev/null; then
        return 0  # Patch is applied
    else
        return 1  # Patch is not applied
    fi
}

# Function to apply a patch
apply_patch() {
    local target_dir="$1"
    local patch_file="$2"
    local patch_name=$(basename "${patch_file}")

    if [ ! -f "${patch_file}" ]; then
        print_error "Patch file not found: ${patch_file}"
        return 1
    fi

    if [ ! -d "${target_dir}" ]; then
        print_error "Target directory not found: ${target_dir}"
        return 1
    fi

    print_info "Applying patch: ${patch_name} to ${target_dir}"

    # Check if patch is already applied
    if is_patch_applied "${target_dir}" "${patch_file}"; then
        print_warn "Patch ${patch_name} is already applied, skipping"
        return 0
    fi

    # Apply the patch
    if git -C "${target_dir}" apply "${patch_file}"; then
        print_info "Successfully applied patch: ${patch_name}"
        return 0
    else
        print_error "Failed to apply patch: ${patch_name}"
        return 1
    fi
}

# Main execution
main() {
    print_info "Starting third-party dependency setup for ftwin"
    print_info "Script directory: ${SCRIPT_DIR}"

    # Check if patches directory exists
    if [ ! -d "${PATCHES_DIR}" ]; then
        print_error "Patches directory not found: ${PATCHES_DIR}"
        exit 1
    fi

    # Check if third-party directory exists
    if [ ! -d "${THIRD_PARTY_DIR}" ]; then
        print_error "Third-party directory not found: ${THIRD_PARTY_DIR}"
        exit 1
    fi

    # Apply libpuzzle patch
    LIBPUZZLE_DIR="${THIRD_PARTY_DIR}/libpuzzle"
    LIBPUZZLE_PATCH="${PATCHES_DIR}/libpuzzle-fix-test.patch"

    if [ -d "${LIBPUZZLE_DIR}" ]; then
        apply_patch "${LIBPUZZLE_DIR}" "${LIBPUZZLE_PATCH}" || exit 1
    else
        print_warn "libpuzzle directory not found: ${LIBPUZZLE_DIR}"
        print_warn "Make sure submodules are initialized: git submodule update --init --recursive"
    fi

    # Add more patches here as needed
    # Example:
    # apply_patch "${THIRD_PARTY_DIR}/other-lib" "${PATCHES_DIR}/other-lib.patch"

    print_info "Third-party dependency setup completed successfully"
}

# Run main function
main "$@"
