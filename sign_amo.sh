#!/usr/bin/env sh
# XVidGrab - AMO signer (makes the extension PERMANENTLY installable
# on any release Firefox).
#
# One-time setup (free, ~2 minutes):
#   1. Create a free account at https://addons.mozilla.org/developers/
#   2. Generate API keys at:
#      https://addons.mozilla.org/developers/addon/api/key/
#   3. export AMO_KEY=<jwt issuer>; export AMO_SECRET=<jwt secret>
#   4. ./sign_amo.sh
#
# The signed .xpi lands in ./dist - install it via about:addons ->
# gear icon -> "Install Add-on From File...".
set -e
: "${AMO_KEY:?Set AMO_KEY (AMO JWT issuer) first}"
: "${AMO_SECRET:?Set AMO_SECRET (AMO JWT secret) first}"
cd "$(dirname "$0")"
npx --yes web-ext sign \
  --api-key "$AMO_KEY" \
  --api-secret "$AMO_SECRET" \
  --channel unlisted \
  --source-dir . \
  --artifacts-dir dist
echo
echo "[OK] Signed XVidGrab .xpi is in ./dist"
echo "     Install: about:addons -> gear icon -> Install Add-on From File..."
