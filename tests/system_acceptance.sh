#!/usr/bin/env bash
set -euo pipefail

base_url="${BASE_URL:-http://127.0.0.1:9006}"
test_user="${TEST_USER:-test}"
test_password="${TEST_PASSWORD:-123456}"
cookie_file="$(mktemp)"
trap 'rm -f "$cookie_file"' EXIT

status() { curl -sS -o "$2" -w '%{http_code}' "${@:3}"; }
expect() { [[ "$1" == "$2" ]] || { echo "FAIL: expected $2, got $1 ($3)"; exit 1; }; }

health_file="$(mktemp)"; trap 'rm -f "$cookie_file" "$health_file"' EXIT
code=$(status health "$health_file" "$base_url/api/health")
expect "$code" 200 health
jq -e '.status=="ok" and .rag_configured==true and .index_ready==true' "$health_file" >/dev/null
code=$(curl -sS -o /dev/null -w '%{http_code}' "$base_url/log.html?from=acceptance")
expect "$code" 200 static_query_string

code=$(curl -sS -o /dev/null -w '%{http_code}' "$base_url/api/community/feed?mode=for_you&limit=10")
expect "$code" 401 unauthenticated_feed
code=$(curl -sS -o /dev/null -w '%{http_code}' -c "$cookie_file" -X POST \
  -H 'Content-Type: application/x-www-form-urlencoded' \
  --data-urlencode "user=$test_user" --data-urlencode "password=$test_password" \
  "$base_url/2CGISQL.cgi")
expect "$code" 200 login
cookie_header=$(awk 'NF>=7{if($6=="sid"||$6=="csrf_token")printf "%s%s=%s",sep,$6,$7;sep="; "}' "$cookie_file")
csrf=$(awk 'NF>=7 && $6=="csrf_token"{print $7}' "$cookie_file")

feed_file=$(mktemp)
code=$(curl -sS -o "$feed_file" -w '%{http_code}' -H "Cookie: $cookie_header" \
  "$base_url/api/community/feed?mode=for_you&limit=10")
expect "$code" 200 feed
jq -e '.items|length>0' "$feed_file" >/dev/null
rm -f "$feed_file"

code=$(curl -sS -o /dev/null -w '%{http_code}' -H "Cookie: $cookie_header" \
  -H 'Content-Type: application/json' --data '{"event_id":"acceptance-event-0001","post_id":"1","action":"like"}' \
  "$base_url/api/community/action")
expect "$code" 403 action_without_csrf
code=$(curl -sS -o /dev/null -w '%{http_code}' -H "Cookie: $cookie_header" \
  "$base_url/api/community/feed?mode=bad&limit=10")
expect "$code" 400 invalid_feed_mode

printf 'PASS: health, auth, feed, CSRF and validation checks\n'
printf 'CSRF token available: %s\n' "${csrf:+yes}"
