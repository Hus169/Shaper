#!/data/data/com.termux/files/usr/bin/bash
# ==============================================================================
# File: sync.sh
# ==============================================================================
# Usage:
#   ./sync.sh "commit message here"
#
# If no message is given, a timestamped default is generated.
# ==============================================================================

set -e  # Halt execution on first critical error

REPO_DIR="$HOME/Shaper"
COMMIT_MSG="${1:-sync $(date '+%Y-%m-%d %H:%M')}"

echo "==> [Axiom] Validating repository location: $REPO_DIR"
if [ ! -d "$REPO_DIR" ]; then
    echo "[-] ERROR: $REPO_DIR does not exist."
    echo "    Fix: git clone https://github.com/Hus169/Shaper.git \"$REPO_DIR\""
    exit 1
fi

cd "$REPO_DIR"

if [ ! -d ".git" ]; then
    echo "[-] ERROR: No .git directory found in $REPO_DIR."
    echo "    The repository structure is corrupted or missing."
    exit 1
fi

# Neutralize Termux shared-storage 'dubious ownership' warnings
git config --global --add safe.directory "$REPO_DIR" 2>/dev/null || true

echo "==> [Axiom] Scanning for local modifications..."
STATUS=$(git status --porcelain)
if [ -z "$STATUS" ]; then
    echo "[+] Working tree is clean. No changes to synchronize."
    exit 0
fi

echo "$STATUS"

# Detect if the remote repository is empty (no 'main' branch exists yet)
if git ls-remote --exit-code --heads origin main >/dev/null 2>&1; then
    echo "==> [Axiom] Remote 'main' branch detected. Fetching and integrating changes..."
    git pull origin main --no-rebase || {
        echo "[-] ERROR: Pull failed. Resolve merge conflicts manually before syncing."
        exit 1
    }
else
    echo "==> [Axiom] Empty remote repository detected. Skipping pull for initial commit."
fi

echo "==> [Axiom] Staging all modifications..."
git add -A

echo "==> [Axiom] Committing changes: \"$COMMIT_MSG\""
git commit -m "$COMMIT_MSG"

# Enforce 'main' as the primary branch name
git branch -M main

echo "==> [Axiom] Pushing to remote repository (origin/main)..."
git push -u origin main

echo "[+] Synchronization complete. Monitor the GitHub Actions tab for APK compilation."
