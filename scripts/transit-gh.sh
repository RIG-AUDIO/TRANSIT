#!/usr/bin/env bash
# TRANSIT — DJ Software / SPDX-License-Identifier: GPL-3.0-only
# TRANSIT — guarded GitHub CLI wrapper.
#
# Runs the identity guard, refuses account-switching/auth commands, then
# executes real gh with the remaining arguments.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"

bash "$here/verify-github-identity.sh"

case "${1:-}" in
  auth)
    # auth status (read-only) is fine; everything else mutates identity.
    if [ "${2:-}" != "status" ]; then
      echo "transit-gh: refusing 'gh auth ${2:-<none>}' (only 'auth status' allowed)." >&2
      exit 1
    fi
    ;;
esac

exec gh "$@"
