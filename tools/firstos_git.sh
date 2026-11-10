#!/usr/bin/env bash
# FirstOS Git & Automated Backup / Safe Update System
# Enables tracking updates, creating system checkpoints/backups, and
# automatically rolling back whenever an update or build fails.

set -e

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BACKUP_DIR="$REPO_ROOT/backups"
CHECKPOINTS_FILE="$BACKUP_DIR/checkpoints.json"
mkdir -p "$BACKUP_DIR"

if [ ! -f "$CHECKPOINTS_FILE" ]; then
    echo "[]" > "$CHECKPOINTS_FILE"
fi

export PATH="/opt/homebrew/bin:$PATH"

log_info()  { echo -e "\033[1;34m[FirstOS-Git]\033[0m \033[1;32mINFO:\033[0m $*"; }
log_warn()  { echo -e "\033[1;34m[FirstOS-Git]\033[0m \033[1;33mWARN:\033[0m $*"; }
log_err()   { echo -e "\033[1;34m[FirstOS-Git]\033[0m \033[1;31mERROR:\033[0m $*"; }
log_succ()  { echo -e "\033[1;34m[FirstOS-Git]\033[0m \033[1;32mSUCCESS:\033[0m $*"; }

# ---------------------------------------------------------------------------
# Helper: record checkpoint in JSON
# ---------------------------------------------------------------------------
record_checkpoint() {
    local tag="$1"
    local commit_sha="$2"
    local msg="$3"
    local type="$4" # manual, pre-update, post-update
    local timestamp="$(date -u +"%Y-%m-%dT%H:%M:%SZ")"

    python3 - << EOF
import json, os
p = "$CHECKPOINTS_FILE"
try:
    with open(p, "r") as f:
        data = json.load(f)
except Exception:
    data = []

entry = {
    "tag": "$tag",
    "commit": "$commit_sha",
    "message": "$msg",
    "type": "$type",
    "timestamp": "$timestamp"
}
data.append(entry)
with open(p, "w") as f:
    json.dump(data, f, indent=2)
EOF
}

# ---------------------------------------------------------------------------
# cmd_status: show repository and backup state
# ---------------------------------------------------------------------------
cmd_status() {
    log_info "=== FirstOS Git & Backup Status ==="
    local branch="$(git rev-parse --abbrev-ref HEAD 2>/dev/null || echo "unknown")"
    local head_commit="$(git rev-parse --short HEAD 2>/dev/null || echo "none")"
    local commit_msg="$(git log -1 --pretty=%B 2>/dev/null | head -n 1 || echo "")"
    
    echo "  Current Branch:  $branch"
    echo "  Active Commit:   $head_commit ($commit_msg)"
    
    # Check dirty state
    if [ -z "$(git status --porcelain)" ]; then
        echo -e "  Working Tree:    \033[32mClean (All updates tracked)\033[0m"
    else
        echo -e "  Working Tree:    \033[33mUncommitted changes present\033[0m"
        git status -s
    fi

    echo ""
    log_info "Recent Backups / Restore Points:"
    python3 - << 'EOF'
import json, os
p = "backups/checkpoints.json"
if os.path.exists(p):
    try:
        with open(p, "r") as f:
            data = json.load(f)
        if not data:
            print("  (No backups created yet)")
        for item in data[-5:]:
            print(f"  • [{item.get('tag')}] ({item.get('timestamp')}) - {item.get('message')}")
    except Exception as e:
        print("  Error reading checkpoints:", e)
else:
    print("  (No backups created yet)")
EOF
}

# ---------------------------------------------------------------------------
# cmd_backup: create a snapshot/backup point
# ---------------------------------------------------------------------------
cmd_backup() {
    local label="${1:-manual_backup}"
    local sanitized_label="$(echo "$label" | tr ' /:;' '____')"
    local timestamp="$(date +"%Y%m%d_%H%M%S")"
    local tag="backup_${timestamp}_${sanitized_label}"

    log_info "Creating FirstOS system backup: '$tag'..."

    # Ensure changes are staged/committed if needed
    if [ -n "$(git status --porcelain)" ]; then
        log_info "Staging uncommitted changes before creating backup..."
        git add -A
        git commit -m "chore(backup): pre-backup snapshot '$label' at $timestamp"
    fi

    local commit_sha="$(git rev-parse HEAD)"
    git tag -a "$tag" -m "FirstOS Backup: $label (commit $commit_sha)"
    record_checkpoint "$tag" "$commit_sha" "$label" "manual"

    # Also create a self-contained git bundle in backups/
    git bundle create "$BACKUP_DIR/${tag}.bundle" HEAD
    log_succ "Backup '$tag' successfully created! (Commit: ${commit_sha:0:8})"
    log_succ "Bundle saved to: $BACKUP_DIR/${tag}.bundle"
}

# ---------------------------------------------------------------------------
# cmd_list: list all available backup points
# ---------------------------------------------------------------------------
cmd_list() {
    log_info "=== FirstOS Backup & Restore Checkpoints ==="
    python3 - << 'EOF'
import json, os
p = "backups/checkpoints.json"
if not os.path.exists(p):
    print("No checkpoints found.")
    exit(0)

try:
    with open(p, "r") as f:
        data = json.load(f)
    if not data:
        print("No checkpoints found.")
        exit(0)

    print(f"{'TAG':<36} {'COMMIT':<10} {'TYPE':<12} {'MESSAGE'}")
    print("-" * 80)
    for it in data:
        t = it.get('tag', '')
        c = it.get('commit', '')[:8]
        typ = it.get('type', 'manual')
        m = it.get('message', '')
        print(f"{t:<36} {c:<10} {typ:<12} {m}")
except Exception as e:
    print("Error reading checkpoints:", e)
EOF
}

