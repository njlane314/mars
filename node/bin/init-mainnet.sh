#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/../.." && pwd)
node_dir=${1:-${MARS_ETH_NODE_DIR:-"$repo_root/node/data/mainnet"}}
min_gib=${MARS_ETH_MIN_FREE_GIB:-700}

case "$node_dir" in
/*) ;;
*) node_dir="$repo_root/$node_dir" ;;
esac

mkdir -p "$node_dir"

avail_gib=$(df -g "$node_dir" | awk 'NR==2 {print $4}')
if [ "${avail_gib:-0}" -lt "$min_gib" ]; then
    echo "refusing: $node_dir has ${avail_gib:-0} GiB free; need at least $min_gib GiB" >&2
    echo "set MARS_ETH_MIN_FREE_GIB to override, or use a larger disk" >&2
    exit 1
fi

mkdir -p "$node_dir/geth" "$node_dir/lighthouse"

if [ ! -f "$node_dir/jwt.hex" ]; then
    openssl rand -hex 32 > "$node_dir/jwt.hex"
    chmod 600 "$node_dir/jwt.hex"
fi

cat <<EOF
initialized $node_dir

terminal 1:
  node/bin/start-geth-mainnet.sh "$node_dir"

terminal 2:
  node/bin/start-lighthouse-mainnet.sh "$node_dir"

after sync begins:
  export ETH_RPC_URL=http://127.0.0.1:8545
EOF
