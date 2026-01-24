#!/usr/bin/env bash

# Remote build script for DXVK Remix (Debug)
# Invokes build_dxvk_debug.ps1 on Windows VM over SSH

set -e

# SSH connection details
SSH_HOST="cr@localhost"
SSH_PORT="2222"
WINDOWS_REPO_PATH="C:\Users\cr\proj\dxvk-remix-gmod"

echo "Starting remote debug build on Windows VM..."
echo "Repository path: $WINDOWS_REPO_PATH"
echo ""

# Execute the PowerShell build script on the Windows VM
ssh -p "$SSH_PORT" "$SSH_HOST" -t "powershell -ExecutionPolicy Bypass -Command \"Set-Location '$WINDOWS_REPO_PATH'; & '.\\build_dxvk_debug.ps1'\""

SSH_EXIT_CODE=$?

if [ $SSH_EXIT_CODE -eq 0 ]; then
    echo ""
    echo "Remote build completed successfully!"
else
    echo ""
    echo "Remote build failed with exit code: $SSH_EXIT_CODE"
    exit $SSH_EXIT_CODE
fi
