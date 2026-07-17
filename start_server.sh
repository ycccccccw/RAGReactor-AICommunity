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
: "${BAILIAN_API_KEY:?Set BAILIAN_API_KEY in .env}"
: "${BAILIAN_BASE_URL:?Set BAILIAN_BASE_URL in .env}"

./server -p 9006 "$@"
