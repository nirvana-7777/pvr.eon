#!/usr/bin/env bash
#
# check-eon.sh -- live diagnostics against the EON backend, for comparing what
# the backend serves with what pvr.eon / plugin.video.eonvod show.
#
#   ./check-eon.sh broker                    broker state: brands, CDNs, geolocation
#   ./check-eon.sh api      [settings.xml]   account state: sp, clock, channels, categories
#   ./check-eon.sh vod      [settings.xml]   VOD state: catalogues, home page stripes
#   ./check-eon.sh devices  [settings.xml]   devices registered on the account
#   ./check-eon.sh epg      [settings.xml]   probe the EPG limits the addon assumes
#   ./check-eon.sh all      [settings.xml]   everything above
#
# `broker` needs no account. The other commands need the user access token:
#   1. a path to pvr.eon's settings.xml (LibreELEC:
#      /storage/.kodi/userdata/addon_data/pvr.eon/settings.xml), or
#   2. EON_ACCESS_TOKEN set in the environment.
# The stored REFRESH token is deliberately never used: spending it here would
# rotate it and desync the copy Kodi holds. When the access token has expired,
# open Live TV in Kodi once and re-copy settings.xml.
#
# Select the provider with --brand (sbb-qa, telemach-ba, telemach-hr,
# telemach-me, telemach-si, nova, vivacom); with a settings.xml it is read
# from the stored provider setting instead.
#
# EON_MOCK_DIR=/dir runs everything against saved responses (<name>.json)
# instead of the network -- for tests and for analyzing someone else's dump.

set -euo pipefail

BROKER="https://broker.global.united.cloud"
# The EON web client's public id:secret, as shipped in the web app's page
# traffic (same pair the addon's Globals.h carries).
BROKER_BASIC="YjhkOWFkZTQtMTA5My00NmE3LWE0ZjctMGU0N2JlNDYzYzEwOjF3NGRtd3c4N3gxZTlsODllc3NxdmM4MXBpZHJxc2EwbGkxcnZhMjM="

# What the addon expects (ExpectedBrandIdentifier + GetBaseApi): identifier=cdn
EXPECTED_BRANDS="sbb-qa=ug telemach-ba=ug telemach-hr=ug telemach-me=ug telemach-si=ug nova=forthnet vivacom=vivacom"
# provider setting index -> identifier (settings.xml stores the index)
PROVIDER_INDEX=(sbb-qa telemach-ba telemach-hr telemach-si vivacom nova telemach-me)

# VOD portal page ids, per CDN, mirroring plugin.video.eonvod's maps: the
# ex-Yu portal (ug) has six pages and keeps documentaries on 10007 with
# tvshows on 10006; Vivacom and Nova have five, with documentaries on 10006.
# Themed pages (10036+) exist too -- inspect any of them with --page <id>.
VOD_PAGES_UG="home=10001 movies=10003 series=10004 kids=10005 tvshows=10006 documentaries=10007"
VOD_PAGES_PORTAL="home=10001 movies=10003 series=10004 kids=10005 documentaries=10006"

command -v jq >/dev/null 2>&1 || { echo "jq is required but not installed" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "curl is required but not installed" >&2; exit 1; }

cmd=${1:-}
shift || true

# No arguments on an interactive terminal -> a small menu; in a pipe or cron
# the usage text is printed instead, so automation never blocks on a prompt.
if [[ -z "$cmd" ]]; then
  if [[ -t 0 || -n "${EON_FORCE_MENU:-}" ]]; then
    echo "check-eon.sh -- EON backend diagnostics"
    echo
    echo "  1) broker   - brands, CDNs, geolocation (no account needed)"
    echo "  2) api      - service provider, clock, channels, categories"
    echo "  3) vod      - catalogues and portal pages with stripes"
    echo "  4) devices  - devices registered on the account"
    echo "  5) epg      - probe EPG limits the addon assumes (batch size, archive depth)"
    echo "  6) all      - everything above"
    echo "  q) quit"
    echo
    read -r -p "Choice: " choice
    case "$choice" in
      1) cmd=broker ;;
      2) cmd=api ;;
      3) cmd=vod ;;
      4) cmd=devices ;;
      5) cmd=epg ;;
      6) cmd=all ;;
      *) exit 0 ;;
    esac
    if [[ "$cmd" != "broker" && -z "${EON_ACCESS_TOKEN:-}" ]]; then
      default_settings=""
      [[ -f ./settings.xml ]] && default_settings="./settings.xml"
      read -r -p "Path to pvr.eon settings.xml${default_settings:+ [$default_settings]}: " settings_in
      settings_menu="${settings_in:-$default_settings}"
    fi
    echo
  else
    grep '^#   ' "$0" | sed 's/^#  //'
    exit 1
  fi
