#!/usr/bin/env bash
set -euo pipefail
command -v wrk >/dev/null || { echo 'wrk is required'; exit 2; }
base_url="${BASE_URL:-http://127.0.0.1:9006}"
test_user="${TEST_USER:-test}"; test_password="${TEST_PASSWORD:-123456}"
duration="${DURATION:-15s}"; connections="${CONNECTIONS:-16}"; threads="${THREADS:-2}"
cookie=$(mktemp); trap 'rm -f "$cookie"' EXIT
curl -fsS -o /dev/null -c "$cookie" -X POST -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode "user=$test_user" --data-urlencode "password=$test_password" "$base_url/2CGISQL.cgi"
cookie_header=$(awk 'NF>=7{if($6=="sid"||$6=="csrf_token")printf "%s%s=%s",sep,$6,$7;sep="; "}' "$cookie")
curl -fsS -o /dev/null -H "Cookie: $cookie_header" "$base_url/api/community/feed?mode=for_you&limit=10"
wrk -t"$threads" -c"$connections" -d"$duration" --latency -H "Cookie: $cookie_header" \
  "$base_url/api/community/feed?mode=for_you&limit=10"
