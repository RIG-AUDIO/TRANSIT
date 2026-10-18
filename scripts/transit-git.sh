#!/usr/bin/env bash
# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only
# TRANSIT — guarded git wrapper.
#
# Runs the identity guard, refuses history-rewriting/force pushes, then
# executes real git with the remaining arguments. Use this instead of bare
# `git` for operations inside the TRANSIT repository.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/.." && pwd)"

bash "$here/verify-github-identity.sh"

# Refuse destructive push forms outright (no force-push policy).
if [ "${1:-}" = "push" ]; then
  shift
  for arg in "$@"; do
    case "$arg" in
      -f|--force|--force-with-lease|--force-if-includes)
        echo "transit-git: refusing force-push ('$arg')." >&2
        exit 1
        ;;
      +*|-*f*)
        echo "transit-git: refusing forced refspec or combined force flag ('$arg')." >&2
        exit 1
        ;;
    esac
  done
  exec git -C "$repo" push "$@"
fi

exec git -C "$repo" "$@"
