#include <curl/curl.h>
#include <sqlite3.h>

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include <string>
#include <utility>
#include <vector>

#include "dex.hpp"

namespace {

static const char *weth_usdc_pool = "0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640";
static const char *swap_topic = "0xc42079f94a6350d7e6235f29174924f928cc2ac818eb64fed8004e115fbcca67";

static size_t http_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata)
{
    std::string *buf = static_cast<std::string *>(userdata);
    const size_t n = size * nmemb;

    if (buf == NULL) {
        return 0U;
    }

    buf->append(ptr, n);
    return n;
}

static mars_status_t http_post_json(const char *url, const std::string &payload, std::string *out)
{
    CURL *curl;
    CURLcode rc;
    struct curl_slist *headers = NULL;
    long code = 0L;
    std::string buf;

    if ((url == NULL) || (out == NULL)) {
        return MARS_ERR_ARG;
    }

    curl = curl_easy_init();
    if (curl == NULL) {
        return MARS_ERR_MEM;
    }

    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (headers == NULL) {
        curl_easy_cleanup(curl);
        return MARS_ERR_MEM;
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
        return MARS_ERR_IO;
    }

    *out = buf;
    return MARS_OK;
}

static uint64_t env_u64(const char *name, uint64_t fallback, uint64_t lo, uint64_t hi)
{
    const char *s = getenv(name);
    char *endp = NULL;
    unsigned long long v;

    if ((s == NULL) || (s[0] == '\0')) {
        return fallback;
    }

    errno = 0;
    v = strtoull(s, &endp, 10);
    if ((errno != 0) || (endp == s) || (*endp != '\0') || (v < lo) || (v > hi)) {
        return fallback;
    }
    return (uint64_t)v;
}

static mars_status_t sql_exec(sqlite3 *db, const char *sql)
{
    char *err = NULL;
    int rc;

    if ((db == NULL) || (sql == NULL)) {
        return MARS_ERR_ARG;
    }

    rc = sqlite3_exec(db, sql, NULL, NULL, &err);
    if (rc != SQLITE_OK) {
        if (err != NULL) {
            (void)fprintf(stderr, "sqlite: %s\n", err);
            sqlite3_free(err);
        }
        return MARS_ERR_IO;
    }

    return MARS_OK;
}

static mars_status_t open_db(const char *path, sqlite3 **db_out)
{
    sqlite3 *db = NULL;
    int rc;

    if ((path == NULL) || (db_out == NULL)) {
        return MARS_ERR_ARG;
    }

    rc = sqlite3_open(path, &db);
    if (rc != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return MARS_ERR_IO;
    }

    *db_out = db;
    return MARS_OK;
}

static mars_status_t sqlite_step_done(sqlite3_stmt *stmt)
{
    const int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE) {
        return MARS_ERR_IO;
    }

    (void)sqlite3_reset(stmt);
    (void)sqlite3_clear_bindings(stmt);
    return MARS_OK;
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

