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
