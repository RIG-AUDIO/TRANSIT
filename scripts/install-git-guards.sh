#!/usr/bin/env bash
# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only
# TRANSIT — install repo-local identity guards.
#
# Idempotent. Sets, in this repository only (never global config):
#   - hooks path -> .githooks (pre-push guard)
#   - commit identity -> RIG-AUDIO no-reply identity
#   - credential helper -> gh
#   - push safety defaults
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

command -v git >/dev/null 2>&1 || { echo "git not found" >&2; exit 1; }

git -C "$repo" config core.hooksPath .githooks
chmod +x "$repo"/.githooks/* 2>/dev/null || true

# Commit identity: GitHub no-reply address, no personal email invented.
git -C "$repo" config user.name "RIG-AUDIO"
git -C "$repo" config user.email "RIG-AUDIO@users.noreply.github.com"

# Credentials: gh-managed, repo-local.
git -C "$repo" config credential.helper "auth git-credential"

# Safety defaults (no force, no auto-tagging surprises).
git -C "$repo" config push.default simple
git -C "$repo" config push.autoSetupRemote true

echo "TRANSIT git guards installed (repo-local):"
git -C "$repo" config --local --list | grep -E '^(core\.hooksPath|user\.|credential\.helper|push\.)' \
  | sed 's/^/  /'