static char hex_char(int v)
{
    static const char *hex = "0123456789abcdef";

    if ((v < 0) || (v > 15)) {
        return '0';
    }
    return hex[v];
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

static long double hex_to_long_double(const std::string &s)
{
    long double v = 0.0L;
    size_t i = 0U;

    if ((s.size() >= 2U) && (s[0] == '0') && ((s[1] == 'x') || (s[1] == 'X'))) {
        i = 2U;
    }

    for (; i < s.size(); ++i) {
        const int d = hex_digit(s[i]);
        if (d < 0) {
            break;
        }
        v = (v * 16.0L) + (long double)d;
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

static int json_bool_range(const std::string &s, const char *key, size_t lo, size_t hi)
{
    std::string needle;
    size_t p;

    if ((key == NULL) || (lo >= s.size())) {
        return 0;
    }
    if (hi > s.size()) {
        hi = s.size();
    }

    needle = std::string("\"") + key + "\"";
    p = s.find(needle, lo);
    if ((p == std::string::npos) || (p >= hi)) {
        return 0;
    }
    p = s.find(':', p + needle.size());
    if ((p == std::string::npos) || (p >= hi)) {
        return 0;
    }
    ++p;
    while ((p < hi) && (isspace((unsigned char)s[p]) != 0)) {
        ++p;
    }

    return ((p + 4U) <= hi) && (s.compare(p, 4U, "true") == 0);
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

static std::string block_hex(uint64_t n)
{
    char buf[64];

    (void)snprintf(buf, sizeof(buf), "0x%llx", (unsigned long long)n);
    return std::string(buf);
}

static mars_status_t eth_rpc(const char *rpc_url, const std::string &method,
                             const std::string &params, std::string *json)
{
    std::string payload;

    if ((rpc_url == NULL) || (json == NULL)) {
        return MARS_ERR_ARG;
    }

    payload = "{\"jsonrpc\":\"2.0\",\"method\":\"" + method +
              "\",\"params\":" + params + ",\"id\":1}";
    return http_post_json(rpc_url, payload, json);
}

static mars_status_t eth_latest_block(const char *rpc_url, uint64_t *out)
{
    std::string json;
    std::string result;
    mars_status_t st;

    if (out == NULL) {
        return MARS_ERR_ARG;
    }

    st = eth_rpc(rpc_url, "eth_blockNumber", "[]", &json);
    if (st != MARS_OK) {
        return st;
    }
    result = json_string(json, "result");
    if (result.empty()) {
        return MARS_ERR_PARSE;
    }

    *out = hex_to_u64_sat(result);
    return MARS_OK;
}

static std::string lower_ascii(const char *s)
{
    std::string out;
    const unsigned char *p;

    if ((s == NULL) || (s[0] == '\0')) {
        return std::string(weth_usdc_pool);
    }

    for (p = (const unsigned char *)s; *p != 0U; ++p) {
        out.push_back((char)tolower(*p));
    }
    return out;
}

static int valid_addr(const std::string &s)
{
    size_t i;

    if ((s.size() != 42U) || (s[0] != '0') || (s[1] != 'x')) {
        return 0;
    }
    for (i = 2U; i < s.size(); ++i) {
        if (hex_digit(s[i]) < 0) {
            return 0;
        }
    }
    return 1;
}

static std::string word_at(const std::string &data, size_t index)
{
    const size_t off = 2U + (index * 64U);

    if ((data.size() < 2U) || (data[0] != '0') || ((data[1] != 'x') && (data[1] != 'X'))) {
        return std::string();
    }
    if ((off + 64U) > data.size()) {
        return std::string();
    }
    return data.substr(off, 64U);
}

static int word_negative(const std::string &word)
{
    const int d = word.empty() ? -1 : hex_digit(word[0]);

    return (d >= 8) ? 1 : 0;
}

static std::string word_abs_hex(const std::string &word)
{
    std::string out(word.size(), '0');
    int carry = 1;
    size_t i;

    if (word_negative(word) == 0) {
        return word;
    }

    i = word.size();
    while (i > 0U) {
        const int d = hex_digit(word[i - 1U]);
        int v;

        if (d < 0) {
            return std::string();
        }
        v = 15 - d + carry;
        if (v >= 16) {
            v -= 16;
            carry = 1;
        } else {
            carry = 0;
        }
        out[i - 1U] = hex_char(v);
        --i;
    }

    return out;
}

static double word_abs_double(const std::string &word)
{
    const std::string mag = word_abs_hex(word);

    if (mag.empty()) {
        return 0.0;
    }
    return hex_to_double(mag);
}

static int word_signed_i32(const std::string &word)
{
    const double mag = word_abs_double(word);
    int out;

    if (mag > 2147483647.0) {
        return 0;
    }
    out = (int)mag;
    return (word_negative(word) != 0) ? -out : out;
}

static double price_from_sqrt_word(const std::string &word)
{
    const long double sqrt_raw = hex_to_long_double(word);
    const long double q96 = ldexpl(1.0L, 96);
    const long double ratio = (sqrt_raw / q96) * (sqrt_raw / q96);
    const long double price = (ratio > 0.0L) ? ((1.0L / ratio) * 1.0e12L) : 0.0L;

    if ((price <= 0.0L) || (isfinite((double)price) == 0)) {
        return 0.0;
    }
    return (double)price;
}

static int block_ts(sqlite3 *db, uint64_t block, uint64_t *ts)
{
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if ((db == NULL) || (ts == NULL)) {
        return 0;
    }
    if (sqlite3_prepare_v2(db, "SELECT ts FROM eth_blocks WHERE number=?", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    (void)sqlite3_bind_int64(stmt, 1, (sqlite3_int64)block);
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        *ts = (uint64_t)sqlite3_column_int64(stmt, 0);
        found = 1;
    }
    sqlite3_finalize(stmt);
    return found;
}

static int get_state(sqlite3 *db, const std::string &key, uint64_t *v);

static mars_status_t set_state(sqlite3 *db, const std::string &key, uint64_t v)
{
    sqlite3_stmt *stmt = NULL;
    char buf[64];
    uint64_t prev = 0U;
    mars_status_t st;

    if (db == NULL) {
        return MARS_ERR_ARG;
    }
    if ((get_state(db, key, &prev) != 0) && (prev > v)) {
        v = prev;
    }

    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO ingest_state(source,last_key,updated_at) VALUES(?,?,strftime('%s','now'))",
        -1, &stmt, NULL) != SQLITE_OK) {
        return MARS_ERR_IO;
    }

    (void)snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
    (void)sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(stmt, 2, buf, -1, SQLITE_TRANSIENT);
    st = sqlite_step_done(stmt);
    sqlite3_finalize(stmt);
    return st;
}

static int get_state(sqlite3 *db, const std::string &key, uint64_t *v)
{
    sqlite3_stmt *stmt = NULL;
    int found = 0;

    if ((db == NULL) || (v == NULL)) {
        return 0;
    }

    if (sqlite3_prepare_v2(db, "SELECT last_key FROM ingest_state WHERE source=?", -1, &stmt, NULL) != SQLITE_OK) {
        return 0;
    }
    (void)sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
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

static mars_status_t store_log(sqlite3 *db, sqlite3_stmt *stmt, const std::string &pool,
                               const std::string &json, size_t lo, size_t hi)
{
    const std::string data = json_string_range(json, "data", lo, hi);
    const std::string block_hex_s = json_string_range(json, "blockNumber", lo, hi);
    const std::string log_hex_s = json_string_range(json, "logIndex", lo, hi);
    const std::string tx_hash = json_string_range(json, "transactionHash", lo, hi);
    const std::string ts_hex = json_string_range(json, "blockTimestamp", lo, hi);
    const std::string amount0_word = word_at(data, 0U);
    const std::string amount1_word = word_at(data, 1U);
    const std::string sqrt_word = word_at(data, 2U);
    const std::string liq_word = word_at(data, 3U);
    const std::string tick_word = word_at(data, 4U);
    const uint64_t block = hex_to_u64_sat(block_hex_s);
    const uint64_t log_index = hex_to_u64_sat(log_hex_s);
    const double quote_amount = word_abs_double(amount0_word) / 1.0e6;
    const double base_amount = word_abs_double(amount1_word) / 1.0e18;
    const double price = price_from_sqrt_word(sqrt_word);
    uint64_t ts = ts_hex.empty() ? 0U : hex_to_u64_sat(ts_hex);
    mars_status_t st;

    if (json_bool_range(json, "removed", lo, hi) != 0) {
        return MARS_OK;
    }
    if (tx_hash.empty() || data.empty() || amount0_word.empty() || amount1_word.empty() ||
        sqrt_word.empty() || liq_word.empty() || tick_word.empty() || (price <= 0.0)) {
        return MARS_ERR_PARSE;
    }
    if ((quote_amount <= 0.0) || (base_amount <= 0.0)) {
        return MARS_OK;
    }
    if ((ts == 0U) && (block_ts(db, block, &ts) == 0)) {
        (void)fprintf(stderr, "dex-update: missing timestamp for block %llu; run eth-update first\n",
                      (unsigned long long)block);
        return MARS_ERR_STATE;
    }

    (void)sqlite3_bind_text(stmt, 1, pool.c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(stmt, 2, (sqlite3_int64)block);
    (void)sqlite3_bind_int64(stmt, 3, (sqlite3_int64)log_index);
    (void)sqlite3_bind_text(stmt, 4, tx_hash.c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int64(stmt, 5, (sqlite3_int64)ts);
    (void)sqlite3_bind_double(stmt, 6, price);
    (void)sqlite3_bind_double(stmt, 7, base_amount);
    (void)sqlite3_bind_double(stmt, 8, quote_amount);
    (void)sqlite3_bind_text(stmt, 9, ("0x" + sqrt_word).c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(stmt, 10, ("0x" + liq_word).c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_int(stmt, 11, word_signed_i32(tick_word));
    (void)sqlite3_bind_text(stmt, 12, ("0x" + amount0_word).c_str(), -1, SQLITE_TRANSIENT);
    (void)sqlite3_bind_text(stmt, 13, ("0x" + amount1_word).c_str(), -1, SQLITE_TRANSIENT);
    st = sqlite_step_done(stmt);
    return st;
}

static mars_status_t store_logs(sqlite3 *db, sqlite3_stmt *stmt, const std::string &pool,
                                const std::string &json, size_t *n_out)
{
    std::vector<std::pair<size_t, size_t> > objs;
    size_t lo = 0U;
    size_t hi = 0U;
    size_t i;
    mars_status_t st;

    if (n_out != NULL) {
        *n_out = 0U;
    }
    if (json_array_range(json, "result", &lo, &hi) == 0) {
        return MARS_ERR_PARSE;
    }
    collect_json_objects(json, lo, hi, &objs);

    for (i = 0U; i < objs.size(); ++i) {
        st = store_log(db, stmt, pool, json, objs[i].first, objs[i].second);
        if (st != MARS_OK) {
            return st;
        }
    }
    if (n_out != NULL) {
        *n_out = objs.size();
    }
    return MARS_OK;
}

static std::string logs_param(const std::string &pool, uint64_t from_block, uint64_t to_block)
{
    return "[{\"address\":\"" + pool + "\",\"fromBlock\":\"" + block_hex(from_block) +
           "\",\"toBlock\":\"" + block_hex(to_block) + "\",\"topics\":[\"" +
           swap_topic + "\"]}]";
}

static mars_status_t export_query(sqlite3 *db, const char *sql, FILE *fp)
{
    sqlite3_stmt *stmt = NULL;
    int rc;
    int ncol;
    int i;

    if ((db == NULL) || (sql == NULL) || (fp == NULL)) {
        return MARS_ERR_ARG;
    }

    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK) {
        return MARS_ERR_IO;
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
    return (rc == SQLITE_DONE) ? MARS_OK : MARS_ERR_IO;
}

} /* namespace */

mars_status_t dex::schema(sqlite3 *db)
{
    static const char *sql =
        "CREATE TABLE IF NOT EXISTS ingest_state("
        " source TEXT PRIMARY KEY,"
        " last_key TEXT NOT NULL,"
        " updated_at INTEGER NOT NULL"
        ");"
        "CREATE TABLE IF NOT EXISTS dex_swaps("
        " pool TEXT NOT NULL,"
        " block_number INTEGER NOT NULL,"
        " log_index INTEGER NOT NULL,"
        " tx_hash TEXT NOT NULL,"
        " ts INTEGER NOT NULL,"
        " price REAL NOT NULL,"
        " base_amount REAL NOT NULL,"
        " quote_amount REAL NOT NULL,"
        " sqrt_price_x96 TEXT NOT NULL,"
        " liquidity TEXT NOT NULL,"
        " tick INTEGER NOT NULL,"
        " amount0_raw TEXT NOT NULL,"
        " amount1_raw TEXT NOT NULL,"
        " PRIMARY KEY(pool,block_number,log_index)"
        ");"
        "CREATE INDEX IF NOT EXISTS dex_swaps_ts_idx ON dex_swaps(ts);"
        "CREATE INDEX IF NOT EXISTS dex_swaps_pool_ts_idx ON dex_swaps(pool,ts);";

    static const char *view_sql =
        "DROP VIEW IF EXISTS dex_training_bars;"
        "CREATE VIEW dex_training_bars AS "
        "WITH agg AS ("
        " SELECT (s.ts / 60) * 60 AS ts,"
        "  count(*) AS swap_count,"
        "  sum(s.base_amount) AS base_amount,"
        "  sum(s.quote_amount) AS quote_amount "
        " FROM dex_swaps s "
        " WHERE s.pool='0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640' "
        " AND s.price > 0.0 "
        " AND s.base_amount > 0.0 "
        " AND s.quote_amount > 0.0 "
        " GROUP BY (s.ts / 60)"
        "), last AS ("
        " SELECT (s.ts / 60) * 60 AS ts,"
        "  max((s.block_number * 1000000) + s.log_index) AS last_key "
        " FROM dex_swaps s "
        " WHERE s.pool='0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640' "
        " AND s.price > 0.0 "
        " AND s.base_amount > 0.0 "
        " AND s.quote_amount > 0.0 "
        " GROUP BY (s.ts / 60)"
        "), close AS ("
        " SELECT l.ts AS ts,s.block_number AS block_number,"
        "  s.log_index AS log_index,s.price AS price "
        " FROM last l "
        " JOIN dex_swaps s ON ((s.block_number * 1000000) + s.log_index)=l.last_key "
        " WHERE s.pool='0x88e6a0c2ddd26feeb64f039a2c41296fcb3f5640'"
        ") "
        "SELECT a.ts AS ts,"
        " c.block_number AS block_number,"
        " c.log_index AS log_index,"
        " c.price * (1.0 - 0.00005) AS bid,"
        " c.price * (1.0 + 0.00005) AS ask,"
        " a.base_amount AS bid_sz,"
        " a.base_amount AS ask_sz,"
        " a.quote_amount AS volume,"
        " coalesce((1.0 * e.gas_used) / nullif(b.gas_limit,0),0.0) AS gas_util,"
        " a.swap_count AS tx_count,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='DGS2' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS dgs2,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='DGS10' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS dgs10,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='T10Y2Y' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS t10y2y,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='VIXCLS' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS vixcls,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='DTWEXBGS' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS dtwexbgs,"
        " coalesce((SELECT value_real FROM fred_observations f "
        "  WHERE f.series_id='WALCL' AND f.value_real IS NOT NULL "
        "  AND f.date <= date(a.ts,'unixepoch') ORDER BY f.date DESC LIMIT 1),0.0) AS walcl "
        "FROM agg a "
        "JOIN close c ON c.ts=a.ts "
        "LEFT JOIN eth_blocks b ON b.number=c.block_number "
        "LEFT JOIN eth_block_features e ON e.block_number=c.block_number;";

    mars_status_t st;

    st = sql_exec(db, sql);
    if (st != MARS_OK) {
        return st;
    }
    return sql_exec(db, view_sql);
}

mars_status_t dex::update(const char *db_path, const char *rpc_url,
                          uint64_t from_block, uint64_t to_block,
                          const char *pool_arg)
{
    sqlite3 *db = NULL;
    sqlite3_stmt *stmt = NULL;
    std::string pool = lower_ascii(pool_arg);
    std::string state_key = "dex_last_block_" + pool;
    uint64_t latest = 0U;
    uint64_t n;
    uint64_t done = 0U;
    const uint64_t batch_size = env_u64("MARS_DEX_BLOCK_BATCH", 2000U, 1U, 10000U);
    mars_status_t st;

    if ((db_path == NULL) || (rpc_url == NULL)) {
        return MARS_ERR_ARG;
    }
    if (valid_addr(pool) == 0) {
        return MARS_ERR_ARG;
    }

    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK) {
        return MARS_ERR_IO;
    }

    st = open_db(db_path, &db);
    if (st != MARS_OK) {
        curl_global_cleanup();
        return st;
    }
    st = dex::schema(db);
    if (st != MARS_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    if (to_block == MARS_ETH_LATEST) {
        st = eth_latest_block(rpc_url, &latest);
        if (st != MARS_OK) {
            sqlite3_close(db);
            curl_global_cleanup();
            return st;
        }
        to_block = latest;
    }

    if (from_block == 0U) {
        uint64_t last = 0U;
        if (get_state(db, state_key, &last) != 0) {
            from_block = last + 1U;
        }
    }

    if (from_block > to_block) {
        sqlite3_close(db);
        curl_global_cleanup();
        return MARS_OK;
    }

    if (sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO dex_swaps(pool,block_number,log_index,tx_hash,ts,price,base_amount,quote_amount,sqrt_price_x96,liquidity,tick,amount0_raw,amount1_raw) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &stmt, NULL) != SQLITE_OK) {
        sqlite3_close(db);
        curl_global_cleanup();
        return MARS_ERR_IO;
    }

    st = sql_exec(db, "BEGIN IMMEDIATE");
    if (st != MARS_OK) {
        sqlite3_finalize(stmt);
        sqlite3_close(db);
        curl_global_cleanup();
        return st;
    }

    for (n = from_block; n <= to_block;) {
        const uint64_t remaining = to_block - n + 1U;
        const uint64_t step = (remaining > batch_size) ? batch_size : remaining;
        const uint64_t batch_to = n + step - 1U;
        std::string json;
        size_t logs = 0U;

        st = eth_rpc(rpc_url, "eth_getLogs", logs_param(pool, n, batch_to), &json);
        if (st != MARS_OK) {
            break;
        }
        st = store_logs(db, stmt, pool, json, &logs);
        if (st != MARS_OK) {
            break;
        }
        st = set_state(db, state_key, batch_to);
        if (st != MARS_OK) {
            break;
        }

        done += step;
        if ((done % 10000U) == 0U || batch_to == to_block) {
            st = sql_exec(db, "COMMIT; BEGIN IMMEDIATE");
            if (st != MARS_OK) {
                break;
            }
            (void)fprintf(stderr, "dex-update: stored through block %llu logs=%llu\n",
                          (unsigned long long)batch_to,
                          (unsigned long long)logs);
        }

        if (batch_to == UINT64_MAX) {
            break;
        }
        n = batch_to + 1U;
    }

    if (st == MARS_OK) {
        st = sql_exec(db, "COMMIT");
    } else {
        (void)sql_exec(db, "ROLLBACK");
    }

    sqlite3_finalize(stmt);
    sqlite3_close(db);
    curl_global_cleanup();
    return st;
}

mars_status_t dex::export_csv(const char *db_path, const char *out_path, const char *pool_arg)
{
    sqlite3 *db = NULL;
    FILE *fp;
    mars_status_t st;
    const std::string pool = lower_ascii(pool_arg);
    const std::string sql =
        "SELECT ts,block_number,log_index,price,base_amount,quote_amount,tick "
        "FROM dex_swaps WHERE pool='" + pool + "' ORDER BY block_number,log_index";

    if ((db_path == NULL) || (out_path == NULL)) {
        return MARS_ERR_ARG;
    }
    if (valid_addr(pool) == 0) {
        return MARS_ERR_ARG;
    }

    st = open_db(db_path, &db);
    if (st != MARS_OK) {
        return st;
    }
    fp = fopen(out_path, "w");
    if (fp == NULL) {
        sqlite3_close(db);
        return MARS_ERR_IO;
    }
    st = export_query(db, sql.c_str(), fp);
    if (fclose(fp) != 0) {
        st = MARS_ERR_IO;
    }
    sqlite3_close(db);
    return st;
}

extern "C" mars_status_t mars_dex_update(const char *db_path, const char *rpc_url,
                                         uint64_t from_block, uint64_t to_block,
                                         const char *pool)
{
    return dex::update(db_path, rpc_url, from_block, to_block, pool);
}

extern "C" mars_status_t mars_dex_export(const char *db_path, const char *out_path,
                                         const char *pool)
{
    return dex::export_csv(db_path, out_path, pool);
}
