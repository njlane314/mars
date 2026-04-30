#!/usr/bin/env python3
"""
Import Uniswap V3 WETH/USDC swaps from Google BigQuery into mars SQLite.

The script uses the Google Cloud `bq` command, keeps query windows bounded, and
does a dry run before each real query so maximum_bytes_billed can stop mistakes.
"""

import argparse
import datetime as dt
import json
import os
import shutil
import sqlite3
import subprocess
import sys
from decimal import Decimal, getcontext


pool_default = "0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640"
event_signature = "Swap(address,address,int256,int256,uint160,uint128,int24)"


def die(msg):
    print(f"bq_dex_import: {msg}", file=sys.stderr)
    return 1


def parse_time(s):
    text = s.strip()

    if len(text) == 10:
        return dt.datetime.strptime(text, "%Y-%m-%d").replace(tzinfo=dt.timezone.utc)
    if text.endswith("Z"):
        text = text[:-1] + "+00:00"
    out = dt.datetime.fromisoformat(text)
    if out.tzinfo is None:
        out = out.replace(tzinfo=dt.timezone.utc)
    return out.astimezone(dt.timezone.utc)


def bq_time(t):
    return t.strftime("%Y-%m-%d %H:%M:%S+00")


def quote_sql(s):
    return "'" + s.replace("\\", "\\\\").replace("'", "\\'") + "'"


def build_sql(pool, start, end):
    return f"""
WITH swaps AS (
  SELECT
    block_timestamp,
    block_number,
    log_index,
    transaction_hash,
    SAFE_CAST(STRING(args[2]) AS BIGNUMERIC) AS amount0,
    SAFE_CAST(STRING(args[3]) AS BIGNUMERIC) AS amount1,
    SAFE_CAST(STRING(args[4]) AS BIGNUMERIC) AS sqrt_price_x96,
    SAFE_CAST(STRING(args[5]) AS BIGNUMERIC) AS liquidity,
    SAFE_CAST(STRING(args[6]) AS INT64) AS tick
  FROM `bigquery-public-data.blockchain_analytics_ethereum_mainnet_us.decoded_events`
  WHERE event_signature = {quote_sql(event_signature)}
    AND address = {quote_sql(pool)}
    AND block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
)
SELECT
  UNIX_SECONDS(block_timestamp) AS ts,
  block_number,
  log_index,
  transaction_hash AS tx_hash,
  CAST(amount0 AS STRING) AS amount0_raw,
  CAST(amount1 AS STRING) AS amount1_raw,
  CAST(sqrt_price_x96 AS STRING) AS sqrt_price_x96,
  CAST(liquidity AS STRING) AS liquidity,
  tick
FROM swaps
WHERE amount0 IS NOT NULL
  AND amount1 IS NOT NULL
  AND sqrt_price_x96 IS NOT NULL
  AND liquidity IS NOT NULL
  AND amount0 != 0
  AND amount1 != 0
ORDER BY block_number, log_index
"""


def run_bq(args, sql, dry_run):
    cmd = [
        args.bq,
        f"--project_id={args.project}",
        f"--location={args.location}",
        "--quiet=true",
        "query",
        "--use_legacy_sql=false",
        f"--maximum_bytes_billed={args.maximum_bytes_billed}",
    ]
    if dry_run:
        cmd.append("--dry_run")
    else:
        cmd.extend(["--format=json", f"--max_rows={args.max_rows}"])
    cmd.append(sql)

    return subprocess.run(cmd, text=True, capture_output=True, check=False)


def load_rows(stdout):
    text = stdout.strip()

    if not text:
        return []
    try:
        data = json.loads(text)
        if isinstance(data, list):
            return data
    except json.JSONDecodeError:
        pass

    rows = []
    for line in text.splitlines():
        line = line.strip()
        if line:
            rows.append(json.loads(line))
    return rows


def price_from_sqrt(s):
    getcontext().prec = 80
    sqrt_raw = Decimal(s)
    q96 = Decimal(2) ** 96
    ratio = (sqrt_raw / q96) * (sqrt_raw / q96)
    if ratio <= 0:
        return 0.0
    return float((Decimal(1) / ratio) * (Decimal(10) ** 12))


def amount_abs(s, scale):
    return float(abs(Decimal(s)) / scale)


def ensure_schema(db_path, mars_bin):
    if shutil.which(mars_bin) is None and not os.path.exists(mars_bin):
        return die(f"mars binary not found: {mars_bin}")

    rc = subprocess.run([mars_bin, "db-init", db_path], check=False)
    if rc.returncode != 0:
        return die("db-init failed")
    return 0


