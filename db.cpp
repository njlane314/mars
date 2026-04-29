#include <curl/curl.h>
#include <sqlite3.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <string>
#include <utility>
#include <vector>

#include "mars_api.h"

namespace {

struct HttpBuf final {
    std::string body;
};

static size_t http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    HttpBuf *buf = static_cast<HttpBuf *>(userdata);
    const size_t n = size * nmemb;
    if (buf == NULL) {
        return 0U;
    }
    buf->body.append(ptr, n);
    return n;
}

static rt_status_t http_post_json(const char *url, const std::string &payload, std::string *out)
{
    CURL *curl;
    CURLcode rc;
    struct curl_slist *headers = NULL;
    long code = 0L;
    HttpBuf buf;

    if ((url == NULL) || (out == NULL)) {
        return RT_ERR_ARG;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return RT_ERR_MEM;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers == NULL) {
        curl_easy_cleanup(curl);
        return RT_ERR_MEM;
    }

    (void)curl_easy_setopt(curl, CURLOPT_URL, url);
    (void)curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    (void)curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    (void)curl_easy_setopt(curl, CURLOPT_USERAGENT, "mars/1");
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    (void)curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    rc = curl_easy_perform(curl);
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if ((rc != CURLE_OK) || (code < 200L) || (code >= 300L)) {
        return RT_ERR_IO;
    }

    *out = buf.body;
    return RT_OK;
}

static rt_status_t http_get(const std::string &url, std::string *out)
{
    CURL *curl;
    CURLcode rc;
    long code = 0L;
    HttpBuf buf;

    if (out == NULL) {
        return RT_ERR_ARG;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return RT_ERR_MEM;
    }

    (void)curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    (void)curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, http_write_cb);
    (void)curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buf);
    (void)curl_easy_setopt(curl, CURLOPT_USERAGENT, "mars/1");
    (void)curl_easy_setopt(curl, CURLOPT_TIMEOUT, 90L);
    (void)curl_easy_setopt(curl, CURLOPT_FAILONERROR, 0L);

    rc = curl_easy_perform(curl);
    (void)curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
    curl_easy_cleanup(curl);

    if ((rc != CURLE_OK) || (code < 200L) || (code >= 300L)) {
        return RT_ERR_IO;
    }

    *out = buf.body;
    return RT_OK;
}

static rt_status_t sql_exec(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    int rc;

    if ((db == NULL) || (sql == NULL)) {
        return RT_ERR_ARG;
    }

    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err != NULL) {
            (void)fprintf(stderr, "sqlite: %s\n", err);
            sqlite3_free(err);
        }
        return RT_ERR_IO;
    }

    return RT_OK;
}

static rt_status_t db_open(const char *path, sqlite3 **db_out)
{
    sqlite3 *db = NULL;
    int rc;

    if ((path == NULL) || (db_out == NULL)) {
        return RT_ERR_ARG;
    }

    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return RT_ERR_IO;
    }

    *db_out = db;
    return RT_OK;
}

