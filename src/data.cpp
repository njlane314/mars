#include <sqlite3.h>

#include <ctype.h>
#include <math.h>
#include <stdlib.h>

#include <string>

#include "data.hpp"

namespace {

static const char *default_table(const char *table)
{
    if ((table == NULL) || (table[0] == '\0')) {
        return "market_bars";
    }
    return table;
}


static int valid_ident(const char *s)
{
    const unsigned char first = (s == NULL) ? 0U : (unsigned char)s[0];
    const char *p;

    if (first == 0U) {
        return 0;
    }
    if ((isalpha(first) == 0) && (first != '_')) {
        return 0;
    }

    for (p = s + 1; *p != '\0'; ++p) {
        const unsigned char c = (unsigned char)*p;
        if ((isalnum(c) == 0) && (c != '_')) {
            return 0;
        }
    }
    return 1;
}


static std::string quote_ident(const char *s)
{
    return std::string("\"") + s + "\"";
}


static mars_status_t open_db(const char *path, int flags, sqlite3 **db_out)
{
    sqlite3 *db = NULL;
    int rc;

    if ((path == NULL) || (db_out == NULL)) {
        return MARS_ERR_ARG;
    }

    rc = sqlite3_open_v2(path, &db, flags, NULL);
    if (rc != SQLITE_OK) {
        if (db != NULL) {
            sqlite3_close(db);
        }
        return MARS_ERR_IO;
    }

    *db_out = db;
    return MARS_OK;
}


static int row_valid(const mars_row_t *r)
{
    if (r == NULL) {
        return 0;
    }
    if ((r->bid <= 0.0) || (r->ask <= 0.0) || (r->ask < r->bid) ||
        (r->bid_sz < 0.0) || (r->ask_sz < 0.0) || (r->volume < 0.0)) {
        return 0;
    }
    if ((isfinite(r->bid) == 0) || (isfinite(r->ask) == 0) ||
        (isfinite(r->bid_sz) == 0) || (isfinite(r->ask_sz) == 0) ||
        (isfinite(r->volume) == 0)) {
        return 0;
    }
    return 1;
}


static mars_status_t count_sql(sqlite3 *db, const std::string &sql, size_t *n_out)
{
    sqlite3_stmt *stmt = NULL;
    sqlite3_int64 n;

    if ((db == NULL) || (n_out == NULL)) {
        return MARS_ERR_ARG;
    }

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        return MARS_ERR_IO;
    }
    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        return MARS_ERR_IO;
    }

    n = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    if (n < 0) {
        return MARS_ERR_IO;
    }

    *n_out = (size_t)n;
    return MARS_OK;
}


static mars_status_t count_rows(sqlite3 *db, const char *table, size_t *n_out)
{
    if (table == NULL) {
        return MARS_ERR_ARG;
    }
    return count_sql(db, "SELECT count(*) FROM " + quote_ident(table), n_out);
}


static std::string spot_count_sql(void)
{
    return "SELECT count(*) FROM eth_spot_training_bars";
}


static std::string spot_training_sql(void)
{
    return "SELECT ts,bid,ask,bid_sz,ask_sz,volume,"
           "range_pct,body_pct,dgs2,dgs10,t10y2y,vixcls,dtwexbgs,walcl "
           "FROM eth_spot_training_bars ORDER BY ts";
}


static std::string basefee_count_sql(void)
{
    return "SELECT count(*) FROM basefee_training_bars";
}


static std::string basefee_training_sql(void)
{
    return "SELECT ts,bid,ask,bid_sz,ask_sz,volume,"
           "gas_util,tx_count,dgs2,dgs10,t10y2y,vixcls,dtwexbgs,walcl "
           "FROM basefee_training_bars ORDER BY ts";
}


