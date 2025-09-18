#!/usr/bin/env bash
# subject_tester.sh — строгий к сабджекту тестер
# Usage:
#   ./subject_tester.sh [BASE_URL] [--port2 BASE_URL2] [--vhost HOST PATH EXPECT]...
# Примеры:
#   ./subject_tester.sh
#   ./subject_tester.sh http://127.0.0.1:8080 --vhost site1.local / "Site1" --vhost site2.local / "Site2"
#   ./subject_tester.sh http://127.0.0.1:8080 --port2 http://127.0.0.1:8081

set -euo pipefail

BASE="${1:-http://127.0.0.1:8080}"
shift || true

PORT2=""
# массивы для vhost-проверок
VHOSTS=()    # host
VPATHS=()    # path
VEXPECTS=()  # подстрока в теле

while [[ $# -gt 0 ]]; do
  case "$1" in
    --port2)
      PORT2="${2:-}"; shift 2;;
    --vhost)
      # три аргумента: HOST PATH EXPECT
      VHOSTS+=("${2:-}")
      VPATHS+=("${3:-/}")
      VEXPECTS+=("${4:-}")
      shift 4;;
    *)
      # игнор/совместимость
      shift;;
  esac
done

RED=$'\033[31m'; GREEN=$'\033[32m'; YELLOW=$'\033[33m'; CYAN=$'\033[36m'; BOLD=$'\033[1m'; NC=$'\033[0m'
pass=0; fail=0
TMPDIR="$(mktemp -d)"
trap 'rm -rf "$TMPDIR"' EXIT

say()  { printf "%s\n" "$*"; }
ok()   { pass=$((pass+1)); printf "%s✔%s %s\n" "$GREEN" "$NC" "$*"; }
bad()  { fail=$((fail+1)); printf "%s✘%s %s\n" "$RED" "$NC" "$*"; }
note() { printf "%s•%s %s\n" "$CYAN" "$NC" "$*"; }

curl_do() { # method url [curl args...]; stdout: code:hdrfile:bodyfile
  local method="$1"; shift
  local url="$1"; shift
  local hdr="$TMPDIR/hdr.$$"
  local body="$TMPDIR/body.$$"
  local code
  # shellcheck disable=SC2086
  code="$(curl -sS -i -X "$method" "$url" "$@" -D "$hdr" -o "$body" -w '%{http_code}')"
  printf "%s:%s:%s\n" "$code" "$hdr" "$body"
}

get_header() { # file header-name
  local hdrfile="$1" name="$2"
  awk -v IGNORECASE=1 -v key="$name" '
    BEGIN{FS=":"}
    tolower($1)==tolower(key){sub(/^[[:space:]]+/,"",$2); gsub(/\r$/,"",$2); print $2}
  ' "$hdrfile"
}

expect_code() { # got want ctx
  if [[ "$1" == "$2" ]]; then ok "$3 (HTTP $2)"; else bad "$3 (ожидался $2, получили $1)"; fi
}

contains() { [[ "$1" == *"$2"* ]]; }

ensure_bigfile() {
  local f="bigfile.bin"
  if [[ ! -s "$f" || $(stat -f%z "$f" 2>/dev/null || stat -c%s "$f") -lt 25000000 ]]; then
    note "Генерирую bigfile.bin (~25MB) для теста 413…"
    dd if=/dev/zero of="$f" bs=1m count=25 status=none || head -c 25000000 /dev/zero > "$f"
  fi
}

total() { echo; printf "%sИТОГО:%s %sPASS%s=%d  %sFAIL%s=%d\n" "$BOLD" "$NC" "$GREEN" "$NC" "$pass" "$RED" "$NC" "$fail"; echo; }

say "${BOLD}Тестирую ${BASE}${NC}"
echo

# ------------------ 1) Статика (GET /) ------------------
res="$(curl_do GET "$BASE/")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"; body="${rest##*:}"
expect_code "$code" "200" "GET / (статика)"
etag="$(get_header "$hdr" 'ETag')"
lm="$(get_header "$hdr" 'Last-Modified')"
[[ -n "$etag" ]] && ok "ETag присутствует: $etag" || note "ETag не обязателен"
[[ -n "$lm"   ]] && ok "Last-Modified присутствует: $lm" || note "Last-Modified отсутствует"

# ------------------ 2) HEAD / (без тела) ----------------
res="$(curl_do HEAD "$BASE/")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
expect_code "$code" "200" "HEAD /"
if [[ -s "$body" ]]; then bad "HEAD / вернул тело (ожидалось пустое)"; else ok "HEAD / без тела"; fi

# ------------------ 3) Директория: index по умолчанию ---
res="$(curl_do GET "$BASE/foo/")"; code="${res%%:*}"
if [[ "$code" == "200" ]]; then ok "GET /foo/ (index) — 200"; else note "/foo/ -> $code (возможно нет локейшна)"; fi

# ------------------ 4) Директория: редирект без слеша ---
res="$(curl_do GET "$BASE/dirlist")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
if [[ "$code" == "301" || "$code" == "308" ]]; then
  loc="$(get_header "$hdr" 'Location')"
  [[ "$loc" == */dirlist/ ]] && ok "Редирект /dirlist -> /dirlist/ ($code)" || bad "Редирект есть, но Location='$loc'"
else
  note "Нет 301/308 для /dirlist (код $code)"
fi

