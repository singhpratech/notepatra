#!/usr/bin/env bash
# Notepatra palette preview - synthetic; no real data
# Exercises: shebang, functions, if/then/fi, case esac, for/while,
# [[ ]] tests, $(command substitution), arrays, parameter expansion.

set -euo pipefail

readonly PI=3.14159
readonly MAX_RETRIES=16
declare -a USERS=("Alice" "Bob" "Carol")
declare -A EMAILS=(
    [Alice]="alice@example.com"
    [Bob]="bob@example.org"
    [Carol]="carol@example.org"
)

log() {
    local level="$1"; shift
    printf '[%s] %s\n' "${level^^}" "$*"
}

greet() {
    local name="${1:-anonymous}"
    local email="${EMAILS[$name]:-unknown@example.com}"
    echo "hello ${name} <${email}>"
}

classify() {
    local value="$1"
    case "$value" in
        ''|null)       echo "empty" ;;
        -[0-9]*)       echo "negative:${value}" ;;
        [0-9]*)        echo "int:${value}" ;;
        *@example.*)   echo "email:${value}" ;;
        *)             echo "str:${value}" ;;
    esac
}

main() {
    log info "starting; pi=${PI} retries=${MAX_RETRIES}"

    for u in "${USERS[@]}"; do
        greet "$u"
    done

    local count=0
    while (( count < 3 )); do
        count=$((count + 1))
    done
    log debug "loop ran ${count} times"

    local now
    now="$(date -u +%Y-%m-%dT%H:%M:%SZ 2>/dev/null || echo 'n/a')"
    log info "now=${now}"

    if [[ "${#USERS[@]}" -gt 0 && -n "${EMAILS[Alice]:-}" ]]; then
        log ok "users present"
    fi

    for v in "" 42 -7 alice@example.com hello; do
        classify "$v"
    done

    local upper="${USERS[0]^^}"
    local lower="${USERS[0],,}"
    log info "case: ${upper} / ${lower}"
}

main "$@"
