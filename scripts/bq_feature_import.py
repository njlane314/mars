#!/usr/bin/env python3
"""
Import observed hourly external features from BigQuery into mars SQLite.

The script only imports aggregates computed from public source rows. It does not
copy raw blockchain tables or raw event streams into SQLite. It does not fill
missing market data with generated values; unavailable fields remain NULL.
"""

import argparse
import datetime as dt
import json
import os
import shutil
import sqlite3
import subprocess
import sys


eth_dataset = "bigquery-public-data.goog_blockchain_ethereum_mainnet_us"
dex_dataset = "bigquery-public-data.goog_blockchain_ethereum_mainnet_us"
btc_dataset = "bigquery-public-data.crypto_bitcoin"
gdelt_table = "gdelt-bq.gdeltv2.events"
weth_usdc_pool = "0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640"
wbtc_weth_pool = "0xcbcdf9626bc03e24f779434178a73a0b4bad62ed"
swap_event = "Swap(address,address,int256,int256,uint160,uint128,int24)"


stablecoin_scales = {
    "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48": 1_000_000.0,  # USDC
    "0xdac17f958d2ee523a2206206994597c13d831ec7": 1_000_000.0,  # USDT
    "0x6b175474e89094c44da98b954eedeac495271d0f": 1_000_000_000_000_000_000.0,  # DAI
}


l2_chains = {
    "polygon": {
        "dataset": "bigquery-public-data.goog_blockchain_polygon_mainnet_us",
        "stablecoins": {
            "0x2791bca1f2de4661ed88a30c99a7a9449aa84174": 1_000_000.0,
            "0x3c499c542cef5e3811e1192ce70d8cc03d5c3359": 1_000_000.0,
            "0xc2132d05d31c914a87c6611c10748aeb04b58e8f": 1_000_000.0,
            "0x8f3cf7ad23cd3cadbd9735aff958023239c6a063": 1_000_000_000_000_000_000.0,
        },
    },
    "arbitrum": {
        "dataset": "bigquery-public-data.goog_blockchain_arbitrum_one_us",
        "stablecoins": {
            "0xaf88d065e77c8cc2239327c5edb3a432268e5831": 1_000_000.0,
            "0xff970a61a04b1ca14834a43f5de4533ebddb5cc8": 1_000_000.0,
            "0xfd086bc7cd5c481dcc9c85ebe478a1c0b69fcbb9": 1_000_000.0,
            "0xda10009cbd5d07dd0cecc66161fc93d7c9000da1": 1_000_000_000_000_000_000.0,
        },
    },
    "optimism": {
        "dataset": "bigquery-public-data.goog_blockchain_optimism_mainnet_us",
        "stablecoins": {
            "0x0b2c639c533813f4aa9d7837caf62653d097ff85": 1_000_000.0,
            "0x7f5c764cbc14f9669b88837ca1490cca17c31607": 1_000_000.0,
            "0x94b008aa00579c1307b0ef2c499ad98a8ce58e58": 1_000_000.0,
            "0xda10009cbd5d07dd0cecc66161fc93d7c9000da1": 1_000_000_000_000_000_000.0,
        },
    },
}


def die(msg):
    print(f"bq_feature_import: {msg}", file=sys.stderr)
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


def stablecoin_case(token_col, quantity_col, mapping):
    parts = [
        f"WHEN {token_col} = '{addr}' THEN SAFE_CAST({quantity_col} AS BIGNUMERIC) / {scale:.1f}"
        for addr, scale in mapping.items()
    ]
    return "CASE " + " ".join(parts) + " ELSE 0 END"


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
        rows = json.loads(text)
        if isinstance(rows, list):
            return rows
    except json.JSONDecodeError:
        pass

    return [json.loads(line) for line in text.splitlines() if line.strip()]


def row_float(row, name):
    value = row.get(name)
    if value is None or value == "":
        return None
    return float(value)


def row_int(row, name):
    value = row.get(name)
    if value is None or value == "":
        return None
    return int(value)