def insert_rows(db, pool, rows, min_quote):
    stmt = (
        "INSERT OR REPLACE INTO dex_swaps"
        "(pool,block_number,log_index,tx_hash,ts,price,base_amount,quote_amount,"
        "sqrt_price_x96,liquidity,tick,amount0_raw,amount1_raw)"
        " VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)"
    )
    total = 0
    skipped = 0
    max_block = 0

    for row in rows:
        try:
            amount0 = row["amount0_raw"]
            amount1 = row["amount1_raw"]
            sqrt_price = row["sqrt_price_x96"]
            quote_amount = amount_abs(amount0, Decimal("1000000"))
            base_amount = amount_abs(amount1, Decimal("1000000000000000000"))
            price = price_from_sqrt(sqrt_price)
            block = int(row["block_number"])
            log_index = int(row["log_index"])
            tick = int(row["tick"])
            ts = int(row["ts"])
        except (KeyError, ArithmeticError, ValueError):
            skipped += 1
            continue

        if quote_amount < min_quote or base_amount <= 0.0 or price <= 0.0:
            skipped += 1
            continue

        db.execute(
            stmt,
            (
                pool,
                block,
                log_index,
                row["tx_hash"],
                ts,
                price,
                base_amount,
                quote_amount,
                sqrt_price,
                row["liquidity"],
                tick,
                amount0,
                amount1,
            ),
        )
        total += 1
        if block > max_block:
            max_block = block

    return total, skipped, max_block


def chunks(start, end, days):
    step = dt.timedelta(days=days)
    cur = start

    while cur < end:
        nxt = min(cur + step, end)
        yield cur, nxt
        cur = nxt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("db_path")
    parser.add_argument("start")
    parser.add_argument("end")
    parser.add_argument("--project", default=os.getenv("BIGQUERY_PROJECT"))
    parser.add_argument("--location", default=os.getenv("BIGQUERY_LOCATION", "US"))
    parser.add_argument("--pool", default=pool_default)
    parser.add_argument("--chunk-days", type=int, default=7)
    parser.add_argument("--maximum-bytes-billed", type=int, default=25_000_000_000)
    parser.add_argument("--max-rows", type=int, default=100_000_000)
    parser.add_argument("--min-quote", type=float, default=1.0)
    parser.add_argument("--bq", default=os.getenv("BQ_BIN", "bq"))
    parser.add_argument("--mars", default=os.getenv("MARS_BIN", "build/mars"))
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()

    if args.project is None or args.project == "":
        return die("set --project or BIGQUERY_PROJECT")
    if shutil.which(args.bq) is None:
        return die("bq command not found; install and authenticate the Google Cloud SDK")
    if args.chunk_days < 1:
        return die("--chunk-days must be positive")

    pool = args.pool.lower()
    start = parse_time(args.start)
    end = parse_time(args.end)
    if start >= end:
        return die("start must be before end")

    if ensure_schema(args.db_path, args.mars) != 0:
        return 1

    db = sqlite3.connect(args.db_path)
    imported = 0
    skipped = 0
    last_block = 0

    try:
        for lo, hi in chunks(start, end, args.chunk_days):
            sql = build_sql(pool, lo, hi)
            dry = run_bq(args, sql, True)
            if dry.returncode != 0:
                print(dry.stderr, file=sys.stderr)
                return die(f"dry run failed for {lo.isoformat()} to {hi.isoformat()}")
            print(dry.stderr.strip() or dry.stdout.strip(), file=sys.stderr)

            if not args.run:
                continue

            got = run_bq(args, sql, False)
            if got.returncode != 0:
                print(got.stderr, file=sys.stderr)
                return die(f"query failed for {lo.isoformat()} to {hi.isoformat()}")

            rows = load_rows(got.stdout)
            with db:
                n, bad, block = insert_rows(db, pool, rows, args.min_quote)
                imported += n
                skipped += bad
                last_block = max(last_block, block)
                if block > 0:
                    db.execute(
                        "INSERT OR REPLACE INTO ingest_state(source,last_key,updated_at) "
                        "VALUES(?,?,strftime('%s','now'))",
                        (f"bq_dex_last_block_{pool}", str(block)),
                    )
            print(
                f"bq-dex-import: {lo.date()}..{hi.date()} rows={n} skipped={bad}",
                file=sys.stderr,
            )
    finally:
        db.close()

    if args.run:
        print(f"imported={imported} skipped={skipped} last_block={last_block}")
    else:
        print("dry_run_only=1; pass --run to import")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