static rt_status_t db_schema(sqlite3 *db)
{
    static const char *sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA temp_store=MEMORY;"
        "CREATE TABLE IF NOT EXISTS meta("
        " key TEXT PRIMARY KEY,"
        " value TEXT NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS ingest_state("
        " source TEXT PRIMARY KEY,"
        " last_key TEXT NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS eth_blocks("
        " number INTEGER PRIMARY KEY,"
        " hash TEXT NOT NULL,"
        " parent_hash TEXT NOT NULL,"
        " ts INTEGER NOT NULL,"
        " gas_used INTEGER NOT NULL,"
        " gas_limit INTEGER NOT NULL,"
        " base_fee_wei TEXT,"
        " base_fee_gwei REAL,"
        " tx_count INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS eth_txs("
        " hash TEXT PRIMARY KEY,"
        " block_number INTEGER NOT NULL,"
        " tx_index INTEGER NOT NULL,"
        " from_addr TEXT,"
        " to_addr TEXT,"
        " value_wei TEXT,"
        " value_eth REAL,"
        " gas INTEGER,"
        " gas_price_wei TEXT,"
        " max_fee_per_gas_wei TEXT,"
        " max_priority_fee_per_gas_wei TEXT,"
        " input_bytes INTEGER"
        ");"
        "CREATE INDEX IF NOT EXISTS eth_txs_block_idx ON eth_txs(block_number,tx_index);"
        "CREATE TABLE IF NOT EXISTS eth_block_features("
        " block_number INTEGER PRIMARY KEY,"
        " ts INTEGER NOT NULL,"
        " tx_count INTEGER NOT NULL,"
        " gas_used INTEGER NOT NULL,"
        " base_fee_gwei REAL,"
        " eth_value_total REAL NOT NULL,"
        " input_bytes_total INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS fred_series("
        " series_id TEXT PRIMARY KEY,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS fred_observations("
        " series_id TEXT NOT NULL,"
        " date TEXT NOT NULL,"
        " realtime_start TEXT NOT NULL,"
        " realtime_end TEXT NOT NULL,"
        " value_real REAL,"
        " value_text TEXT NOT NULL,"
        " PRIMARY KEY(series_id,date,realtime_start,realtime_end)"
        ");"
        "CREATE INDEX IF NOT EXISTS fred_obs_date_idx ON fred_observations(date);";

    return sql_exec(db, sql);
}

static int hex_digit(char c)
{
    if ((c >= '0') && (c <= '9')) {
        return (int)(c - '0');
    }
    if ((c >= 'a') && (c <= 'f')) {
        return (int)(c - 'a') + 10;
    }
    if ((c >= 'A') && (c <= 'F')) {
        return (int)(c - 'A') + 10;
    }
    return -1;
}