fi

brand=""
settings=""
extra_pages=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --brand) brand=$2; shift 2 ;;
    --page) extra_pages="$extra_pages ${2//,/ }"; shift 2 ;;
    *) settings=$1; shift ;;
  esac
done
[[ -z "$settings" && -n "${settings_menu:-}" ]] && settings=$settings_menu

# ---------------------------------------------------------------------------
# helpers
# ---------------------------------------------------------------------------

# fetch <name> <url> [curl args...]: GET with mock support; body on stdout,
# fails the command (not the script) on HTTP errors so sections degrade alone.
fetch() {
  local name=$1 url=$2; shift 2
  if [[ -n "${EON_MOCK_DIR:-}" ]]; then
    cat "$EON_MOCK_DIR/$name.json" 2>/dev/null || { echo "  (no mock for $name)" >&2; return 1; }
    return 0
  fi
  local out status
  out=$(curl -sS -w $'\n%{http_code}' "$url" "$@") || return 1
  status=${out##*$'\n'}
  if [[ "$status" != 2* ]]; then
    echo "  HTTP $status from ${url%%\?*}" >&2
    return 1
  fi
  printf '%s' "${out%$'\n'*}"
}

# probe <name> <url>: HTTP status only -- for requests whose FAILURE is the
# information (limit probes). Mocked by <name>.status files.
probe() {
  local name=$1 url=$2
  if [[ -n "${EON_MOCK_DIR:-}" ]]; then
    cat "$EON_MOCK_DIR/$name.status" 2>/dev/null || echo 200
    return 0
  fi
  curl -sS -o /dev/null -w '%{http_code}' "$url" \
    -H "Authorization: bearer $ACCESS_TOKEN" -H "User-Agent: Mozilla/5.0" \
    -H "Content-Type: application/json" -H "x-ucp-time-format: timestamp"
}

setting_value() { # setting_value <id> <file>
  sed -n "s/.*<setting id=\"$1\"[^>]*>\\([^<]*\\)<\\/setting>.*/\\1/p" "$2" | head -n 1
}

jwt_claims() { # decode a JWT payload to JSON on stdout (empty on failure)
  local payload
  payload=$(printf '%s' "$1" | cut -d. -f2 | tr '_-' '/+')
  while (( ${#payload} % 4 )); do payload+="="; done
  printf '%s' "$payload" | base64 -d 2>/dev/null || true
}

generic_token() {
  [[ -n "${EON_MOCK_DIR:-}" ]] && { echo "mock-token"; return; }
  curl -sS -X POST "$BROKER/oauth/token?grant_type=client_credentials" \
    -H "Authorization: Basic $BROKER_BASIC" -H "Content-Type: application/json" -d "{}" \
    | jq -r '.access_token // empty'
}

# ---------------------------------------------------------------------------
# account token + API host resolution
# ---------------------------------------------------------------------------
ACCESS_TOKEN="${EON_ACCESS_TOKEN:-}"
if [[ -n "$settings" ]]; then
  [[ -f "$settings" ]] || { echo "settings.xml not found: $settings" >&2; exit 1; }
  [[ -z "$ACCESS_TOKEN" ]] && ACCESS_TOKEN=$(setting_value accesstoken "$settings")
  if [[ -z "$brand" ]]; then
    idx=$(setting_value provider "$settings"); idx=${idx:-0}
    brand=${PROVIDER_INDEX[$idx]:-sbb-qa}
  fi
fi
brand=${brand:-sbb-qa}

api_host_for_brand() { # resolve api-web host via broker (brands -> cdninfo)
  local token cdn
  token=$(generic_token)
  cdn=$(fetch brands "$BROKER/v2/brands" -H "Authorization: bearer $token" \
        | jq -r --arg b "$brand" '.[] | select(.identifier == $b) | .cdnIdentifier' ) || return 1
  [[ -z "$cdn" ]] && { echo "brand '$brand' not in broker list" >&2; return 1; }
  fetch cdninfo "$BROKER/v1/cdninfo" -H "Authorization: bearer $token" \
    | jq -r --arg c "$cdn" '.[] | select(.identifier == $c) | .domains.baseApi.be'
}

require_token() {
  if [[ -z "$ACCESS_TOKEN" && -z "${EON_MOCK_DIR:-}" ]]; then
    echo "This command needs the user access token: pass pvr.eon's settings.xml" >&2
    echo "or set EON_ACCESS_TOKEN. The refresh token is never used -- if the" >&2
    echo "access token expired, open Live TV in Kodi once and re-copy the file." >&2
    exit 1
  fi
  if [[ -n "$ACCESS_TOKEN" ]]; then
    local claims exp now
    claims=$(jwt_claims "$ACCESS_TOKEN")
    exp=$(printf '%s' "$claims" | jq -r '.exp // 0' 2>/dev/null || echo 0)
    now=$(date +%s)
    if [[ "$exp" -gt 0 && "$exp" -lt "$now" ]]; then
      echo "Access token EXPIRED at $(date -d "@$exp" 2>/dev/null || echo "$exp")." >&2
      echo "Open Live TV in Kodi once (it refreshes and stores a new one)," >&2
      echo "then re-copy settings.xml. Not using the refresh token on purpose:" >&2
      echo "spending it here would rotate it away from the copy Kodi holds." >&2
      exit 1
    fi
    echo "Account: sp_id=$(printf '%s' "$claims" | jq -r '.sp_id // "?"') user_id=$(printf '%s' "$claims" | jq -r '.user_id // "?"') token expires $(date -d "@$exp" 2>/dev/null || echo "$exp")"
  fi
}

api_get() { # api_get <name> <path-with-query>
  local name=$1 path=$2
  fetch "$name" "https://${API_HOST}/${path}" \
    -H "Authorization: bearer $ACCESS_TOKEN" \
    -H "User-Agent: Mozilla/5.0" -H "Content-Type: application/json" \
    -H "x-ucp-time-format: timestamp"
}

# ---------------------------------------------------------------------------
# sections
# ---------------------------------------------------------------------------
run_broker() {
  local token
  token=$(generic_token)
  [[ -z "$token" ]] && { echo "Could not obtain a broker token" >&2; return 1; }

  echo "=== Broker: geolocation ==="
  fetch geolocation "$BROKER/v1/geolocation" -H "Authorization: bearer $token" \
    | jq -r '"  country=\(.countryCode) isp=\(.isp // "?") vpn=\(.isVpn) proxy=\(.isProxy)"' || true
  echo

  echo "=== Broker: brands vs addon expectations ==="
  local brands
  brands=$(fetch brands "$BROKER/v2/brands" -H "Authorization: bearer $token") || return 1
  local ok=1 pair id cdn have
  for pair in $EXPECTED_BRANDS; do
    id=${pair%%=*}; cdn=${pair##*=}
    have=$(printf '%s' "$brands" | jq -r --arg b "$id" '.[] | select(.identifier == $b) | .cdnIdentifier')
    if [[ -z "$have" ]]; then
      echo "  MISSING from broker: $id (addon expects it on cdn '$cdn')"; ok=0
    elif [[ "$have" != "$cdn" ]]; then
      echo "  CDN CHANGED: $id is now on '$have', addon expects '$cdn'"; ok=0
    fi
  done
  printf '%s' "$brands" | jq -r '.[].identifier' | while read -r id; do
    case " $EXPECTED_BRANDS " in
      *" $id="*) : ;;
      *) echo "  NEW brand not in the addon: $id ($(printf '%s' "$brands" | jq -r --arg b "$id" '.[] | select(.identifier==$b) | .name + " -> " + .cdnIdentifier'))" ;;
    esac
  done
  [[ $ok -eq 1 ]] && echo "  All expected brands present with expected CDNs."
  echo

  echo "=== Broker: CDN API hosts ==="
  fetch cdninfo "$BROKER/v1/cdninfo" -H "Authorization: bearer $token" \
    | jq -r '.[] | "  \(.identifier)\(if .isDefault then " (default)" else "" end): api=\(.domains.baseApi.be // "?")"' || true
  echo
}

run_api() {
  echo "=== API host for brand '$brand' ==="
  API_HOST=${EON_API_HOST:-$(api_host_for_brand)}
  API_HOST="api-web.${API_HOST}"
  echo "  https://$API_HOST"
  echo

  echo "=== Service provider (v1/sp) ==="
  api_get sp "v1/sp" | jq -r '"  identifier=\(.identifier // "?") support=\(.supportWebAddress // "?")"' || true
  echo

  echo "=== Backend clock (v1/time) ==="
  local t now
  t=$(api_get time "v1/time" | jq -r '.time // 0')
  now=$(($(date +%s%N) / 1000000))
  [[ "$t" != 0 ]] && echo "  backend is $((t - now)) ms ahead of this machine" || echo "  unavailable"
  echo

  local type
  for type in TV RADIO; do
    echo "=== Channels ($type) ==="
    api_get "channels_$type" "v3/channels?channelType=$type" | jq -r '
      "  total=\(length)  subscribed=\([.[] | select(.subscribed)] | length)  cutvEnabled=\([.[] | select(.cutvEnabled)] | length)"' || true
    echo "=== Categories ($type) ==="
    api_get "categories_$type" "v2/categories/$type" | jq -r '
      .[] | "  [\(.order // 0)] \(.name): \(.channels | length) channels"' || true
    echo
  done
}

run_vod() {
  if [[ -z "${API_HOST:-}" ]]; then
    API_HOST=${EON_API_HOST:-$(api_host_for_brand)}
    API_HOST="api-web.${API_HOST}"
    echo "API host: https://$API_HOST"
    echo
  fi

  echo "=== VOD catalogues (v1/catalogues) ==="
  api_get catalogues "v1/catalogues" | jq -r '.[] | "  id=\(.id) \(.name // "?")"' || true
  echo

  # Page set matches the plugin's map for this brand's CDN.
  local pages
  case "$brand" in
    vivacom|nova) pages=$VOD_PAGES_PORTAL ;;
    *)            pages=$VOD_PAGES_UG ;;
  esac
  local p
  for p in $extra_pages; do
    pages="$pages page$p=$p"
  done

  local pair name pid stripes ids
  for pair in $pages; do
    name=${pair%%=*}; pid=${pair##*=}
    echo "=== VOD page '$name' (ps/v2/pages/$pid) ==="
    stripes=$(api_get "page_$pid" "ps/v2/pages/$pid") || { echo; continue; }
    printf '%s' "$stripes" | jq -r '
      "  title=\(.title // "?") stripes=\(.stripes | length)",
      (.stripes[] | "    stripe id=\(.id // "?") \(.title // .name // "?")")' || true
    # item counts for the first few stripes, same batch endpoint the plugin uses
    ids=$(printf '%s' "$stripes" | jq -r '[.stripes[].id | tostring] | .[0:4] | join(",")' 2>/dev/null || true)
    if [[ -n "$ids" && "$ids" != "null" ]]; then
      api_get "stripes_$pid" "ps/v2/stripes?id=$ids" | jq -r '
        (if type == "array" then .[] else .[]? end)
        | "    items: stripe \(.id // "?") -> \((.items // .assets // []) | length)"' 2>/dev/null || true
    fi
    echo
  done
}

run_devices() {
  if [[ -z "${API_HOST:-}" ]]; then
    API_HOST=${EON_API_HOST:-$(api_host_for_brand)}
    API_HOST="api-web.${API_HOST}"
  fi
  echo "=== Devices on the account (v1/devices) ==="
  # Listing may not be allowed on every provider; a failure here only means
  # the endpoint refused, not that the script is broken.
  api_get devices "v1/devices" | jq -r '
    if type == "array" then
      "  count=\(length)",
      (.[] | "  id=\(.deviceId // .id // "?")  \(.deviceName // .name // "?")  model=\(.modelName // "?")  number=\((.deviceNumber // "?") | tostring | .[0:8] + "...")")
    else . end' || echo "  (listing not available on this provider)"
  echo
}

run_epg() {
  if [[ -z "${API_HOST:-}" ]]; then
    API_HOST=${EON_API_HOST:-$(api_host_for_brand)}
    API_HOST="api-web.${API_HOST}"
  fi
  echo "=== EPG limit probes (what pvr.eon hardcodes vs what the backend enforces) ==="

  local ids15 ids16 now from to status
  ids15=$(api_get channels_TV "v3/channels?channelType=TV" \
          | jq -r '[.[].id] | .[0:16] | map(tostring) | join(",")') || { echo "  cannot list channels"; return; }
  [[ -z "$ids15" ]] && { echo "  no channel ids"; return; }
  ids16=$ids15
  ids15=$(printf '%s' "$ids15" | cut -d, -f1-15)
  now=$(date +%s)

  # Batch size: the addon sends at most 15 channel ids per request
  # (EPG_CHANNELS_PER_REQUEST) because 16 used to be refused.
  from="${now}000"; to="$((now + 6 * 3600))000"
  status=$(probe epg_batch15 "https://${API_HOST}/v1/events/epg?cid=${ids15}&fromTime=${from}&toTime=${to}")
  echo "  batch of 15 channels: HTTP $status $([[ $status == 200 ]] && echo '(as the addon assumes)' || echo '(PROBLEM: the addon batches 15)')"
  status=$(probe epg_batch16 "https://${API_HOST}/v1/events/epg?cid=${ids16}&fromTime=${from}&toTime=${to}")
  echo "  batch of 16 channels: HTTP $status $([[ $status == 200 ]] && echo '(limit RAISED -- addon could batch more)' || echo '(still refused, 15 remains the cap)')"

  # Archive depth: the addon clamps requests to 7 days back
  # (EPG_ARCHIVE_SECONDS) because older ranges used to answer 400.
  local cid1=${ids15%%,*}
  from="$((now - 8 * 86400))000"; to="$((now - 7 * 86400 - 43200))000"
  status=$(probe epg_archive8 "https://${API_HOST}/v1/events/epg?cid=${cid1}&fromTime=${from}&toTime=${to}")
  echo "  window 8 days back:   HTTP $status $([[ $status == 200 ]] && echo '(archive DEEPENED -- clamp could be relaxed)' || echo '(still refused, 7-day clamp remains right)')"
  from="$((now - 6 * 86400))000"; to="$((now - 5 * 86400))000"
  status=$(probe epg_archive6 "https://${API_HOST}/v1/events/epg?cid=${cid1}&fromTime=${from}&toTime=${to}")
  echo "  window 6 days back:   HTTP $status $([[ $status == 200 ]] && echo '(as the addon assumes)' || echo '(PROBLEM: archive is SHALLOWER than the 7-day clamp)')"
  echo
}

case "$cmd" in
  broker)  run_broker ;;
  api)     require_token; run_api ;;
  vod)     require_token; run_vod ;;
  devices) require_token; run_devices ;;
  epg)     require_token; run_epg ;;
  all)     run_broker; require_token; run_api; run_vod; run_devices; run_epg ;;
  *) echo "Unknown command: $cmd" >&2; exit 1 ;;
esac