def row_ts(row):
    value = row.get("ts")
    if value is None:
        raise ValueError("missing ts")
    return int(value)


def ensure_schema(db):
    db.executescript(
        """
        CREATE TABLE IF NOT EXISTS bq_eth_hourly(
          ts INTEGER PRIMARY KEY,
          block_count INTEGER,
          tx_count INTEGER,
          gas_used REAL,
          avg_gas_used_ratio REAL,
          avg_base_fee_gwei REAL,
          avg_priority_fee_gwei REAL,
          failed_tx_ratio REAL,
          contract_creation_count INTEGER,
          erc20_transfer_count INTEGER,
          stablecoin_transfer_volume REAL,
          weth_usdc_swap_count INTEGER,
          weth_usdc_swap_volume_usd REAL,
          weth_usdc_swap_volume_eth REAL,
          eth_price REAL,
          wbtc_weth_swap_count INTEGER,
          wbtc_weth_volume_btc REAL,
          wbtc_weth_volume_eth REAL,
          btc_eth_price REAL,
          weth_usdc_buy_eth REAL,
          weth_usdc_sell_eth REAL,
          wbtc_weth_buy_eth REAL,
          wbtc_weth_sell_eth REAL
        );
        CREATE TABLE IF NOT EXISTS bq_l2_hourly(
          ts INTEGER NOT NULL,
          chain TEXT NOT NULL,
          tx_count INTEGER,
          gas_used REAL,
          stablecoin_transfer_volume REAL,
          PRIMARY KEY(ts,chain)
        );
        CREATE TABLE IF NOT EXISTS bq_btc_hourly(
          ts INTEGER PRIMARY KEY,
          tx_count INTEGER,
          fee_total_btc REAL,
          fee_per_tx_btc REAL,
          output_value_btc REAL,
          output_count INTEGER,
          input_count INTEGER
        );
        CREATE TABLE IF NOT EXISTS bq_gdelt_hourly(
          ts INTEGER PRIMARY KEY,
          crypto_regulation_intensity REAL,
          exchange_stress_intensity REAL,
          cyber_security_intensity REAL,
          banking_stress_intensity REAL,
          broad_risk_off_intensity REAL
        );
        """
    )
    ensure_column(db, "bq_eth_hourly", "weth_usdc_buy_eth", "REAL")
    ensure_column(db, "bq_eth_hourly", "weth_usdc_sell_eth", "REAL")
    ensure_column(db, "bq_eth_hourly", "wbtc_weth_buy_eth", "REAL")
    ensure_column(db, "bq_eth_hourly", "wbtc_weth_sell_eth", "REAL")
    create_feature_view(db)


def ensure_column(db, table, name, decl):
    rows = db.execute(f"PRAGMA table_info({table})").fetchall()
    if name not in {row[1] for row in rows}:
        db.execute(f"ALTER TABLE {table} ADD COLUMN {name} {decl}")