static uint64_t hex_to_u64_sat(const std::string &s)
{
    uint64_t v = 0U;
    size_t i = 0U;

    if ((s.size() >= 2U) && (s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        i = 2U;
    }

    for (; i < s.size(); ++i) {
        const int d = hex_digit(s[i]);
        if (d < 0) {
            break;
        }
        if (v > (UINT64_MAX >> 4U)) {
            return UINT64_MAX;
        }
        v = (v << 4U) | (uint64_t)d;
    }

    return v;
}

static double hex_to_double(const std::string &s)
{
    double v = 0.0;
    size_t i = 0U;

    if ((s.size() >= 2U) && (s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        i = 2U;
    }

    for (; i < s.size(); ++i) {
        const int d = hex_digit(s[i]);
        if (d < 0) {
            break;
        }
        v = (v * 16.0) + (double)d;
    }

    return v;
}

static int is_escaped_quote(const std::string &s, size_t pos)
{
    size_t n = 0U;
    while ((pos > n) && (s[pos - n - 1U] == '\\')) {
        ++n;
    }
    return ((n % 2U) != 0U) ? 1 : 0;
}

static std::string json_string_range(const std::string &s, const char *key, size_t lo, size_t hi)
{
    std::string needle;
    size_t p;
    size_t q;

    if ((key == NULL) || (lo >= s.size())) {
        return std::string();
    }
    if (hi > s.size()) {
        hi = s.size();
    }

    needle = std::string("\"") + key + "\"";
    p = s.find(needle, lo);
    if ((p == std::string::npos) || (p >= hi)) {
        return std::string();
    }
    p = s.find(':', p + needle.size());
    if ((p == std::string::npos) || (p >= hi)) {
        return std::string();
    }
    ++p;
    while ((p < hi) && (isspace((unsigned char)s[p]) != 0)) {
        ++p;
    }
    if ((p >= hi) || (s[p] != '"')) {
        return std::string();
    }
    ++p;
    q = p;
    while (q < hi) {
        if ((s[q] == '"') && (is_escaped_quote(s, q) == 0)) {
            return s.substr(p, q - p);
        }
        ++q;
    }

    return std::string();
}

static std::string json_string(const std::string &s, const char *key)
{
    return json_string_range(s, key, 0U, s.size());
}

static int json_array_range(const std::string &s, const char *key, size_t *lo, size_t *hi)
{
    std::string needle;
    size_t p;
    size_t i;
    int depth = 0;
    int in_str = 0;

    if ((key == NULL) || (lo == NULL) || (hi == NULL)) {
        return 0;
    }

    needle = std::string("\"") + key + "\"";
    p = s.find(needle);
    if (p == std::string::npos) {
        return 0;
    }
    p = s.find('[', p + needle.size());
    if (p == std::string::npos) {
        return 0;
    }

    *lo = p + 1U;
    for (i = p; i < s.size(); ++i) {
        const char c = s[i];
        if (in_str != 0) {
            if ((c == '"') && (is_escaped_quote(s, i) == 0)) {
                in_str = 0;
            }
            continue;
        }
        if (c == '"') {
            in_str = 1;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                *hi = i;
                return 1;
            }
        }
    }

    return 0;
}

static void collect_json_objects(const std::string &s, size_t lo, size_t hi,
                                 std::vector<std::pair<size_t, size_t> > *out)
{
    size_t i;
    size_t start = 0U;
    int depth = 0;
    int in_str = 0;

    if (out == NULL) {
        return;
    }
    if (hi > s.size()) {
        hi = s.size();
    }

    for (i = lo; i < hi; ++i) {
        const char c = s[i];
        if (in_str != 0) {
            if ((c == '"') && (is_escaped_quote(s, i) == 0)) {
                in_str = 0;
            }
            continue;
        }
        if (c == '"') {
            in_str = 1;
        } else if (c == '{') {
            if (depth == 0) {
                start = i;
            }
            ++depth;
        } else if (c == '}') {
            if (depth > 0) {
                --depth;
                if (depth == 0) {
                    out->push_back(std::make_pair(start, i + 1U));
                }
            }
        }
    }
}

static rt_status_t sqlite_step_done(sqlite3_stmt *stmt)
{
    const int rc = sqlite3_step(stmt);
    if (rc != SQLITE_DONE) {
        return RT_ERR_IO;
    }
    (void)sqlite3_reset(stmt);
    (void)sqlite3_clear_bindings(stmt);
    return RT_OK;
}

static void bind_text_or_null(sqlite3_stmt *stmt, int col, const std::string &s)
{
    if (s.empty()) {
        (void)sqlite3_bind_null(stmt, col);
    } else {
        (void)sqlite3_bind_text(stmt, col, s.c_str(), -1, SQLITE_TRANSIENT);
    }
}

static rt_status_t set_state(sqlite3 *db, const char *key, uint64_t v)
{
    sqlite3_stmt *stmt = NULL;
    char buf[64];
    int rc;
    rt_status_t st;

    if ((db == NULL) || (key == NULL)) {
        return RT_ERR_ARG;
    }

    rc = sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO ingest_state(source,last_key,updated_at) VALUES(?,?,strftime('%s','now'))",
        -1, &stmt, NULL);
    if (rc != SQLITE_OK) {
        return RT_ERR_IO;
    }

    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(stmt, 2, buf, -1, SQLITE_TRANSIENT);
    st = sqlite_step_done(stmt);
    sqlite3_finalize(stmt);
    return st;
}