# ------------------ 5) Autoindex (если включён) ---------
res="$(curl_do GET "$BASE/dirlist/")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
if [[ "$code" == "200" ]]; then
  if grep -qi "Index of /dirlist/" "$body"; then ok "Autoindex HTML на /dirlist/"; else note "200 на /dirlist/, но autoindex шаблон не распознан"; fi
else
  note "/dirlist/ -> $code (возможно autoindex off)"
fi

# ------------------ 6) 404 страница ---------------------
res="$(curl_do GET "$BASE/no-such-file")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
expect_code "$code" "404" "GET /no-such-file (404)"
if [[ -s "$body" ]]; then ok "404 body присутствует"; else bad "404 без тела"; fi

# ------------------ 7) CGI (GET /cgi/hello.py) ----------
res="$(curl_do GET "$BASE/cgi/hello.py")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
if [[ "$code" == "200" ]]; then
  ok "CGI hello.py -> 200"
  if grep -qi "Hello" "$body"; then ok "CGI body содержит текст"; else note "CGI 200, но тело неузнаваемое"; fi
else
  note "CGI hello.py -> $code (проверь конфиг CGI)"
fi

# ------------------ 8) CGI: второй пример (опц.) --------
res="$(curl_do GET "$BASE/cgi/chunked.py")"; code="${res%%:*}"
if [[ "$code" == "200" ]]; then ok "CGI chunked.py -> 200"; else note "CGI chunked.py -> $code (опционально)"; fi

# ------------------ 9) Upload (POST) + GET + DELETE -----
payload="test-$(date +%s).$$"
res="$(curl_do POST "$BASE/upload" --data-binary "$payload")"; code="${res%%:*}"; rest="${res#*:}"; hdr="${rest%%:*}"
if [[ "$code" == "201" ]]; then
  ok "POST /upload -> 201 Created"
  loc="$(get_header "$hdr" 'Location')"
  if [[ -z "$loc" ]]; then
    bad "201 без Location"
  else
    ok "Location: $loc"
    res="$(curl_do GET "$BASE$loc")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
    if [[ "$code" == "200" && "$(cat "$body")" == "$payload" ]]; then
      ok "GET $loc возвращает загруженное тело"
      res="$(curl_do DELETE "$BASE$loc")"; code="${res%%:*}"
      [[ "$code" == "204" ]] && ok "DELETE $loc -> 204" || bad "DELETE $loc ожидал 204, получил $code"
    else
      # fallback на /uploads/
      loc2="$(echo "$loc" | sed 's#^/upload/#/uploads/#')"
      res="$(curl_do GET "$BASE$loc2")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
      if [[ "$code" == "200" && "$(cat "$body")" == "$payload" ]]; then
        ok "GET $loc2 возвращает загруженное тело"
        res="$(curl_do DELETE "$BASE$loc2")"; code="${res%%:*}"
        [[ "$code" == "204" ]] && ok "DELETE $loc2 -> 204" || bad "DELETE $loc2 ожидал 204, получил $code"
      else
        bad "Ни $loc ни $loc2 не вернули загруженное тело (код $code)"
      fi
    fi
  fi
else
  bad "POST /upload ожидал 201, получил $code"
fi

# ------------------ 10) Ограничение client body size ----
ensure_bigfile
res="$(curl_do POST "$BASE/upload" --data-binary @bigfile.bin)"; code="${res%%:*}"
if [[ "$code" == "413" ]]; then ok "413 Payload Too Large"; else note "Лимит выше (код $code) — допустимо"; fi

# ------------------ 11) Методы на роуте (пример: PUT) ---
res="$(curl_do PUT "$BASE/upload")"; code="${res%%:*}"
if [[ "$code" == "405" || "$code" == "501" ]]; then ok "PUT /upload не разрешён (код $code)"; else note "PUT /upload -> $code"; fi

# ------------------ 12) Второй порт (опционально) -------
if [[ -n "$PORT2" ]]; then
  res="$(curl_do GET "$PORT2/")"; code="${res%%:*}"
  [[ "$code" == "200" ]] && ok "Слушает и на втором порту ($PORT2)" || bad "Второй порт не отвечает 200 ($code)"
else
  note "Тест второго порта пропущен (не передан --port2)"
fi

# ------------------ 13) server_name / Host-based routing -
if [[ "${#VHOSTS[@]}" -gt 0 ]]; then
  say ""; say "${BOLD}Проверки server_name (Host)${NC}"
  for i in "${!VHOSTS[@]}"; do
    host="${VHOSTS[$i]}"; path="${VPATHS[$i]}"; expect="${VEXPECTS[$i]}"
    url="$BASE$path"
    res="$(curl_do GET "$url" -H "Host: $host")"; code="${res%%:*}"; rest="${res#*:}"; body="${rest##*:}"
    if [[ "$code" == "200" ]]; then
      if [[ -z "$expect" ]] || grep -q "$expect" "$body"; then
        ok "Host: $host $path -> 200 (совпало с ожидаемым контентом)"
      else
        bad "Host: $host $path -> 200, но тело не содержит ожидаемое: '$expect'"
      fi
    else
      bad "Host: $host $path — ожидался 200, получили $code"
    fi
  done
else
  note "Проверки server_name пропущены (не переданы --vhost кейсы)"
fi

# ---------------------------------------------------------------------------
echo
printf "%sИТОГО:%s %sPASS%s=%d  %sFAIL%s=%d\n" "$BOLD" "$NC" "$GREEN" "$NC" "$pass" "$RED" "$NC" "$fail"
echo
exit $(( fail == 0 ? 0 : 1 ))