def create_feature_view(db):
    db.executescript(
        """
        DROP VIEW IF EXISTS bq_feature_hourly;
        DROP VIEW IF EXISTS bq_ethbtc_bars;
        CREATE VIEW bq_feature_hourly AS
        WITH l2 AS (
          SELECT ts,
            sum(tx_count) AS tx_count,
            sum(gas_used) AS gas_used,
            sum(stablecoin_transfer_volume) AS stablecoin_transfer_volume
          FROM bq_l2_hourly GROUP BY ts
        ), base AS (
          SELECT e.ts,
            e.eth_price,
            CASE WHEN e.eth_price > 0 AND e.btc_eth_price > 0
              THEN e.eth_price * e.btc_eth_price ELSE NULL END AS btc_price,
            CASE WHEN e.btc_eth_price > 0 THEN 1.0 / e.btc_eth_price ELSE NULL END AS ethbtc_price,
            e.weth_usdc_swap_volume_usd AS eth_flow,
            e.avg_base_fee_gwei,
            e.gas_used AS eth_gas_used,
            e.tx_count AS eth_tx_count,
            e.stablecoin_transfer_volume,
            l2.tx_count AS l2_tx_count,
            b.tx_count AS btc_tx_count,
            b.fee_total_btc,
            b.output_value_btc,
            CASE
              WHEN g.crypto_regulation_intensity IS NULL
                OR g.exchange_stress_intensity IS NULL
                OR g.cyber_security_intensity IS NULL
                OR g.banking_stress_intensity IS NULL
                OR g.broad_risk_off_intensity IS NULL
              THEN NULL
              ELSE g.crypto_regulation_intensity + g.exchange_stress_intensity +
                g.cyber_security_intensity + g.banking_stress_intensity +
                g.broad_risk_off_intensity
            END AS news_risk,
            (SELECT value_real FROM fred_observations f
              WHERE f.series_id='VIXCLS' AND f.value_real IS NOT NULL
              AND f.date <= date(e.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1) AS vixcls,
            (SELECT value_real FROM fred_observations f
              WHERE f.series_id='T10Y2Y' AND f.value_real IS NOT NULL
              AND f.date <= date(e.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1) AS t10y2y
          FROM bq_eth_hourly e
          LEFT JOIN l2 ON l2.ts=e.ts
          LEFT JOIN bq_btc_hourly b ON b.ts=e.ts
          LEFT JOIN bq_gdelt_hourly g ON g.ts=e.ts
        ), lagged AS (
          SELECT *,
            ln(eth_price / lag(eth_price) OVER (ORDER BY ts)) AS eth_ret,
            ln(btc_price / lag(btc_price) OVER (ORDER BY ts)) AS btc_ret,
            ln(ethbtc_price / lag(ethbtc_price) OVER (ORDER BY ts)) AS ethbtc_ret,
            ln(lead(ethbtc_price,4) OVER (ORDER BY ts) / ethbtc_price) AS target_ethbtc_4h,
            ln(lead(ethbtc_price,24) OVER (ORDER BY ts) / ethbtc_price) AS target_ethbtc_24h
          FROM base
        ), stats AS (
          SELECT *,
            avg(avg_base_fee_gwei) OVER w AS base_fee_mu,
            avg(avg_base_fee_gwei*avg_base_fee_gwei) OVER w AS base_fee_m2,
            avg(eth_gas_used) OVER w AS gas_mu,
            avg(eth_gas_used*eth_gas_used) OVER w AS gas_m2,
            avg(eth_tx_count) OVER w AS tx_mu,
            avg(eth_tx_count*eth_tx_count) OVER w AS tx_m2,
            avg(stablecoin_transfer_volume) OVER w AS stable_mu,
            avg(stablecoin_transfer_volume*stablecoin_transfer_volume) OVER w AS stable_m2,
            avg(l2_tx_count) OVER w AS l2_mu,
            avg(l2_tx_count*l2_tx_count) OVER w AS l2_m2,
            avg(btc_tx_count + fee_total_btc) OVER w AS btc_chain_mu,
            avg((btc_tx_count + fee_total_btc)*(btc_tx_count + fee_total_btc)) OVER w AS btc_chain_m2,
            avg(news_risk) OVER w AS news_mu,
            avg(news_risk*news_risk) OVER w AS news_m2,
            avg(vixcls-t10y2y) OVER w AS macro_mu,
            avg((vixcls-t10y2y)*(vixcls-t10y2y)) OVER w AS macro_m2
          FROM lagged
          WINDOW w AS (ORDER BY ts ROWS BETWEEN 168 PRECEDING AND 1 PRECEDING)
        )
        SELECT ts,
          eth_ret,
          btc_ret,
          ethbtc_ret,
          NULL AS eth_funding,
          eth_flow,
          (avg_base_fee_gwei-base_fee_mu)/nullif(sqrt(max(base_fee_m2-base_fee_mu*base_fee_mu,0.0)),0.0) AS eth_base_fee_z,
          (eth_gas_used-gas_mu)/nullif(sqrt(max(gas_m2-gas_mu*gas_mu,0.0)),0.0) AS eth_gas_used_z,
          (eth_tx_count-tx_mu)/nullif(sqrt(max(tx_m2-tx_mu*tx_mu,0.0)),0.0) AS eth_tx_count_z,
          (stablecoin_transfer_volume-stable_mu)/nullif(sqrt(max(stable_m2-stable_mu*stable_mu,0.0)),0.0) AS stablecoin_flow_z,
          (l2_tx_count-l2_mu)/nullif(sqrt(max(l2_m2-l2_mu*l2_mu,0.0)),0.0) AS l2_activity_z,
          ((btc_tx_count+fee_total_btc)-btc_chain_mu)/nullif(sqrt(max(btc_chain_m2-btc_chain_mu*btc_chain_mu,0.0)),0.0) AS btc_chain_activity_z,
          (news_risk-news_mu)/nullif(sqrt(max(news_m2-news_mu*news_mu,0.0)),0.0) AS news_risk_z,
          ((vixcls-t10y2y)-macro_mu)/nullif(sqrt(max(macro_m2-macro_mu*macro_mu,0.0)),0.0) AS fred_macro_risk_z,
          target_ethbtc_4h,
          target_ethbtc_24h
        FROM stats;

        CREATE VIEW bq_ethbtc_bars AS
        SELECT f.ts AS ts,
          1.0 / e.btc_eth_price AS bid,
          1.0 / e.btc_eth_price AS ask,
          e.wbtc_weth_buy_eth AS bid_sz,
          e.wbtc_weth_sell_eth AS ask_sz,
          e.wbtc_weth_volume_eth AS volume,
          f.eth_ret AS eth_ret,
          f.btc_ret AS btc_ret,
          f.ethbtc_ret AS ethbtc_ret,
          f.eth_flow AS eth_flow,
          f.eth_base_fee_z AS eth_base_fee_z,
          f.eth_gas_used_z AS eth_gas_used_z,
          f.eth_tx_count_z AS eth_tx_count_z,
          f.stablecoin_flow_z AS stablecoin_flow_z,
          f.l2_activity_z AS l2_activity_z,
          f.btc_chain_activity_z AS btc_chain_activity_z,
          f.news_risk_z AS news_risk_z,
          f.fred_macro_risk_z AS fred_macro_risk_z
        FROM bq_feature_hourly f
        JOIN bq_eth_hourly e ON e.ts=f.ts
        WHERE e.btc_eth_price IS NOT NULL
          AND e.btc_eth_price > 0.0
          AND e.wbtc_weth_volume_eth IS NOT NULL
          AND e.wbtc_weth_volume_eth > 0.0
          AND e.wbtc_weth_buy_eth IS NOT NULL
          AND e.wbtc_weth_sell_eth IS NOT NULL
          AND (e.wbtc_weth_buy_eth + e.wbtc_weth_sell_eth) > 0.0
          AND f.eth_ret IS NOT NULL
          AND f.btc_ret IS NOT NULL
          AND f.ethbtc_ret IS NOT NULL
          AND f.eth_flow IS NOT NULL
          AND f.eth_base_fee_z IS NOT NULL
          AND f.eth_gas_used_z IS NOT NULL
          AND f.eth_tx_count_z IS NOT NULL
          AND f.stablecoin_flow_z IS NOT NULL
          AND f.l2_activity_z IS NOT NULL
          AND f.btc_chain_activity_z IS NOT NULL
          AND f.news_risk_z IS NOT NULL
          AND f.fred_macro_risk_z IS NOT NULL;
        """
    )


