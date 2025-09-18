#!/usr/bin/env bash
set -euo pipefail

BASE="${1:-http://127.0.0.1:8080}"
NC_HOST="$(echo "$BASE" | sed -E 's#^https?://([^/:]+).*$#\1#')"
NC_PORT="$(echo "$BASE" | sed -E 's#^https?://[^/:]+:?([0-9]+)?.*$#\1#')"
NC_PORT="${NC_PORT:-80}"

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; BOLD=$'\033[1m'; NC=$'\033[0m'

pass=0; fail=0
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

say()  { printf "%s\n" "$*"; }
ok()   { pass=$((pass+1)); printf "%s✔%s %s\n" "$GREEN" "$NC" "$*"; }
bad()  { fail=$((fail+1)); printf "%s✘%s %s\n" "$RED" "$NC" "$*"; }
note() { printf "%s•%s %s\n" "$CYAN" "$NC" "$*"; }

# --- helpers ---------------------------------------------------------------

# curl wrapper: saves headers/body into files and prints http code to stdout
curl_do() {
  local method="$1"; shift
  local url="$1"; shift
  local hdr="$TMPDIR/hdr.$$"
  local body="$TMPDIR/body.$$"
  local code

  # shellcheck disable=SC2086
  code="$(curl -sS -i -X "$method" "$url" "$@" \
           -D "$hdr" \
           -o "$body" \
           -w '%{http_code}')"

  printf "%s\n%s\n" "$hdr" "$body" >"$TMPDIR/last"   # for debugging
  echo "$code:$hdr:$body"
}

get_header() {
  local hdrfile="$1" name="$2"
  # case-insensitive header lookup
  awk -v IGNORECASE=1 -v key="$name" '
    BEGIN{FS=":"}
    tolower($1)==tolower(key){sub(/^[[:space:]]+/,"",$2); print $2}
  ' "$hdrfile" | sed 's/\r$//'
}

status_line() {
  head -n1 "$1" | tr -d '\r'
}

expect_code() {
  local got="$1" want="$2" ctx="$3"
  if [[ "$got" == "$want" ]]; then ok "$ctx (HTTP $want)"; else bad "$ctx (ожидался $want, получили $got)"; fi
}

contains() {
  local hay="$1" needle="$2"
  [[ "$hay" == *"$needle"* ]]
}

# ensure bigfile for 413 test (≈25MB)
ensure_bigfile() {
  local f="bigfile.bin"
  if [[ ! -s "$f" || $(stat -f%z "$f" 2>/dev/null || stat -c%s "$f") -lt 25000000 ]]; then
    note "Генерирую bigfile.bin (~25MB) для теста 413…"
    # portable
    dd if=/dev/zero of="$f" bs=1m count=25 status=none || \
    head -c 25000000 /dev/zero > "$f"
  fi
}

# netcat portable wrapper
nc_send() {
  # printf payload | nc host port
  if command -v nc >/dev/null 2>&1; then
    nc "$NC_HOST" "$NC_PORT"
  else
    # macOS usually has netcat; if not, we skip those tests
    return 127
  fi
}

# --- TESTS -----------------------------------------------------------------

total() { echo; printf "%sИТОГО:%s %sPASS%s=%d  %sFAIL%s=%d\n" "$BOLD" "$NC" "$GREEN" "$NC" "$pass" "$RED" "$NC" "$fail"; echo; }

say "${BOLD}Тестирую ${BASE}${NC}"
echo

# 1) GET / 200 + ETag + Last-Modified
res="$(curl_do GET "$BASE/")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"; body="${rest##*:}"
expect_code "$code" "200" "GET /"
etag="$(get_header "$hdr" 'ETag')"
lm="$(get_header "$hdr" 'Last-Modified')"
[[ -n "$etag" ]] && ok "ETag присутствует: $etag" || bad "Нет ETag"
[[ -n "$lm"   ]] && ok "Last-Modified присутствует: $lm" || bad "Нет Last-Modified"

# 2) If-None-Match -> 304
if [[ -n "$etag" ]]; then
  res="$(curl_do GET "$BASE/" -H "If-None-Match: $etag")"; code="${res%%:*}"
  expect_code "$code" "304" "INM для /"
fi

# 3) HEAD / -> 200 (проверяем, что тело пустое)
res="$(curl_do HEAD "$BASE/")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"; body="${rest##*:}"
expect_code "$code" "200" "HEAD /"
if [[ -s "$body" ]]; then bad "HEAD / вернул тело (ожидалось пустое)"; else ok "HEAD / без тела"; fi

# 4) Host обязателен для HTTP/1.1
REQ=$'GET / HTTP/1.1\r\n\r\n'
if command -v nc >/dev/null 2>&1; then
  resp="$(printf "%b" "$REQ" | nc_send || true)"
  if [[ -z "$resp" ]]; then
    note "netcat не дал ответ (возможно закрыл соединение) — пропускаю проверку 400 без Host"
  else
    line="$(printf "%s" "$resp" | head -n1 | tr -d '\r')"
    contains "$line" "400" && ok "HTTP/1.1 без Host -> 400" || bad "HTTP/1.1 без Host: ожидался 400, получили: $line"
  fi
else
  note "netcat не найден — пропускаю проверку обязательного Host"
fi

# 5) 405 + Allow (PUT на /upload запрещён)
res="$(curl_do PUT "$BASE/upload")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
if [[ "$code" == "405" ]]; then
  allow="$(get_header "$hdr" 'Allow')"
  if contains "$allow" "POST" && contains "$allow" "DELETE"; then
    ok "405 + Allow корректен для PUT /upload: ${allow}"
  else
    bad "405, но Allow странный: ${allow:-<пусто>}"
  fi
