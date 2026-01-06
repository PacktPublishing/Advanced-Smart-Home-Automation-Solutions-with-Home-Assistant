#!/usr/bin/env bash
set -Eeu

OWNER="${1:-}"; REPO="${2:-}"; REF="${3:-main}"; FILEPATH="${4:-}"; DEST="${5:-}"
: "${OWNER:?missing OWNER}"; : "${REPO:?missing REPO}"; : "${FILEPATH:?missing PATH}"; : "${DEST:?missing DEST}"

TOKEN_FILE="/config/.secrets/github_token"
TOKEN="$(cat "$TOKEN_FILE")"

URL="https://api.github.com/repos/${OWNER}/${REPO}/contents/${FILEPATH}?ref=${REF}"
curl --fail -sSL \
    -H "Authorization: Bearer ${TOKEN}" \
    -H "Accept: application/vnd.github.v3.raw" \
    "$URL" -o "$DEST"

echo "Saved ${OWNER}/${REPO}@${REF}:${FILEPATH} -> ${DEST}"
