#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "csv.hpp"

static uint64_t parse_u64(const char *s, int *ok)
{
    char *endp = NULL;
    unsigned long long v;

    errno = 0;
    v = strtoull(s, &endp, 10);
    if ((errno != 0) || (endp == s)) {
        *ok = 0;
        return 0U;
    }
    *ok = 1;
    return (uint64_t)v;
}


static double parse_f64(const char *s, int *ok)
{
    char *endp = NULL;
    double v;

    errno = 0;
    v = strtod(s, &endp);
    if ((errno != 0) || (endp == s) || (!isfinite(v))) {
        *ok = 0;
        return 0.0;
    }
    *ok = 1;
    return v;
}


static int line_is_header(const char *line)
{
    const unsigned char c = (unsigned char)line[0];
    if ((isdigit(c) != 0) || (c == '+') || (c == '-')) {
        return 0;
    }
    return 1;
}


static rt_status_t count_csv_rows(const char *path, size_t *n_out)
{
    FILE *fp;
    char line[RT_MAX_LINE];
    size_t n = 0U;

    if ((path == NULL) || (n_out == NULL)) {
        return RT_ERR_ARG;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        return RT_ERR_IO;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        if ((n == 0U) && (line_is_header(line) != 0)) {
            continue;
        }
        ++n;
    }

    if (ferror(fp) != 0) {
        (void)fclose(fp);
        return RT_ERR_IO;
    }

    if (fclose(fp) != 0) {
        return RT_ERR_IO;
    }

    *n_out = n;
    return RT_OK;
}


static rt_status_t parse_csv_line(char *line, rt_row_t *r)
{
    char *tok;
    double vals[5];
    int ok;
    uint32_t i;

    if ((line == NULL) || (r == NULL)) {
        return RT_ERR_ARG;
    }

    tok = strtok(line, ",\r\n");
    if (tok == NULL) {
        return RT_ERR_PARSE;
    }
    r->ts = parse_u64(tok, &ok);
    if (ok == 0) {
        return RT_ERR_PARSE;
    }

    for (i = 0U; i < 5U; ++i) {
        tok = strtok(NULL, ",\r\n");
        if (tok == NULL) {
            return RT_ERR_PARSE;
        }
        vals[i] = parse_f64(tok, &ok);
        if (ok == 0) {
            return RT_ERR_PARSE;
        }
    }

    r->bid = vals[0];
    r->ask = vals[1];
    r->bid_sz = vals[2];
    r->ask_sz = vals[3];
    r->volume = vals[4];

    if ((r->bid <= 0.0) || (r->ask <= 0.0) || (r->ask < r->bid) ||
        (r->bid_sz < 0.0) || (r->ask_sz < 0.0) || (r->volume < 0.0)) {
        return RT_ERR_PARSE;
    }

    return RT_OK;
}


rt_status_t CsvData::load(const char *path, rt_data_t *d)
{
    FILE *fp;
    char line[RT_MAX_LINE];
    size_t cap;
    size_t n = 0U;
    rt_status_t st;

    if ((path == NULL) || (d == NULL)) {
        return RT_ERR_ARG;
    }

    d->row = NULL;
    d->n = 0U;

    st = count_csv_rows(path, &cap);
    if (st != RT_OK) {
        return st;
    }

    if (cap < RT_MIN_TRAIN_ROWS) {
        return RT_ERR_STATE;
    }

    d->row = (rt_row_t *)calloc(cap, sizeof(rt_row_t));
    if (d->row == NULL) {
        return RT_ERR_MEM;
    }

    fp = fopen(path, "r");
    if (fp == NULL) {
        free(d->row);
        d->row = NULL;
        return RT_ERR_IO;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char tmp[RT_MAX_LINE];

        if (line[0] == '\0' || line[0] == '\n' || line[0] == '#') {
            continue;
        }
        if ((n == 0U) && (line_is_header(line) != 0)) {
            continue;
        }

        if (strlen(line) >= sizeof(tmp)) {
            (void)fclose(fp);
            free(d->row);
            d->row = NULL;
            return RT_ERR_PARSE;
        }

        (void)strncpy(tmp, line, sizeof(tmp));
        tmp[sizeof(tmp) - 1U] = '\0';

        st = parse_csv_line(tmp, &d->row[n]);
        if (st != RT_OK) {
            (void)fclose(fp);
            free(d->row);
            d->row = NULL;
            return st;
        }
        ++n;
    }

    if (ferror(fp) != 0) {
        (void)fclose(fp);
        free(d->row);
        d->row = NULL;
        return RT_ERR_IO;
    }

    if (fclose(fp) != 0) {
        free(d->row);
        d->row = NULL;
        return RT_ERR_IO;
    }

    d->n = n;
    return RT_OK;
}


void CsvData::release(rt_data_t *d)
{
    if (d != NULL) {
        free(d->row);
        d->row = NULL;
        d->n = 0U;
    }
}
