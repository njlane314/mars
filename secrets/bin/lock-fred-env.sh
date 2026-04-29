#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/../.." && pwd)
src=${1:-"$repo_root/.env"}
recipient_file="$repo_root/secrets/fred.env.age.recipient"
out="$repo_root/secrets/fred.env.age"

if ! command -v age >/dev/null 2>&1; then
    echo "age is required; install with: brew install age" >&2
    exit 127
fi

if [ ! -f "$src" ]; then
    echo "missing source env file: $src" >&2
    exit 2
fi

if [ ! -f "$recipient_file" ]; then
    echo "missing recipient file: $recipient_file" >&2
    exit 2
fi

recipient=$(sed -n 's/^recipient=//p' "$recipient_file" | head -n 1)
if [ -z "$recipient" ]; then
    echo "recipient file does not contain recipient=..." >&2
    exit 2
fi

tmp=$(mktemp "$out.tmp.XXXXXX")
age -r "$recipient" -o "$tmp" "$src"
mv "$tmp" "$out"
