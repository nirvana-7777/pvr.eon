#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/eon.har" >&2
  exit 1
fi

har_path=$1

if [[ ! -f "$har_path" ]]; then
  echo "HAR file not found: $har_path" >&2
  exit 1
fi

if ! command -v jq >/dev/null 2>&1; then
  echo "jq is required but not installed" >&2
  exit 1
fi

echo "HAR: $har_path"
echo

# ---------------------------------------------------------------------------
# Auth flow. Vivacom signs in through its own IdP (login.vivacom.bg, OAuth2
# authorization code + PKCE) while the ex-Yu providers use EON's oauth/token
# directly, so when a provider changes its login this section shows the new
# route at a glance. Only methods, hosts, paths and statuses are printed --
# never query strings or bodies, which carry codes, tokens and credentials.
# ---------------------------------------------------------------------------
echo "Auth flow"
jq -r '
  .log.entries[]
  | select(.request.url
      | test("oauth2?/(token|authorize)|/commonauth|authenticationendpoint/login\\.do|/v1/consent/token"))
  | [
      .startedDateTime,
      .request.method,
      .response.status,
      (.request.url | split("?")[0]),
      ((.request.url | (try (capture("grant_type=(?<g>[^&]+)").g) catch null))
        // ((.request.postData.text // "") | (try (capture("grant_type=(?<g>[^&]+)").g) catch null))
        // "")
    ]
  | @tsv
' "$har_path" | while IFS=$'	' read -r started method status url grant; do
  printf '  %s  %-5s status=%s  %s%s\n' "$started" "$method" "$status" "$url" "${grant:+  grant=$grant}"
done
echo

# ---------------------------------------------------------------------------
# Device registration. The v1 (POST) vs v2 (PATCH) split is exactly what the
# providers have been migrating across; one look here says which contract a
# provider's web player is on today.
# ---------------------------------------------------------------------------
echo "Devices endpoints"
jq -r '
  .log.entries[]
  | select(.request.url | test("/v[12]/devices"))
  | [.startedDateTime, .request.method, .response.status, (.request.url | split("?")[0])]
  | @tsv
' "$har_path" | while IFS=$'	' read -r started method status url; do
  printf '  %s  %-5s status=%s  %s\n' "$started" "$method" "$status" "$url"
done
echo

# ---------------------------------------------------------------------------
# Profiles. An account can hold several viewer profiles and a session token
# is bound to one of them (active_profile_id), which decides whose channel
# lists and preferences the API serves. This section shows how the web
# player moves a session onto a profile: every profile and token request
# with the NAMES of its query and body parameters (never their values) and
# the profile the answered token is bound to. Record it while picking a
# profile on the "who is watching" screen.
# ---------------------------------------------------------------------------
echo "Profile selection"
jq -r '
  def names: split("&") | map(split("=")[0]) | map(select(length > 0)) | unique | join(",");
  def body: if .encoding == "base64" then (.text // "" | try @base64d catch "") else (.text // "") end;
  .log.entries[]
  | select(.request.url | test("/v[0-9]+/profiles|/oauth/token|/household"))
  | (.request.url | split("?")) as $u
  | (.request.postData.text // "") as $req
  | (.response.content | body) as $resp
  | [
      .startedDateTime,
      .request.method,
      (.response.status | tostring),
      $u[0],
      (if ($u | length) > 1 then ($u[1] | names) else "" end),
      (if ((.request.postData.params // []) | length) > 0
       then (.request.postData.params | map(.name) | unique | join(","))
       elif ($req | test("Content-Disposition: form-data"))
       then ([$req | scan("name=\"([^\"]+)\"")[0]] | unique | join(","))
       elif ($req | length) == 0 or ($req | startswith("--")) then ""
       else (try ($req | fromjson | if type == "object" then (keys | join(",")) else type end)
             catch ($req | names)) end),
      (try ($resp | fromjson
            | if type == "object" and has("active_profile_id")
              then "active_profile_id=\(.active_profile_id) default=\(.active_profile_is_default)"
              else "" end)
       catch "")
    ]
  | join("\u001f")
' "$har_path" | while IFS=$'\x1f' read -r started method status url query reqbody bound; do
  # Unit separator rather than a tab: read collapses runs of tabs, which
  # would shift the empty fields here.
  printf '  %s  %-6s status=%s  %s\n' "$started" "$method" "$status" "$url"
  [[ -n "$query" ]] && printf '      query params: %s\n' "$query"
  [[ -n "$reqbody" ]] && printf '      body fields:  %s\n' "$reqbody"
  [[ -n "$bound" ]] && printf '      token bound:  %s\n' "$bound"
done

jq -r '
  def body: if .encoding == "base64" then (.text // "" | try @base64d catch "") else (.text // "") end;
  [ .log.entries[]
    | select((.request.url | split("?")[0] | test("/v[0-9]+/profiles$")) and .request.method == "GET")
    | (.response.content | body | try fromjson catch null)
    | select(type == "array") ]
  | last // []
  | .[]
  | "      profile id=\(.id) name=\(.profileName) default=\(.isDefault) pin=\(.pinProtected)"
' "$har_path"
echo

# ---------------------------------------------------------------------------
# Guide requests. Shows how the web player batches channels and sizes its
# window -- the reference the addon's own EPG prefetch is modelled on.
# ---------------------------------------------------------------------------
echo "EPG requests"
jq -r '
  .log.entries[]
  | select(.request.url | contains("/events/epg"))
  | ((.request.url | (try (capture("cid=(?<c>[^&]+)").c) catch null)) // "") as $cids
  | ((.request.url | (try (capture("fromTime=(?<f>[0-9]+)").f | tonumber) catch null)) // 0) as $from
  | ((.request.url | (try (capture("toTime=(?<t>[0-9]+)").t | tonumber) catch null)) // 0) as $to
  | [
      .startedDateTime,
      .response.status,
      (if $cids == "" then 0 else ($cids | split(",") | length) end),
      (if $from > 0 and $to > $from then (($to - $from) / 3600000 * 10 | round / 10) else 0 end)
    ]
  | @tsv
' "$har_path" | while IFS=$'	' read -r started status channels hours; do
  printf '  %s  status=%s  channels=%s  window=%sh\n' "$started" "$status" "$channels" "$hours"
done
echo

echo "Sessions"
jq -r '
  .log.entries[]
  | select(.request.url | contains("/stream"))
  | .request.url
  | select(test("session=[^&]+"))
  | capture("session=(?<session>[^&]+)").session
' "$har_path" \
  | sort -u \
  | sed '/^$/d' \
  | sed 's/^/  /'
echo

echo "Bootstrap requests"
jq -r '
  .log.entries[]
  | select(.request.url | contains("/stream?"))
  | [.startedDateTime, .response.status, (if (.request.url | contains("info=true")) then "info" else "bootstrap" end), .request.url]
  | @tsv
' "$har_path" | while IFS=$'\t' read -r started status kind url; do
  printf '  %s  status=%s  kind=%s\n' "$started" "$status" "$kind"
done
echo

echo "Info responses"
jq -r '
  .log.entries[]
  | select(.request.url | contains("info=true"))
  | (.response.content.text // "" | try fromjson catch {}) as $body
  | [
      .startedDateTime,
      .response.status,
      ($body.location // ""),
      ($body.machine // ""),
      ($body.source // ""),
      ($body.target // "")
    ]
  | @tsv
' "$har_path" | while IFS=$'\t' read -r started status location machine source target; do
  printf '  %s  status=%s  location=%s  machine=%s  source=%s  target=%s\n' \
    "$started" "$status" "${location:-?}" "${machine:-?}" "${source:-?}" "${target:-?}"
done
echo

playlist_rows=$(
  jq -r '
    .log.entries[]
    | select(.request.url | contains("/stream/playlist/hls/"))
    | (.response.content.text // "") as $text
    | select($text | contains("MEDIA-SEQUENCE:"))
    | [
        .startedDateTime,
        (.request.url | capture("stream=(?<stream>[^&]+)").stream),
        ($text | capture("MEDIA-SEQUENCE:(?<seq>[0-9]+)").seq),
        ([ $text | match("offset=([0-9]+)&pointeroffset=([0-9]+)"; "g") | .captures[0].string ] | first),
        ([ $text | match("offset=([0-9]+)&pointeroffset=([0-9]+)"; "g") | .captures[0].string ] | last),
        ([ $text | match("offset=([0-9]+)&pointeroffset=([0-9]+)"; "g") | .captures[1].string ] | first),
        ([ $text | match("offset=([0-9]+)&pointeroffset=([0-9]+)"; "g") | .captures[1].string ] | last),
        ($text | capture("TARGETDURATION:(?<target>[0-9]+)").target),
        ([ $text | match("#EXTINF:(?<dur>[0-9.]+),"; "g") | .captures[0].string ] | first)
      ]
    | @tsv
  ' "$har_path"
)

if [[ -z "$playlist_rows" ]]; then
  echo "Playlist polling"
  echo "  No /stream/playlist/hls/ entries found"
  exit 0
fi

echo "Playlist polling"
printf '%s\n' "$playlist_rows" | while IFS=$'\t' read -r started stream seq first_off last_off first_ptr last_ptr target segment; do
  printf '  %s  stream=%s  seq=%s  offsets=%s..%s  pointers=%s..%s  target=%ss  segment=%ss\n' \
    "$started" "$stream" "$seq" "$first_off" "$last_off" "$first_ptr" "$last_ptr" "$target" "$segment"
done
echo

playlist_count=$(printf '%s\n' "$playlist_rows" | wc -l | tr -d ' ')
first_playlist=$(printf '%s\n' "$playlist_rows" | head -n 1)
last_playlist=$(printf '%s\n' "$playlist_rows" | tail -n 1)

echo "Playlist summary"
echo "  playlist_count=$playlist_count"
printf '%s\n' "$first_playlist" | awk -F '\t' '{printf "  first: %s stream=%s seq=%s offsets=%s..%s\n", $1, $2, $3, $4, $5}'
printf '%s\n' "$last_playlist" | awk -F '\t' '{printf "  last:  %s stream=%s seq=%s offsets=%s..%s\n", $1, $2, $3, $4, $5}'
echo

echo "Detected jumps"
printf '%s\n' "$playlist_rows" | awk -F '\t' '
  NR == 1 {
    prev_started = $1
    prev_stream = $2
    prev_seq = $3 + 0
    prev_first = $4 + 0
    prev_last = $5 + 0
    jumps = 0
    next
  }
  {
    stream = $2
    seq = $3 + 0
    first = $4 + 0
    last = $5 + 0
    seq_delta = seq - prev_seq
    offset_delta = first - prev_last
    if (stream != prev_stream) {
      prev_started = $1
      prev_stream = stream
      prev_seq = seq
      prev_first = first
      prev_last = last
      next
    }
    if (seq_delta != 1 || (offset_delta != -1 && offset_delta != 0 && offset_delta != 1)) {
      printf "  %s  stream=%s seq=%d prev_seq=%d  first_offset=%d prev_last_offset=%d  seq_delta=%d offset_delta=%d\n",
        $1, stream, seq, prev_seq, first, prev_last, seq_delta, offset_delta
      jumps++
    }
    prev_started = $1
    prev_stream = stream
    prev_seq = seq
    prev_first = first
    prev_last = last
  }
  END {
    if (jumps == 0) {
      print "  none"
    }
  }
'
echo

echo "Interpretation"
printf '%s\n' "$playlist_rows" | awk -F '\t' '
  NR == 1 {
    prev_stream = $2
    prev_seq = $3 + 0
    prev_last = $5 + 0
    total = 1
    jumps = 0
    next
  }
  {
    stream = $2
    seq = $3 + 0
    first = $4 + 0
    if (stream != prev_stream) {
      prev_stream = stream
      prev_seq = seq
      prev_last = $5 + 0
      total++
      next
    }
    seq_delta = seq - prev_seq
    offset_delta = first - prev_last
    if (seq_delta != 1 || (offset_delta != -1 && offset_delta != 0 && offset_delta != 1)) {
      jumps++
    }
    prev_stream = stream
    prev_seq = seq
    prev_last = $5 + 0
    total++
  }
  END {
    if (jumps == 0) {
      print "  Continuous rolling playlist. This HAR did not capture a seek/restart transition."
    } else {
      printf "  Found %d discontinuity event(s). This HAR likely includes a seek or session restart.\n", jumps
    }
  }
'