def eth_sql(args, start, end):
    stable_case = stablecoin_case("address", "quantity", stablecoin_scales)
    return f"""
WITH blocks AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    COUNT(*) AS block_count,
    SUM(gas_used) AS gas_used,
    AVG(SAFE_DIVIDE(gas_used, gas_limit)) AS avg_gas_used_ratio,
    AVG(base_fee_per_gas) / 1000000000.0 AS avg_base_fee_gwei
  FROM `{args.eth_dataset}.blocks`
  WHERE block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
  GROUP BY ts
), tx AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(t.block_timestamp, HOUR)) AS ts,
    COUNT(*) AS tx_count,
    AVG(GREATEST(CAST(r.effective_gas_price AS BIGNUMERIC)-CAST(b.base_fee_per_gas AS BIGNUMERIC),0)) / 1000000000.0 AS avg_priority_fee_gwei,
    AVG(CASE WHEN r.status = 0 THEN 1.0 ELSE 0.0 END) AS failed_tx_ratio,
    COUNTIF(t.to_address IS NULL) AS contract_creation_count
  FROM `{args.eth_dataset}.transactions` t
  LEFT JOIN `{args.eth_dataset}.receipts` r
    ON r.block_number=t.block_number AND r.transaction_hash=t.transaction_hash
  LEFT JOIN `{args.eth_dataset}.blocks` b
    ON b.block_number=t.block_number
  WHERE t.block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND t.block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
  GROUP BY ts
), transfers AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    COUNT(*) AS erc20_transfer_count,
    SUM(CAST(({stable_case}) AS FLOAT64)) AS stablecoin_transfer_volume
  FROM `{args.eth_dataset}.token_transfers`
  WHERE block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
  GROUP BY ts
), weth_swaps AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    SAFE_CAST(STRING(args[2]) AS BIGNUMERIC) AS amount0,
    SAFE_CAST(STRING(args[3]) AS BIGNUMERIC) AS amount1
  FROM `{args.dex_dataset}.decoded_events`
  WHERE event_signature = {quote_sql(swap_event)}
    AND address = {quote_sql(args.weth_usdc_pool)}
    AND block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
), weth AS (
  SELECT ts,
    COUNT(*) AS weth_usdc_swap_count,
    SUM(ABS(amount0) / 1000000.0) AS weth_usdc_swap_volume_usd,
    SUM(ABS(amount1) / 1000000000000000000.0) AS weth_usdc_swap_volume_eth,
    SAFE_DIVIDE(SUM(ABS(amount0) / 1000000.0),
      NULLIF(SUM(ABS(amount1) / 1000000000000000000.0),0)) AS eth_price,
    SUM(CASE WHEN amount1 < 0 THEN ABS(amount1) / 1000000000000000000.0 ELSE 0 END) AS weth_usdc_buy_eth,
    SUM(CASE WHEN amount1 > 0 THEN ABS(amount1) / 1000000000000000000.0 ELSE 0 END) AS weth_usdc_sell_eth
  FROM weth_swaps
  WHERE amount0 IS NOT NULL AND amount1 IS NOT NULL
  GROUP BY ts
), wbtc_swaps AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    SAFE_CAST(STRING(args[2]) AS BIGNUMERIC) AS amount0,
    SAFE_CAST(STRING(args[3]) AS BIGNUMERIC) AS amount1
  FROM `{args.dex_dataset}.decoded_events`
  WHERE event_signature = {quote_sql(swap_event)}
    AND address = {quote_sql(args.wbtc_weth_pool)}
    AND block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
), wbtc AS (
  SELECT ts,
    COUNT(*) AS wbtc_weth_swap_count,
    SUM(ABS(amount0) / 100000000.0) AS wbtc_weth_volume_btc,
    SUM(ABS(amount1) / 1000000000000000000.0) AS wbtc_weth_volume_eth,
    SAFE_DIVIDE(SUM(ABS(amount1) / 1000000000000000000.0),
      NULLIF(SUM(ABS(amount0) / 100000000.0),0)) AS btc_eth_price,
    SUM(CASE WHEN amount1 < 0 THEN ABS(amount1) / 1000000000000000000.0 ELSE 0 END) AS wbtc_weth_buy_eth,
    SUM(CASE WHEN amount1 > 0 THEN ABS(amount1) / 1000000000000000000.0 ELSE 0 END) AS wbtc_weth_sell_eth
  FROM wbtc_swaps
  WHERE amount0 IS NOT NULL AND amount1 IS NOT NULL
  GROUP BY ts
)
SELECT blocks.ts, block_count, tx_count, gas_used, avg_gas_used_ratio,
  avg_base_fee_gwei, avg_priority_fee_gwei, failed_tx_ratio,
  contract_creation_count, erc20_transfer_count, stablecoin_transfer_volume,
  weth_usdc_swap_count, weth_usdc_swap_volume_usd, weth_usdc_swap_volume_eth,
  eth_price, wbtc_weth_swap_count, wbtc_weth_volume_btc, wbtc_weth_volume_eth,
  btc_eth_price, weth_usdc_buy_eth, weth_usdc_sell_eth, wbtc_weth_buy_eth,
  wbtc_weth_sell_eth
FROM blocks
LEFT JOIN tx USING(ts)
LEFT JOIN transfers USING(ts)
LEFT JOIN weth USING(ts)
LEFT JOIN wbtc USING(ts)
ORDER BY ts
"""