static int get_state_u64(sqlite3 *db, const char *key, uint64_t *v)
{
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if ((db == NULL) || (key == NULL) || (v == NULL)) {
        return 0;
    }

    if (sqlite3_prepare_v2(db, "SELECT last_key FROM ingest_state WHERE source=?", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    (void)sqlite3_bind_text(stmt, 1, key, -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt != NULL) {
            *v = (uint64_t)strtoull((const char *)txt, NULL, 10);
            found = 1;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

static rt_status_t eth_rpc(const char *rpc_url, const std::string &method,
                           const std::string &params, std::string *json)
{
    std::string payload;

    if ((rpc_url == NULL) || (json == NULL)) {
        return RT_ERR_ARG;
    }

    payload = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method +
              "\",\"params\":" + params + ",\"id\":1}";
    return http_post_json(rpc_url, payload, json);
}

static rt_status_t eth_latest_block(const char *rpc_url, uint64_t *out)
{
    std::string json;
    std::string r;
    rt_status_t st;

    if (out == NULL) {
        return RT_ERR_ARG;
    }

    st = eth_rpc(rpc_url, "eth_blockNumber", "[]", &json);
    if (st != RT_OK) {
        return st;
    }
    r = json_string(json, "result");
    if (r.empty()) {
        return RT_ERR_PARSE;
    }
    *out = hex_to_u64_sat(r);
    return RT_OK;
}

static std::string block_param(uint64_t n)
{
    char buf[64];
    (void)snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)n);
    return std::string("[\"") + buf + "\",true]";
}

static rt_status_t store_eth_block(sqlite3_stmt *block_stmt, sqlite3_stmt *feat_stmt,
                                   sqlite3_stmt *tx_stmt, const std::string &json,
                                   uint32_t store_txs)
{
    std::vector<std::pair<size_t, size_t> > objs;
    std::string h;
    std::string parent;
    std::string num_hex;
    std::string ts_hex;
    std::string gas_used_hex;
    std::string gas_limit_hex;
    std::string base_fee_hex;
    size_t tx_lo = 0U;
    size_t tx_hi = 0U;
    uint64_t number;
    uint64_t ts;
    uint64_t gas_used;
    uint64_t gas_limit;
    double base_fee_gwei;
    double total_eth = 0.0;
    uint64_t total_input_bytes = 0U;
    size_t i;
    rt_status_t st;

    if ((block_stmt == NULL) || (feat_stmt == NULL) || json.empty()) {
        return RT_ERR_ARG;
    }

    if (json_array_range(json, "transactions", &tx_lo, &tx_hi) == 0) {
        return RT_ERR_PARSE;
    }

    h = json_string_range(json, "hash", 0U, tx_lo);
    parent = json_string_range(json, "parentHash", 0U, tx_lo);
    num_hex = json_string_range(json, "number", 0U, tx_lo);
    ts_hex = json_string_range(json, "timestamp", 0U, tx_lo);
    gas_used_hex = json_string_range(json, "gasUsed", 0U, tx_lo);
    gas_limit_hex = json_string_range(json, "gasLimit", 0U, tx_lo);
    base_fee_hex = json_string_range(json, "baseFeePerGas", 0U, tx_lo);

    if (h.empty() || parent.empty() || num_hex.empty() || ts_hex.empty()) {
        return RT_ERR_PARSE;
    }

    number = hex_to_u64_sat(num_hex);
    ts = hex_to_u64_sat(ts_hex);
    gas_used = hex_to_u64_sat(gas_used_hex);
    gas_limit = hex_to_u64_sat(gas_limit_hex);
    base_fee_gwei = base_fee_hex.empty() ? 0.0 : (hex_to_double(base_fee_hex) / 1.0e9);

    collect_json_objects(json, tx_lo, tx_hi, &objs);

    if ((store_txs != 0U) && (tx_stmt == NULL)) {
        return RT_ERR_ARG;
    }

    for (i = 0U; i < objs.size(); ++i) {
        const size_t a = objs[i].first;
        const size_t b = objs[i].second;
        const std::string hash = json_string_range(json, "hash", a, b);
        const std::string from_addr = json_string_range(json, "from", a, b);
        const std::string to_addr = json_string_range(json, "to", a, b);
        const std::string value_wei = json_string_range(json, "value", a, b);
        const std::string gas_hex = json_string_range(json, "gas", a, b);
        const std::string gas_price = json_string_range(json, "gasPrice", a, b);
        const std::string max_fee = json_string_range(json, "maxFeePerGas", a, b);
        const std::string max_prio = json_string_range(json, "maxPriorityFeePerGas", a, b);
        const std::string input = json_string_range(json, "input", a, b);
        const std::string txi_hex = json_string_range(json, "transactionIndex", a, b);
        const double value_eth = value_wei.empty() ? 0.0 : (hex_to_double(value_wei) / 1.0e18);
        const uint64_t input_bytes = (input.size() >= 2U) ? ((uint64_t)(input.size() - 2U) / 2U) : 0U;

        total_eth += value_eth;
        total_input_bytes += input_bytes;

        if (store_txs != 0U) {
            if (hash.empty()) {
                return RT_ERR_PARSE;
            }
            (void)sqlite3_bind_text(tx_stmt, 1, hash.c_str(), -1, SQLITE_TRANSIENT);
            (void)sqlite3_bind_int64(tx_stmt, 2, (sqlite3_int64)number);
            (void)sqlite3_bind_int64(tx_stmt, 3, (sqlite3_int64)hex_to_u64_sat(txi_hex));
            bind_text_or_null(tx_stmt, 4, from_addr);
            bind_text_or_null(tx_stmt, 5, to_addr);
            bind_text_or_null(tx_stmt, 6, value_wei);
            (void)sqlite3_bind_double(tx_stmt, 7, value_eth);
            (void)sqlite3_bind_int64(tx_stmt, 8, (sqlite3_int64)hex_to_u64_sat(gas_hex));
            bind_text_or_null(tx_stmt, 9, gas_price);
            bind_text_or_null(tx_stmt, 10, max_fee);
            bind_text_or_null(tx_stmt, 11, max_prio);
            (void)sqlite3_bind_int64(tx_stmt, 12, (sqlite3_int64)input_bytes);
            st = sqlite_step_done(tx_stmt);
            if (st != RT_OK) {
                return st;
            }
        }
    }

    (void)sqlite3_bind_int64(block_stmt, 1, (sqlite3_int64)number);
    (void)sqlite3_bind_text(block_stmt, 2, h.c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(block_stmt, 3, parent.c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(block_stmt, 4, (sqlite3_int64)ts);
    (void)sqlite3_bind_int64(block_stmt, 5, (sqlite3_int64)gas_used);
    (void)sqlite3_bind_int64(block_stmt, 6, (sqlite3_int64)gas_limit);
    bind_text_or_null(block_stmt, 7, base_fee_hex);
    (void)sqlite3_bind_double(block_stmt, 8, base_fee_gwei);
    (void)sqlite3_bind_int64(block_stmt, 9, (sqlite3_int64)objs.size());
    st = sqlite_step_done(block_stmt);
    if (st != RT_OK) {
        return st;
    }

    (void)sqlite3_bind_int64(feat_stmt, 1, (sqlite3_int64)number);
    (void)sqlite3_bind_int64(feat_stmt, 2, (sqlite3_int64)ts);
    (void)sqlite3_bind_int64(feat_stmt, 3, (sqlite3_int64)objs.size());
    (void)sqlite3_bind_int64(feat_stmt, 4, (sqlite3_int64)gas_used);
    (void)sqlite3_bind_double(feat_stmt, 5, base_fee_gwei);
    (void)sqlite3_bind_double(feat_stmt, 6, total_eth);
    (void)sqlite3_bind_int64(feat_stmt, 7, (sqlite3_int64)total_input_bytes);
    return sqlite_step_done(feat_stmt);
}

static std::vector<std::string> split_csv(const char *s)
{
    std::vector<std::string> out;
    std::string cur;
    const char *p;

    if (s == NULL) {
        return out;
    }

    for (p = s; *p != '\0'; ++p) {
        if (*p == ',') {
            if (!cur.empty()) {
                out.push_back(cur);
                cur.clear();
            }
        } else if (isspace((unsigned char)*p) == 0) {
            cur.push_back(*p);
        }
    }
    if (!cur.empty()) {
        out.push_back(cur);
    }
    return out;
}

static std::string fred_last_date(sqlite3 *db, const std::string &series)
{
    sqlite3_stmt *stmt = NULL;
    std::string out;

    if (sqlite3_prepare_v2(db, "SELECT max(date) FROM fred_observations WHERE series_id=?", -1, &stmt, NULL) != SQLITE_OK) {
        return out;
    }
    (void)sqlite3_bind_text(stmt, 1, series.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *txt = sqlite3_column_text(stmt, 0);
        if (txt != NULL) {
            out = (const char *)txt;
        }
    }
    sqlite3_finalize(stmt);
    return out;
}

static rt_status_t store_fred_observations(sqlite3 *db, const std::string &series, const std::string &json)
{
    sqlite3_stmt *obs = NULL;
    sqlite3_stmt *ser = NULL;
    std::vector<std::pair<size_t, size_t> > objs;
    size_t lo = 0U;
    size_t hi = 0U;
    size_t i;
    rt_status_t st = RT_OK;

    if (json_array_range(json, "observations", &lo, &hi) == 0) {
        return RT_ERR_PARSE;
    }
    collect_json_objects(json, lo, hi, &objs);

    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO fred_observations(series_id,date,realtime_start,realtime_end,value_real,value_text) VALUES(?,?,?,?,?,?)",
        -1, &obs, NULL) != SQLITE_OK) {
        return RT_ERR_IO;
    }
    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO fred_series(series_id,updated_at) VALUES(?,strftime('%s','now'))",
        -1, &ser, NULL) != SQLITE_OK) {
        sqlite3_finalize(obs);
        return RT_ERR_IO;
    }

    (void)sqlite3_bind_text(ser, 1, series.c_str(), -1, SQLITE_TRANSIENT);
    st = sqlite_step_done(ser);
    sqlite3_finalize(ser);
    if (st != RT_OK) {
        sqlite3_finalize(obs);
        return st;
    }

    for (i = 0U; i < objs.size(); ++i) {
        const size_t a = objs[i].first;
        const size_t b = objs[i].second;
        const std::string rt0 = json_string_range(json, "realtime_start", a, b);
        const std::string rt1 = json_string_range(json, "realtime_end", a, b);
        const std::string date = json_string_range(json, "date", a, b);
        const std::string val = json_string_range(json, "value", a, b);

        if (date.empty() || rt0.empty() || rt1.empty() || val.empty()) {
            sqlite3_finalize(obs);
            return RT_ERR_PARSE;
        }

        (void)sqlite3_bind_text(obs, 1, series.c_str(), -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(obs, 2, date.c_str(), -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(obs, 3, rt0.c_str(), -1, SQLITE_TRANSIENT);
        (void)sqlite3_bind_text(obs, 4, rt1.c_str(), -1, SQLITE_TRANSIENT);
        if (val == ".") {
            (void)sqlite3_bind_null(obs, 5);
        } else {
            (void)sqlite3_bind_double(obs, 5, strtod(val.c_str(), NULL));
        }
        (void)sqlite3_bind_text(obs, 6, val.c_str(), -1, SQLITE_TRANSIENT);
        st = sqlite_step_done(obs);
        if (st != RT_OK) {
            sqlite3_finalize(obs);
            return st;
        }
    }

    sqlite3_finalize(obs);
    return RT_OK;
}

static rt_status_t export_query(sqlite3 *db, const char *sql, FILE *fp)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    int ncol;
    int i;

    if ((db == NULL) || (sql == NULL) || (fp == NULL)) {
        return RT_ERR_ARG;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return RT_ERR_IO;
    }

    ncol = sqlite3_column_count(stmt);
    for (i = 0; i < ncol; ++i) {
        if (i > 0) {
            (void)fputc(',', fp);
        }
        (void)fputs(sqlite3_column_name(stmt, i), fp);
    }
    (void)fputc('\n', fp);

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        for (i = 0; i < ncol; ++i) {
            const unsigned char *txt;
            if (i > 0) {
                (void)fputc(',', fp);
            }
            txt = sqlite3_column_text(stmt, i);
            if (txt != NULL) {
                (void)fputs((const char *)txt, fp);
            }
        }
        (void)fputc('\n', fp);
    }

    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE) ? RT_OK : RT_ERR_IO;
}

} /* namespace */