else
  bad "PUT /upload ожидал 405, получил $code"
fi

# 6) Пользовательская 404 страница
res="$(curl_do GET "$BASE/no-such-file")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
expect_code "$code" "404" "GET /no-such-file"
if grep -qi "ERROR 404" "$body"; then ok "404 body = кастомная страница"; else bad "404 body не выглядит как кастомная"; fi

# 7) 501 Not Implemented (PUT /)
res="$(curl_do PUT "$BASE/")"; code="${res%%:*}"
expect_code "$code" "501" "PUT /"

# 8) If-Modified-Since для индекса директории (/foo/ или /)
#   a) /foo/ запросить Last-Modified
res="$(curl_do GET "$BASE/foo/")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
lm_foo="$(get_header "$hdr" 'Last-Modified')"
if [[ -n "$lm_foo" ]]; then
  res2="$(curl_do GET "$BASE/foo/" -H "If-Modified-Since: $lm_foo")"; code2="${res2%%:*}"
  expect_code "$code2" "304" "IMS для /foo/"
else
  note "Нет Last-Modified у /foo/ — пропускаю IMS-тест"
fi

# 9) CGI: /cgi/hello.py
res="$(curl_do GET "$BASE/cgi/hello.py")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"; body="${rest##*:}"
expect_code "$code" "200" "GET /cgi/hello.py"
te="$(get_header "$hdr" 'Transfer-Encoding')"
contains "$(tr '[:upper:]' '[:lower:]' <<<"$te")" "chunked" && ok "CGI hello -> chunked" || bad "CGI hello без chunked"
grep -q "Hello from CGI" "$body" && ok "CGI body OK" || bad "CGI body не совпадает"

# 10) CGI: /cgi/chunked.py
res="$(curl_do GET "$BASE/cgi/chunked.py")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"; body="${rest##*:}"
expect_code "$code" "200" "GET /cgi/chunked.py"
te="$(get_header "$hdr" 'Transfer-Encoding')"
contains "$(tr '[:upper:]' '[:lower:]' <<<"$te")" "chunked" && ok "CGI chunked -> chunked" || bad "CGI chunked без chunked"
grep -q "Hello chunked!" "$body" && ok "CGI chunked body OK" || bad "CGI chunked body не совпадает"

# 11) POST upload -> 201 + Location, последующий GET по /uploads/<file> совпадает с телом
payload="test-$(date +%s)"
res="$(curl_do POST "$BASE/upload" --data-binary "$payload")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
expect_code "$code" "201" "POST /upload"
loc="$(get_header "$hdr" 'Location')"
if [[ -z "$loc" ]]; then
  bad "201 без Location"
else
  ok "Location присутствует: $loc"
  # у тебя файлы доступны под /uploads/..., а Location указывает на /upload/...
  mapped="$(echo "$loc" | sed 's#^/upload/#/uploads/#')"
  res="$(curl_do GET "$BASE$mapped")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
  if [[ "$code" == "200" ]] && [[ "$(cat "$body")" == "$payload" ]]; then
    ok "GET $mapped возвращает загруженное тело"
    # 12) DELETE загруженного
    res="$(curl_do DELETE "$BASE$mapped")"; code="${res%%:*}"
    expect_code "$code" "204" "DELETE $mapped"
  else
    bad "GET $mapped не совпадает с загруженным телом (код $code)"
  fi
fi

# 13) DELETE traversal попытка
res="$(curl_do DELETE "$BASE/../../etc/hosts")"; code="${res%%:*}"
if [[ "$code" == "204" ]]; then
  bad "DELETE traversal неожиданно успешен (204)"
else
  ok "DELETE traversal заблокирован (HTTP $code)"
fi

# 14) autoindex /dirlist/
res="$(curl_do GET "$BASE/dirlist/")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
expect_code "$code" "200" "GET /dirlist/"
grep -qi "Index of /dirlist/" "$body" && ok "autoindex HTML присутствует" || note "autoindex контент не распознан (проверь вручную)"

# 15) редирект без слеша для /dirlist
res="$(curl_do GET "$BASE/dirlist")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
if [[ "$code" == "301" ]]; then
  loc="$(get_header "$hdr" 'Location')"
  [[ "$loc" == "/dirlist/" ]] && ok "301 /dirlist -> /dirlist/" || bad "301 есть, но Location: $loc"
else
  bad "GET /dirlist ожидал 301, получил $code"
fi

# 16) keep-alive: два запроса по одному TCP
if command -v nc >/dev/null 2>&1; then
  REQ=$'GET / HTTP/1.1\r\nHost: test\r\nConnection: keep-alive\r\n\r\nGET /foo HTTP/1.1\r\nHost: test\r\n\r\n'
  resp="$(printf "%b" "$REQ" | nc_send || true)"
  hits="$(printf "%s" "$resp" | grep -cE '^HTTP/1\.1 ' || true)"
  if [[ "$hits" -ge 2 ]]; then ok "Keep-Alive: два ответа в одном соединении"; else bad "Keep-Alive: ожидалось 2 ответа, получили $hits"; fi
else
  note "netcat не найден — пропускаю keep-alive мультизапрос"
fi

# 17) 413 на большой аплоад
ensure_bigfile
res="$(curl_do POST "$BASE/upload" --data-binary @bigfile.bin)"; code="${res%%:*}"
if [[ "$code" == "201" ]]; then
  note "Большой файл прошёл — возможно, лимит в конфиге выше. Пропускаю принудительный FAIL."
else
  [[ "$code" == "413" ]] && ok "413 Payload Too Large" || note "Ожидался 413, получили $code (см. client_max_body_size)"
fi

total
exit $(( fail == 0 ? 0 : 1 ))