def l2_sql(args, chain, start, end):
    meta = l2_chains[chain]
    stable_case = stablecoin_case("address", "quantity", meta["stablecoins"])
    dataset = meta["dataset"]
    return f"""
WITH tx AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    COUNT(*) AS tx_count,
    SUM(receipt_gas_used) AS gas_used
  FROM `{dataset}.transactions`
  WHERE block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
  GROUP BY ts
), transfers AS (
  SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
    SUM(CAST(({stable_case}) AS FLOAT64)) AS stablecoin_transfer_volume
  FROM `{dataset}.token_transfers`
  WHERE block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
    AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
  GROUP BY ts
)
SELECT tx.ts, {quote_sql(chain)} AS chain, tx_count, gas_used, stablecoin_transfer_volume
FROM tx LEFT JOIN transfers USING(ts)
ORDER BY ts
"""


def btc_sql(args, start, end):
    return f"""
SELECT UNIX_SECONDS(TIMESTAMP_TRUNC(block_timestamp, HOUR)) AS ts,
  COUNT(*) AS tx_count,
  SUM(fee) / 100000000.0 AS fee_total_btc,
  AVG(fee) / 100000000.0 AS fee_per_tx_btc,
  SUM(output_value) / 100000000.0 AS output_value_btc,
  SUM(output_count) AS output_count,
  SUM(input_count) AS input_count
FROM `{args.btc_dataset}.transactions`
WHERE block_timestamp >= TIMESTAMP({quote_sql(bq_time(start))})
  AND block_timestamp < TIMESTAMP({quote_sql(bq_time(end))})
GROUP BY ts
ORDER BY ts
"""


