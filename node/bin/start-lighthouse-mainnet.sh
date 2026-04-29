#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/../.." && pwd)
node_dir=${1:-${MARS_ETH_NODE_DIR:-"$repo_root/node/data/mainnet"}}
checkpoint_url=${MARS_CHECKPOINT_URL:-https://mainnet.checkpoint.sigp.io}

case "$node_dir" in
/*) ;;
*) node_dir="$repo_root/$node_dir" ;;
esac

if [ ! -f "$node_dir/jwt.hex" ]; then
    echo "usage: $0 [/path/to/initialized/node-data-dir]" >&2
    echo "run node/bin/init-mainnet.sh first" >&2
    exit 2
fi

exec lighthouse bn \
    --network mainnet \
    --datadir "$node_dir/lighthouse" \
    --execution-endpoint http://127.0.0.1:8551 \
    --execution-jwt "$node_dir/jwt.hex" \
    --checkpoint-sync-url "$checkpoint_url" \
    --http \
    --http-address 127.0.0.1 \
    --http-port 5052
