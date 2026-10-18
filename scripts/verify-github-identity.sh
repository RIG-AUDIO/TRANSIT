#!/usr/bin/env bash
# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only
# TRANSIT — GitHub identity guard (read-only, prints no secrets).
#
# Verifies that this machine's effective GitHub identity and repository
# remote match the publication contract: RIG-AUDIO / RIG-AUDIO/TRANSIT.
# Safe to run repeatedly; prints only account names and URLs, never tokens.
set -euo pipefail

EXPECT_OWNER="RIG-AUDIO"
EXPECT_REPO="https://github.com/RIG-AUDIO/TRANSIT"

fail=0
err() { echo "IDENTITY GUARD: $*" >&2; fail=1; }

# 1. No ambient token overrides (CI sets these by design; skip there).
if [ -z "${GITHUB_ACTIONS:-}" ]; then
  for v in GH_TOKEN GITHUB_TOKEN; do
    eval "val=\${$v:-}"
    if [ -n "$val" ]; then
      err "$v is set in the environment; it overrides the configured account. Unset it (value not shown)."
    fi
  done
fi

# 2. gh CLI identity.
if ! command -v gh >/dev/null 2>&1; then
  err "gh CLI not found; cannot verify GitHub identity."
else
  login="$(gh api user -q .login 2>/dev/null || true)"
  if [ -z "$login" ]; then
    err "gh is not authenticated (gh api user failed)."
  elif [ "$login" != "$EXPECT_OWNER" ]; then
    err "authenticated GitHub account is '$login', expected '$EXPECT_OWNER'."
  else
    echo "IDENTITY GUARD: gh account = $login (ok)"
  fi
fi

# 3. git credential helper resolves to the expected username for github.com.
#    Only the username line is ever surfaced; the password is discarded.
if command -v git >/dev/null 2>&1; then
  helper="$(git config --get-all credential.helper 2>/dev/null || true)"
  if [ -n "$helper" ] && [ "$helper" != "auth git-credential" ]; then
    err "credential.helper is '$helper', expected 'auth git-credential' (gh)."
  fi
  if [ -z "${GITHUB_ACTIONS:-}" ] && command -v gh >/dev/null 2>&1; then
    cred_user="$(printf 'protocol=https\nhost=github.com\n\n' \
      | git credential fill 2>/dev/null \
      | awk -F= '/^username=/{print $2; exit}')"
    if [ -n "$cred_user" ] && [ "$cred_user" != "$EXPECT_OWNER" ]; then
      err "git credential username is '$cred_user', expected '$EXPECT_OWNER'."
    elif [ -n "$cred_user" ]; then
      echo "IDENTITY GUARD: git credential username = $cred_user (ok)"
    fi
  fi
fi

# 4. Repository remote, when run inside a worktree.
if git rev-parse --git-dir >/dev/null 2>&1; then
  origin="$(git remote get-url origin 2>/dev/null || true)"
  if [ -z "$origin" ]; then
    err "no 'origin' remote configured."
  elif [ "$origin" != "$EXPECT_REPO" ]; then
    err "origin is '$origin', expected '$EXPECT_REPO'."
  else
    echo "IDENTITY GUARD: origin = $origin (ok)"
  fi
fi

if [ "$fail" -ne 0 ]; then
  echo "IDENTITY GUARD: FAILED" >&2
  exit 1
fi
echo "IDENTITY GUARD: OK"