def gdelt_sql(args, start, end):
    return f"""
WITH src AS (
  SELECT
    UNIX_SECONDS(TIMESTAMP_TRUNC(PARSE_TIMESTAMP('%Y%m%d%H%M%S', CAST(DATEADDED AS STRING)), HOUR)) AS ts,
    LOWER(CONCAT(COALESCE(SOURCEURL,''),' ',COALESCE(Actor1Name,''),' ',COALESCE(Actor2Name,''))) AS text,
    CAST(NumMentions AS FLOAT64) AS mentions,
    CAST(AvgTone AS FLOAT64) AS tone
  FROM `{args.gdelt_table}`
  WHERE DATEADDED >= CAST(FORMAT_TIMESTAMP('%Y%m%d%H%M%S', TIMESTAMP({quote_sql(bq_time(start))})) AS INT64)
    AND DATEADDED < CAST(FORMAT_TIMESTAMP('%Y%m%d%H%M%S', TIMESTAMP({quote_sql(bq_time(end))})) AS INT64)
)
SELECT ts,
  SUM(CASE WHEN REGEXP_CONTAINS(text, r'(crypto|bitcoin|ethereum|stablecoin).*(regulat|sec|cftc|ban|sanction)|regulat.*(crypto|bitcoin|ethereum|stablecoin)') THEN mentions ELSE 0 END) AS crypto_regulation_intensity,
  SUM(CASE WHEN REGEXP_CONTAINS(text, r'(exchange|binance|kraken|okx|bybit|ftx).*(stress|halt|withdraw|lawsuit|hack|insolven|bankrupt)|withdraw.*exchange') THEN mentions ELSE 0 END) AS exchange_stress_intensity,
  SUM(CASE WHEN REGEXP_CONTAINS(text, r'(hack|exploit|ransomware|cyber|breach|phishing|malware|security)') THEN mentions ELSE 0 END) AS cyber_security_intensity,
  SUM(CASE WHEN REGEXP_CONTAINS(text, r'(bank|banking|credit|liquidity|deposit|default).*(stress|crisis|risk|run|failure|panic)|bank run') THEN mentions ELSE 0 END) AS banking_stress_intensity,
  SUM(CASE WHEN tone < -5 OR REGEXP_CONTAINS(text, r'(risk-off|selloff|crash|panic|recession|contagion|default|war|conflict)') THEN mentions ELSE 0 END) AS broad_risk_off_intensity
FROM src
GROUP BY ts
ORDER BY ts
"""


