#!/bin/sh
set -eu

script_dir=$(CDPATH= cd "$(dirname "$0")" && pwd)
repo_root=$(CDPATH= cd "$script_dir/../.." && pwd)
node_dir=${1:-${MARS_ETH_NODE_DIR:-"$repo_root/node/data/mainnet"}}

case "$node_dir" in
/*) ;;
*) node_dir="$repo_root/$node_dir" ;;
esac

if [ ! -f "$node_dir/jwt.hex" ]; then
    echo "usage: $0 [/path/to/initialized/node-data-dir]" >&2
    echo "run node/bin/init-mainnet.sh first" >&2
    exit 2
fi

exec geth \
    --mainnet \
    --datadir "$node_dir/geth" \
    --syncmode snap \
    --cache 4096 \
    --http \
    --http.addr 127.0.0.1 \
    --http.port 8545 \
    --http.api eth,net,web3 \
    --http.vhosts localhost,127.0.0.1 \
    --authrpc.addr 127.0.0.1 \
    --authrpc.port 8551 \
    --authrpc.jwtsecret "$node_dir/jwt.hex"