# ---------------------------------------------------------------------------
# cmd_restore: restore codebase to chosen backup point
# ---------------------------------------------------------------------------
cmd_restore() {
    local target="$1"
    if [ -z "$target" ]; then
        # Find the latest backup tag
        target="$(python3 - << 'EOF'
import json, os
p = "backups/checkpoints.json"
if os.path.exists(p):
    try:
        with open(p, "r") as f:
            data = json.load(f)
        if data:
            print(data[-1].get("tag", ""))
    except: pass
EOF
)"
    fi

    if [ -z "$target" ]; then
        log_err "No backup target specified and no prior backups found!"
        exit 1
    fi

    log_warn "Requesting restore to backup checkpoint: '$target'..."
    
    # Save a safety rescue tag of current state before rolling back
    local rescue_tag="rescue_before_restore_$(date +"%Y%m%d_%H%M%S")"
    if [ -n "$(git status --porcelain)" ]; then
        git add -A
        git commit -m "chore(restore-guard): rescue commit before restoring $target" || true
    fi
    git tag "$rescue_tag" || true

    log_info "Checking out restore target: $target..."
    git checkout -f "$target"

    log_info "Rebuilding FirstOS kernel to verify restored state..."
    if make kernel.bin; then
        log_succ "FirstOS codebase successfully restored to '$target'!"
        log_succ "Kernel builds cleanly. System is ready."
    else
        log_err "Rebuild after restore encountered an error! Restoring safety rescue tag '$rescue_tag'..."
        git checkout -f "$rescue_tag"
        exit 1
    fi
}

# ---------------------------------------------------------------------------
# cmd_safe_update: apply update with automatic rollback on failure
# ---------------------------------------------------------------------------
cmd_safe_update() {
    local update_desc="${1:-system_update}"
    shift || true
    local update_cmd="$*"

    log_info "=== Starting FirstOS Safe Update Pipeline ==="
    log_info "Update Description: $update_desc"

    # Step 1: Create automatic pre-update checkpoint
    local timestamp="$(date +"%Y%m%d_%H%M%S")"
    local pre_tag="pre_update_${timestamp}"
    log_info "Step 1/4: Creating automated pre-update safety checkpoint '$pre_tag'..."

    if [ -n "$(git status --porcelain)" ]; then
        git add -A
        git commit -m "chore(safe-update): save working tree before update '$update_desc'"
    fi

    local pre_commit="$(git rev-parse HEAD)"
    git tag "$pre_tag"
    record_checkpoint "$pre_tag" "$pre_commit" "Pre-update checkpoint before '$update_desc'" "pre-update"

    # Step 2: Execute the update action if provided
    log_info "Step 2/4: Applying update changes..."
    if [ -n "$update_cmd" ]; then
        log_info "Executing update command: $update_cmd"
        if ! eval "$update_cmd"; then
            log_err "Update command execution failed! Initiating AUTOMATIC ROLLBACK..."
            git reset --hard "$pre_tag"
            make kernel.bin || true
            log_warn "System safely rolled back to pre-update checkpoint '$pre_tag'. No broken state was kept."
            exit 1
        fi
    fi

    # Step 3: Verify build and kernel integrity
    log_info "Step 3/4: Verifying build and kernel compilation..."
    if ! make kernel.bin; then
        log_err "--------------------------------------------------------"
        log_err "UPDATE FAILED: Kernel compilation produced errors!"
        log_err "TRIGGERING AUTOMATIC ROLLBACK to: $pre_tag ($pre_commit)"
        log_err "--------------------------------------------------------"
        git reset --hard "$pre_tag"
        make clean
        make kernel.bin
        log_succ "Rollback complete! Working tree restored to functional state ($pre_tag)."
        exit 1
    fi

    # Step 4: Verification passed -> commit & create post-update checkpoint
    log_info "Step 4/4: Verification PASSED! Committing update..."
    if [ -n "$(git status --porcelain)" ]; then
        git add -A
        git commit -m "feat(update): $update_desc"
    fi

    local post_commit="$(git rev-parse HEAD)"
    local post_tag="post_update_${timestamp}"
    git tag "$post_tag"
    record_checkpoint "$post_tag" "$post_commit" "Successfully applied: $update_desc" "post-update"

    log_succ "=========================================================="
    log_succ "Update '$update_desc' verified and committed successfully!"
    log_succ "Checkpoints: $pre_tag -> $post_tag (Commit: ${post_commit:0:8})"
    log_succ "=========================================================="
}

# ---------------------------------------------------------------------------
# CLI Dispatch
# ---------------------------------------------------------------------------
case "$1" in
    status)
        cmd_status
        ;;
    backup|snapshot)
        shift
        cmd_backup "$*"
        ;;
    list|history)
        cmd_list
        ;;
    restore|rollback)
        shift
        cmd_restore "$1"
        ;;
    safe-update|update)
        shift
        cmd_safe_update "$@"
        ;;
    help|--help|-h|"")
        echo "Usage: ./tools/firstos_git.sh <command> [arguments]"
        echo ""
        echo "Commands:"
        echo "  status                  Show git branch, tree status, and recent backups"
        echo "  backup [label]          Create a full snapshot backup point"
        echo "  list                    List all available backup restore points"
        echo "  restore [tag|id]        Restore codebase to a specific backup (def: latest)"
        echo "  safe-update <desc> [cmd] Apply update with automatic rollback on build failure"
        echo "  help                    Display this help message"
        ;;
    *)
        log_err "Unknown command: '$1'. Run './tools/firstos_git.sh help' for usage."
        exit 1
        ;;
esac