def insert_eth(db, rows):
    sql = (
        "INSERT OR REPLACE INTO bq_eth_hourly("
        "ts,block_count,tx_count,gas_used,avg_gas_used_ratio,"
        "avg_base_fee_gwei,avg_priority_fee_gwei,failed_tx_ratio,"
        "contract_creation_count,erc20_transfer_count,stablecoin_transfer_volume,"
        "weth_usdc_swap_count,weth_usdc_swap_volume_usd,weth_usdc_swap_volume_eth,"
        "eth_price,wbtc_weth_swap_count,wbtc_weth_volume_btc,wbtc_weth_volume_eth,"
        "btc_eth_price,weth_usdc_buy_eth,weth_usdc_sell_eth,wbtc_weth_buy_eth,"
        "wbtc_weth_sell_eth) VALUES"
        "(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)"
    )
    for row in rows:
        db.execute(
            sql,
            (
                row_ts(row),
                row_int(row, "block_count"),
                row_int(row, "tx_count"),
                row_float(row, "gas_used"),
                row_float(row, "avg_gas_used_ratio"),
                row_float(row, "avg_base_fee_gwei"),
                row_float(row, "avg_priority_fee_gwei"),
                row_float(row, "failed_tx_ratio"),
                row_int(row, "contract_creation_count"),
                row_int(row, "erc20_transfer_count"),
                row_float(row, "stablecoin_transfer_volume"),
                row_int(row, "weth_usdc_swap_count"),
                row_float(row, "weth_usdc_swap_volume_usd"),
                row_float(row, "weth_usdc_swap_volume_eth"),
                row_float(row, "eth_price"),
                row_int(row, "wbtc_weth_swap_count"),
                row_float(row, "wbtc_weth_volume_btc"),
                row_float(row, "wbtc_weth_volume_eth"),
                row_float(row, "btc_eth_price"),
                row_float(row, "weth_usdc_buy_eth"),
                row_float(row, "weth_usdc_sell_eth"),
                row_float(row, "wbtc_weth_buy_eth"),
                row_float(row, "wbtc_weth_sell_eth"),
            ),
        )


def insert_l2(db, rows):
    for row in rows:
        db.execute(
            "INSERT OR REPLACE INTO bq_l2_hourly VALUES(?,?,?,?,?)",
            (
                row_ts(row),
                row["chain"],
                row_int(row, "tx_count"),
                row_float(row, "gas_used"),
                row_float(row, "stablecoin_transfer_volume"),
            ),
        )