static mars_status_t load_query(sqlite3 *db, const std::string &sql, size_t cap, mars_data_t *d)
{
    sqlite3_stmt *stmt = NULL;
    size_t n = 0U;
    int rc;

    if ((db == NULL) || (d == NULL) || (cap < MARS_MIN_TRAIN_ROWS)) {
        return MARS_ERR_ARG;
    }

    d->row = NULL;
    d->n = 0U;

    d->row = (mars_row_t *)calloc(cap, sizeof(mars_row_t));
    if (d->row == NULL) {
        return MARS_ERR_MEM;
    }

    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, NULL) != SQLITE_OK) {
        data::release(d);
        return MARS_ERR_IO;
    }

    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        mars_row_t *r;
        sqlite3_int64 ts;
        int col;
        int aux;

        if (n >= cap) {
            sqlite3_finalize(stmt);
            data::release(d);
            return MARS_ERR_STATE;
        }

        ts = sqlite3_column_int64(stmt, 0);
        if (ts < 0) {
            sqlite3_finalize(stmt);
            data::release(d);
            return MARS_ERR_PARSE;
        }
        for (col = 1; col <= 5; ++col) {
            if (sqlite3_column_type(stmt, col) == SQLITE_NULL) {
                sqlite3_finalize(stmt);
                data::release(d);
                return MARS_ERR_PARSE;
            }
        }

        r = &d->row[n];
        r->ts = (uint64_t)ts;
        r->bid = sqlite3_column_double(stmt, 1);
        r->ask = sqlite3_column_double(stmt, 2);
        r->bid_sz = sqlite3_column_double(stmt, 3);
        r->ask_sz = sqlite3_column_double(stmt, 4);
        r->volume = sqlite3_column_double(stmt, 5);
        for (aux = 0; aux < (int)MARS_MAX_AUX_FEATURES; ++aux) {
            const int src_col = 6 + aux;
            r->aux[aux] = (sqlite3_column_count(stmt) > src_col) ?
                sqlite3_column_double(stmt, src_col) : 0.0;
            if (isfinite(r->aux[aux]) == 0) {
                r->aux[aux] = 0.0;
            }
        }
        if (row_valid(r) == 0) {
            sqlite3_finalize(stmt);
            data::release(d);
            return MARS_ERR_PARSE;
        }
        ++n;
    }

    sqlite3_finalize(stmt);

    if (rc != SQLITE_DONE) {
        data::release(d);
        return MARS_ERR_IO;
    }
    if (n < MARS_MIN_TRAIN_ROWS) {
        data::release(d);
        return MARS_ERR_STATE;
    }

    d->n = n;
    return MARS_OK;
}

} /* namespace */

mars_status_t data::load_bars(const char *db_path, const char *table_arg, mars_data_t *d)
{
    const char *table = default_table(table_arg);
    sqlite3 *db = NULL;
    size_t cap = 0U;
    mars_status_t st;
    std::string sql;

    if ((db_path == NULL) || (d == NULL) || (valid_ident(table) == 0)) {
        return MARS_ERR_ARG;
    }

    st = open_db(db_path, SQLITE_OPEN_READONLY, &db);
    if (st != MARS_OK) {
        return st;
    }

    st = count_rows(db, table, &cap);
    if (st != MARS_OK) {
        sqlite3_close(db);
        return st;
    }

    if ((cap < MARS_MIN_TRAIN_ROWS) && (table_arg == NULL)) {
        st = count_sql(db, spot_count_sql(), &cap);
        if (st != MARS_OK) {
            sqlite3_close(db);
            return st;
        }
        if (cap >= MARS_MIN_TRAIN_ROWS) {
            sql = spot_training_sql();
        }
    }

    if ((cap < MARS_MIN_TRAIN_ROWS) && (table_arg == NULL)) {
        st = count_sql(db, basefee_count_sql(), &cap);
        if (st != MARS_OK) {
            sqlite3_close(db);
            return st;
        }
        if (cap >= MARS_MIN_TRAIN_ROWS) {
            sql = basefee_training_sql();
        }
    }

    if (sql.empty()) {
        if (cap < MARS_MIN_TRAIN_ROWS) {
            sqlite3_close(db);
            return MARS_ERR_STATE;
        }
        sql = "SELECT ts,bid,ask,bid_sz,ask_sz,volume FROM " +
              quote_ident(table) + " ORDER BY ts";
    }

    st = load_query(db, sql, cap, d);
    sqlite3_close(db);
    return st;
}

void data::release(mars_data_t *d)
{
    if (d != NULL) {
        free(d->row);
        d->row = NULL;
        d->n = 0U;
    }
}