extern "C" rt_status_t mars_db_init(const char *db_path)
{
    sqlite3 *db = NULL;
    rt_status_t st;

    st = db_open(db_path, &db);
    if (st != RT_OK) {
        return st;
    }
    st = db_schema(db);
    sqlite3_close(db);
    return st;
}

extern "C" rt_status_t mars_eth_update(const char *db_path, const char *rpc_url,
                                        uint64_t from_block, uint64_t to_block,
                                        uint32_t store_txs)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *block_stmt = NULL;
    sqlite3_stmt *feat_stmt = NULL;
    sqlite3_stmt *tx_stmt = NULL;
    uint64_t n;
    uint64_t latest = 0U;
    rt_status_t st;

    if ((db_path == NULL) || (rpc_url == NULL)) {
        return RT_ERR_ARG;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return RT_ERR_IO;
    }

    st = db_open(db_path, &db);
    if (st != RT_OK) {
        curl_global_cleanup();
        return st;
    }
    st = db_schema(db);
    if (st != RT_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    if (to_block == RT_ETH_LATEST) {
        st = eth_latest_block(rpc_url, &latest);
        if (st != RT_OK) {
            sqlite3_close(db);
            curl_global_cleanup();
            return st;
        }
        to_block = latest;
    }

    if (from_block == 0U) {
        uint64_t last = 0U;
        if (get_state_u64(db, "eth_last_block", &last) != 0) {
            from_block = last + 1U;
        }
    }

    if (from_block > to_block) {
        sqlite3_close(db);
        curl_global_cleanup();
        return RT_OK;
    }

    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO eth_blocks(number,hash,parent_hash,ts,gas_used,gas_limit,base_fee_wei,base_fee_gwei,tx_count) VALUES(?,?,?,?,?,?,?,?,?)",
        -1, &block_stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return RT_ERR_IO;
    }
    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO eth_block_features(block_number,ts,tx_count,gas_used,base_fee_gwei,eth_value_total,input_bytes_total) VALUES(?,?,?,?,?,?,?)",
        -1, &feat_stmt, NULL) != SQLITE_OK) {
        sqlite3_finalize(block_stmt);
        sqlite3_close(db);
        curl_global_cleanup();
        return RT_ERR_IO;
    }
    if (store_txs != 0U) {
        if (sqlite3_prepare_v2(db,
            "INSERT OR REPLACE INTO eth_txs(hash,block_number,tx_index,from_addr,to_addr,value_wei,value_eth,gas,gas_price_wei,max_fee_per_gas_wei,max_priority_fee_per_gas_wei,input_bytes) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",
            -1, &tx_stmt, NULL) != SQLITE_OK) {
            sqlite3_finalize(feat_stmt);
            sqlite3_finalize(block_stmt);
            sqlite3_close(db);
            curl_global_cleanup();
            return RT_ERR_IO;
        }
    }

    st = sql_exec(db, "BEGIN IMMEDIATE");
    if (st != RT_OK) {
        sqlite3_finalize(tx_stmt);
        sqlite3_finalize(feat_stmt);
        sqlite3_finalize(block_stmt);
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    for (n = from_block; n <= to_block; ++n) {
        std::string json;
        st = eth_rpc(rpc_url, "eth_getBlockByNumber", block_param(n), &json);
        if (st != RT_OK) {
            break;
        }
        st = store_eth_block(block_stmt, feat_stmt, tx_stmt, json, store_txs);
        if (st != RT_OK) {
            break;
        }
        st = set_state(db, "eth_last_block", n);
        if (st != RT_OK) {
            break;
        }
        if (((n - from_block + 1U) % 100U) == 0U) {
            st = sql_exec(db, "COMMIT; BEGIN IMMEDIATE");
            if (st != RT_OK) {
                break;
            }
            (void)fprintf(stderr, "eth-update: stored block %llu\n", (unsigned long long)n);
        }
        if (n == UINT64_MAX) {
            break;
        }
    }

    if (st == RT_OK) {
        st = sql_exec(db, "COMMIT");
    } else {
        (void)sql_exec(db, "ROLLBACK");
    }

    sqlite3_finalize(tx_stmt);
    sqlite3_finalize(feat_stmt);
    sqlite3_finalize(block_stmt);
    sqlite3_close(db);
    curl_global_cleanup();
    return st;
}

