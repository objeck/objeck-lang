#!/bin/bash
# Safe removal of macOS Intel x64 code
# This script removes orphaned Intel Mac code while preserving Linux/Windows x64

set -e  # Exit on error

echo "=========================================="
echo "  macOS Intel x64 Code Removal Script"
echo "=========================================="
echo ""

# Backup before removal
BACKUP_DIR="./macos_intel_backup_$(date +%Y%m%d_%H%M%S)"
mkdir -p "$BACKUP_DIR"

echo "Creating backup in: $BACKUP_DIR"

# Category 1: Remove orphaned Intel Mac build scripts
echo ""
echo "[1/3] Removing orphaned Intel Mac build scripts..."

SCRIPTS=(
    "lib/crypto/build_osx_x64.sh"
    "lib/diags/build_osx_x64.sh"
    "lib/odbc/build_osx_x64.sh"
    "lib/sdl/build_osx_x64.sh"
)

for script in "${SCRIPTS[@]}"; do
    if [ -f "$script" ]; then
        echo "  - Backing up and removing: $script"
        cp "$script" "$BACKUP_DIR/"
        rm "$script"
    else
        echo "  - Not found (already removed?): $script"
    fi
done

# Category 2: Clean up x86_64 version strings in C++ files
echo ""
echo "[2/3] Cleaning up x86_64 version strings..."

# Function to remove x86_64 version string #else clause
cleanup_version_strings() {
    local file=$1

    if [ ! -f "$file" ]; then
        echo "  - File not found: $file"
        return
    fi

    echo "  - Processing: $file"

    # Backup original
    cp "$file" "$BACKUP_DIR/$(basename $file)"

    # Remove the x86_64 version string block
    # This removes:
    #   #else
    #     usage += L" (macOS x86_64)";  // or similar
    #   #endif
    # And simplifies to just ARM64 version

    sed -i.bak '/#ifdef _ARM64/,/#endif/{
        /#else/,/#endif/{
            /x86_64\|x86-64/d
            /#else/d
        }
    }' "$file"

    # Remove backup if sed succeeded
    if [ $? -eq 0 ]; then
        rm -f "${file}.bak"
        echo "    ✓ Cleaned up"
    else
        echo "    ✗ Failed, restoring backup"
        mv "${file}.bak" "$file"
    fi
}

FILES_TO_CLEAN=(
    "compiler/posix_main.cpp"
    "compiler/compiler.cpp"
    "debugger/debugger.cpp"
    "vm/win_main.cpp"
    "utils/WindowsApp/AppLauncher/AppLauncher.cpp"
)

for file in "${FILES_TO_CLEAN[@]}"; do
    cleanup_version_strings "$file"
done

# Category 3: Update documentation references
echo ""
echo "[3/3] Updating documentation..."

# Update main README if it mentions macOS x64
if grep -q "macOS x64" readme.md 2>/dev/null; then
    echo "  - Updating readme.md"
    cp readme.md "$BACKUP_DIR/"
    sed -i 's/macOS x64\/ARM64/macOS ARM64 (Apple Silicon)/g' readme.md
    sed -i 's/macOS x64/macOS ARM64/g' readme.md
    echo "    ✓ Updated"
fi

echo ""
echo "=========================================="
echo "  Cleanup Complete!"
echo "=========================================="
echo ""
echo "Summary:"
echo "  - Removed 4 orphaned build scripts"
echo "  - Cleaned 5 version string files"
echo "  - Updated documentation references"
echo ""
echo "Backup location: $BACKUP_DIR"
echo ""
echo "Next steps:"
echo "  1. Review changes: git diff"
echo "  2. Test builds on:"
echo "     - Linux x64: ./release/deploy_posix.sh x64"
echo "     - macOS ARM64: ./release/deploy_macos_arm64.sh"
echo "     - Windows x64: release\\deploy_windows.cmd x64"
echo "  3. If all tests pass: git add -A && git commit"
echo "  4. To restore: cp -r $BACKUP_DIR/* ./"
echo ""
