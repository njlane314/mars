#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/../.." && pwd)
identity=${MARS_FRED_AGE_IDENTITY:-"$repo_root/.secrets/fred.agekey"}
src="$repo_root/secrets/fred.env.age"
out=${1:-"$repo_root/.env"}

if ! command -v age >/dev/null 2>&1; then
    echo "age is required; install with: brew install age" >&2
    exit 127
fi

if [ ! -f "$identity" ]; then
    echo "missing private identity: $identity" >&2
    exit 2
fi

if [ ! -f "$src" ]; then
    echo "missing encrypted secret: $src" >&2
    exit 2
fi

umask 077
tmp=$(mktemp "$out.tmp.XXXXXX")
age -d -i "$identity" -o "$tmp" "$src"
chmod 600 "$tmp"
mv "$tmp" "$out"