extern "C" rt_status_t mars_eth_export(const char *db_path, const char *csv_path)
{
    sqlite3 *db = NULL;
    FILE *fp;
    rt_status_t st;

    if ((db_path == NULL) || (csv_path == NULL)) {
        return RT_ERR_ARG;
    }

    st = db_open(db_path, &db);
    if (st != RT_OK) {
        return st;
    }
    fp = fopen(csv_path, "w");
    if (fp == NULL) {
        sqlite3_close(db);
        return RT_ERR_IO;
    }
    st = export_query(db,
        "SELECT ts,block_number,tx_count,gas_used,base_fee_gwei,eth_value_total,input_bytes_total FROM eth_block_features ORDER BY block_number",
        fp);
    if (fclose(fp) != 0) {
        st = RT_ERR_IO;
    }
    sqlite3_close(db);
    return st;
}

extern "C" rt_status_t mars_fred_update(const char *db_path, const char *series_csv,
                                         const char *api_key)
{
    sqlite3 *db = NULL;
    std::vector<std::string> series;
    size_t i;
    rt_status_t st;

    if ((db_path == NULL) || (series_csv == NULL) || (api_key == NULL)) {
        return RT_ERR_ARG;
    }

    series = split_csv(series_csv);
    if (series.empty()) {
        return RT_ERR_ARG;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return RT_ERR_IO;
    }

    st = db_open(db_path, &db);
    if (st != RT_OK) {
        curl_global_cleanup();
        return st;
    }
    st = db_schema(db);
    if (st != RT_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    st = sql_exec(db, "BEGIN IMMEDIATE");
    if (st != RT_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    for (i = 0U; i < series.size(); ++i) {
        const std::string last = fred_last_date(db, series[i]);
        std::string url = "https://api.stlouisfed.org/fred/series/observations?series_id=" +
                          series[i] + "&api_key=" + api_key +
                          "&file_type=json&sort_order=asc&limit=100000";
        std::string json;
        if (!last.empty()) {
            url += "&observation_start=" + last;
        }
        st = http_get(url, &json);
        if (st != RT_OK) {
            break;
        }
        st = store_fred_observations(db, series[i], json);
        if (st != RT_OK) {
            break;
        }
        (void)fprintf(stderr, "fred-update: stored %s\n", series[i].c_str());
    }

    if (st == RT_OK) {
        st = sql_exec(db, "COMMIT");
    } else {
        (void)sql_exec(db, "ROLLBACK");
    }
    sqlite3_close(db);
    curl_global_cleanup();
    return st;
}

extern "C" rt_status_t mars_fred_export(const char *db_path, const char *csv_path)
{
    sqlite3 *db = NULL;
    FILE *fp;
    rt_status_t st;

    if ((db_path == NULL) || (csv_path == NULL)) {
        return RT_ERR_ARG;
    }

    st = db_open(db_path, &db);
    if (st != RT_OK) {
        return st;
    }
    fp = fopen(csv_path, "w");
    if (fp == NULL) {
        sqlite3_close(db);
        return RT_ERR_IO;
    }
    st = export_query(db,
        "SELECT series_id,date,realtime_start,realtime_end,value_text,value_real FROM fred_observations ORDER BY series_id,date,realtime_start,realtime_end",
        fp);
    if (fclose(fp) != 0) {
        st = RT_ERR_IO;
    }
    sqlite3_close(db);
    return st;
}