def insert_btc(db, rows):
    for row in rows:
        db.execute(
            "INSERT OR REPLACE INTO bq_btc_hourly VALUES(?,?,?,?,?,?,?)",
            (
                row_ts(row),
                row_int(row, "tx_count"),
                row_float(row, "fee_total_btc"),
                row_float(row, "fee_per_tx_btc"),
                row_float(row, "output_value_btc"),
                row_int(row, "output_count"),
                row_int(row, "input_count"),
            ),
        )


def insert_gdelt(db, rows):
    for row in rows:
        db.execute(
            "INSERT OR REPLACE INTO bq_gdelt_hourly VALUES(?,?,?,?,?,?)",
            (
                row_ts(row),
                row_float(row, "crypto_regulation_intensity"),
                row_float(row, "exchange_stress_intensity"),
                row_float(row, "cyber_security_intensity"),
                row_float(row, "banking_stress_intensity"),
                row_float(row, "broad_risk_off_intensity"),
            ),
        )


def jobs(args, start, end):
    source = args.source
    if source in ("all", "eth"):
        yield "eth", eth_sql(args, start, end), insert_eth
    if source in ("all", "l2"):
        for chain in sorted(l2_chains):
            yield f"l2_{chain}", l2_sql(args, chain, start, end), insert_l2
    if source in ("all", "btc"):
        yield "btc", btc_sql(args, start, end), insert_btc
    if source in ("all", "gdelt"):
        yield "gdelt", gdelt_sql(args, start, end), insert_gdelt


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("db_path")
    parser.add_argument("start")
    parser.add_argument("end")
    parser.add_argument("--source", choices=("all", "eth", "l2", "btc", "gdelt"), default="all")
    parser.add_argument("--project", default=os.getenv("BIGQUERY_PROJECT"))
    parser.add_argument("--location", default=os.getenv("BIGQUERY_LOCATION", "US"))
    parser.add_argument("--maximum-bytes-billed", type=int, default=25_000_000_000)
    parser.add_argument("--max-rows", type=int, default=100_000_000)
    parser.add_argument("--bq", default=os.getenv("BQ_BIN", "bq"))
    parser.add_argument("--eth-dataset", default=os.getenv("ETH_BQ_DATASET", eth_dataset))
    parser.add_argument("--dex-dataset", default=os.getenv("DEX_BQ_DATASET", dex_dataset))
    parser.add_argument("--btc-dataset", default=os.getenv("BTC_BQ_DATASET", btc_dataset))
    parser.add_argument("--gdelt-table", default=os.getenv("GDELT_BQ_TABLE", gdelt_table))
    parser.add_argument("--weth-usdc-pool", default=weth_usdc_pool)
    parser.add_argument("--wbtc-weth-pool", default=wbtc_weth_pool)
    parser.add_argument("--print-sql", action="store_true")
    parser.add_argument("--run", action="store_true")
    args = parser.parse_args()

    if args.project is None or args.project == "":
        return die("set --project or BIGQUERY_PROJECT")
    if shutil.which(args.bq) is None:
        return die("bq command not found; install and authenticate the Google Cloud SDK")

    start = parse_time(args.start)
    end = parse_time(args.end)
    if start >= end:
        return die("start must be before end")

    db = sqlite3.connect(args.db_path)
    try:
        ensure_schema(db)
        for name, sql, insert_fn in jobs(args, start, end):
            if args.print_sql:
                print(f"-- {name}\n{sql}")
            dry = run_bq(args, sql, True)
            if dry.returncode != 0:
                print(dry.stderr, file=sys.stderr)
                return die(f"dry run failed for {name}")
            print(dry.stderr.strip() or dry.stdout.strip(), file=sys.stderr)

            if not args.run:
                continue

            got = run_bq(args, sql, False)
            if got.returncode != 0:
                print(got.stderr, file=sys.stderr)
                return die(f"query failed for {name}")
            rows = load_rows(got.stdout)
            with db:
                insert_fn(db, rows)
                create_feature_view(db)
            print(f"bq-feature-import: {name} rows={len(rows)}", file=sys.stderr)
    finally:
        db.close()

    if not args.run:
        print("dry_run_only=1; pass --run to import")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
