#!/bin/bash
set -euo pipefail

if [[ -f .env ]]; then
    # .env is local-only and ignored by Git.
    set -a
    source .env
    set +a
fi

: "${MYSQL_USER:?Set MYSQL_USER in the environment or .env}"
: "${MYSQL_PASSWORD:?Set MYSQL_PASSWORD in the environment or .env}"
: "${MYSQL_DATABASE:?Set MYSQL_DATABASE in the environment or .env}"

./server -p 9006 "$@"
