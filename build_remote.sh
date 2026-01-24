#!/usr/bin/env bash

# Remote build script for DXVK Remix (Custom)
# Invokes build_dxvk.ps1 with custom arguments on Windows VM over SSH

set -e

# SSH connection details
SSH_HOST="cr@localhost"
SSH_PORT="2222"
WINDOWS_REPO_PATH="C:\Users\cr\proj\dxvk-remix-gmod"

# Check if arguments were provided
if [ $# -eq 0 ]; then
    echo "Usage: $0 [PowerShell arguments for build_dxvk.ps1]"
    echo ""
    echo "Examples:"
    echo "  $0 -BuildFlavour release -BuildSubDir _CompTest -Backend ninja"
    echo "  $0 -BuildFlavour debugoptimized -Backend vs"
    echo ""
    echo "For predefined builds, use:"
    echo "  ./build_remote_release.sh  - Release build"
    echo "  ./build_remote_debug.sh    - Debug build"
    exit 1
fi

echo "Starting remote custom build on Windows VM..."
echo "Repository path: $WINDOWS_REPO_PATH"
echo "Arguments: $@"
echo ""

# Convert bash arguments to PowerShell format
# Pass all arguments directly to the PowerShell script
ssh -p "$SSH_PORT" "$SSH_HOST" -t "powershell -ExecutionPolicy Bypass -Command \"Set-Location '$WINDOWS_REPO_PATH'; . '.\\build_dxvk.ps1'; PerformBuild $@\""

SSH_EXIT_CODE=$?

if [ $SSH_EXIT_CODE -eq 0 ]; then
    echo ""
    echo "Remote build completed successfully!"
else
    echo ""
    echo "Remote build failed with exit code: $SSH_EXIT_CODE"
    exit $SSH_EXIT_CODE
fi
