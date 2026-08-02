/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#include "toml_min.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TOML_MAX_FILE_BYTES (1 << 20)   /* 1 MiB; these files are a few hundred bytes */

typedef enum {
    VAL_BARE,      /* number or boolean, kept verbatim and converted on access */
    VAL_STRING     /* quoted basic string, already unescaped                   */
} val_kind;

typedef struct {
    char    *table;
    char    *key;
    char    *val;
    val_kind kind;
    int      line;
} entry;

struct toml_min {
    entry  *e;
    size_t  n, cap;
    char  **tables;      /* every table header seen, in order of appearance */
    size_t  ntables;
};

static void seterr(char *err, size_t errlen, const char *fmt, ...)
{
    if (!err || errlen == 0) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(err, errlen, fmt, ap);
    va_end(ap);
}

/* --- small helpers -------------------------------------------------------- */

static char *xstrndup(const char *s, size_t n)
{
    char *p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static const char *skip_ws(const char *p)
{
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

/* Trims trailing spaces/tabs in place. */
static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) s[--n] = '\0';
}

/* TOML bare keys: A-Z a-z 0-9 _ - */
static int is_bare_key(const char *s)
{
    if (!*s) return 0;
    for (const char *p = s; *p; p++) {
        if (!(isalnum((unsigned char)*p) || *p == '_' || *p == '-')) return 0;
    }
    return 1;
}

static int push_table(toml_min *t, const char *name)
{
    for (size_t i = 0; i < t->ntables; i++) {
        if (strcmp(t->tables[i], name) == 0) return 0;   /* already recorded */
    }
    char **nt = realloc(t->tables, (t->ntables + 1) * sizeof(*nt));
    if (!nt) return -1;
    t->tables = nt;
    t->tables[t->ntables] = xstrndup(name, strlen(name));
    if (!t->tables[t->ntables]) return -1;
    t->ntables++;
    return 0;
}

static int push_entry(toml_min *t, const char *table, const char *key,
                      const char *val, val_kind kind, int line)
{
    if (t->n == t->cap) {
        size_t ncap = t->cap ? t->cap * 2 : 16;
        entry *ne = realloc(t->e, ncap * sizeof(*ne));
        if (!ne) return -1;
        t->e = ne;
        t->cap = ncap;
    }
    entry *e = &t->e[t->n];
    e->table = xstrndup(table, strlen(table));
    e->key   = xstrndup(key, strlen(key));
    e->val   = xstrndup(val, strlen(val));
    e->kind  = kind;
    e->line  = line;
    if (!e->table || !e->key || !e->val) {
        free(e->table); free(e->key); free(e->val);
        return -1;
    }
    t->n++;
    return 0;
}

/* --- value scanning ------------------------------------------------------- */

/* Copies a basic string starting at *p (which points at the opening quote) into
 * a freshly allocated buffer, advancing *p past the closing quote. */
static char *scan_string(const char **p, int line, char *err, size_t errlen)
{
    const char *s = *p + 1;                    /* past the opening quote */
    size_t cap = strlen(s) + 1;
    char *out = malloc(cap);
    if (!out) { seterr(err, errlen, "out of memory"); return NULL; }

    size_t o = 0;
    while (*s && *s != '"') {
        if (*s == '\\') {
            s++;
            switch (*s) {
            case '"':  out[o++] = '"';  break;
            case '\\': out[o++] = '\\'; break;
            case 'n':  out[o++] = '\n'; break;
            case 't':  out[o++] = '\t'; break;
            case 'r':  out[o++] = '\r'; break;
            default:
                free(out);
                seterr(err, errlen, "line %d: unsupported escape \\%c "
                                    "(only \\\" \\\\ \\n \\t \\r are accepted)",
                       line, *s ? *s : '?');
                return NULL;
            }
            s++;
        } else {
            out[o++] = *s++;
        }
    }
    if (*s != '"') {
        free(out);
        seterr(err, errlen, "line %d: unterminated string", line);
        return NULL;
    }
    out[o] = '\0';
    *p = s + 1;                                 /* past the closing quote */
    return out;
}

/* --- parser --------------------------------------------------------------- */

toml_min *toml_min_load(const char *path, char *err, size_t errlen)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        seterr(err, errlen, "cannot open %s: %s", path, strerror(errno));
        return NULL;
    }

    if (fseek(f, 0, SEEK_END) != 0) {
        seterr(err, errlen, "cannot seek %s: %s", path, strerror(errno));
        fclose(f);
        return NULL;
    }
    long size = ftell(f);
    if (size < 0 || size > TOML_MAX_FILE_BYTES) {
        seterr(err, errlen, "%s: implausible size for a configuration file (%ld bytes)",
               path, size);
        fclose(f);
        return NULL;
    }
    rewind(f);

    char *text = malloc((size_t)size + 1);
    if (!text) {
        seterr(err, errlen, "out of memory");
        fclose(f);
        return NULL;
    }
    size_t got = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[got] = '\0';

    if (memchr(text, '\0', got) != NULL) {
        seterr(err, errlen, "%s: not a text file (embedded NUL)", path);
        free(text);
        return NULL;
    }

    toml_min *t = calloc(1, sizeof(*t));
    if (!t) {
        seterr(err, errlen, "out of memory");
        free(text);
        return NULL;
    }

    char table[128] = "";
    int line = 0;
    char *save = text;
    char *ln;

    /* Hand-rolled line walk: strtok_r would collapse consecutive newlines and
     * throw the line numbering off, which is the one thing error messages here
     * have to get right. */
    while (save && *save) {
        ln = save;
        char *nl = strchr(save, '\n');
        if (nl) { *nl = '\0'; save = nl + 1; } else { save = NULL; }
        line++;

        size_t lnlen = strlen(ln);
        if (lnlen > 0 && ln[lnlen - 1] == '\r') ln[lnlen - 1] = '\0';   /* CRLF */

        const char *p = skip_ws(ln);
        if (*p == '\0' || *p == '#') continue;

        /* --- table header --- */
        if (*p == '[') {
            if (p[1] == '[') {
                seterr(err, errlen, "line %d: arrays of tables are not supported", line);
                goto fail;
            }
            p++;
            const char *start = skip_ws(p);
            const char *close = strchr(start, ']');
            if (!close) {
                seterr(err, errlen, "line %d: missing ']' in table header", line);
                goto fail;
            }
            size_t nlen = (size_t)(close - start);
            while (nlen > 0 && (start[nlen - 1] == ' ' || start[nlen - 1] == '\t')) nlen--;
            if (nlen == 0 || nlen >= sizeof(table)) {
                seterr(err, errlen, "line %d: invalid table name", line);
                goto fail;
            }
            memcpy(table, start, nlen);
            table[nlen] = '\0';
            if (strchr(table, '.') != NULL) {
                seterr(err, errlen, "line %d: nested tables ([%s]) are not supported",
                       line, table);
                goto fail;
            }
            if (!is_bare_key(table)) {
                seterr(err, errlen, "line %d: table name '%s' is not a bare name", line, table);
                goto fail;
            }
            const char *rest = skip_ws(close + 1);
            if (*rest != '\0' && *rest != '#') {
                seterr(err, errlen, "line %d: trailing text after table header", line);
                goto fail;
            }
            if (push_table(t, table) != 0) { seterr(err, errlen, "out of memory"); goto fail; }
            continue;
        }

        /* --- key = value --- */
        const char *eq = strchr(p, '=');
        if (!eq) {
            seterr(err, errlen, "line %d: expected 'key = value'", line);
            goto fail;
        }
        size_t klen = (size_t)(eq - p);
        while (klen > 0 && (p[klen - 1] == ' ' || p[klen - 1] == '\t')) klen--;
        char key[128];
        if (klen == 0 || klen >= sizeof(key)) {
            seterr(err, errlen, "line %d: invalid key", line);
            goto fail;
        }
        memcpy(key, p, klen);
        key[klen] = '\0';
        if (strchr(key, '.') != NULL) {
            seterr(err, errlen, "line %d: dotted keys ('%s') are not supported", line, key);
            goto fail;
        }
        if (key[0] == '"' || key[0] == '\'') {
            seterr(err, errlen, "line %d: quoted keys are not supported", line);
            goto fail;
        }
        if (!is_bare_key(key)) {
            seterr(err, errlen, "line %d: key '%s' is not a bare name "
                                "(letters, digits, '_' and '-' only)", line, key);
            goto fail;
        }

        const char *v = skip_ws(eq + 1);
        if (*v == '\0' || *v == '#') {
            seterr(err, errlen, "line %d: key '%s' has no value", line, key);
            goto fail;
        }

        if (*v == '"') {
            if (v[1] == '"' && v[2] == '"') {
                seterr(err, errlen, "line %d: multi-line strings are not supported", line);
                goto fail;
            }
            const char *cur = v;
            char *sval = scan_string(&cur, line, err, errlen);
            if (!sval) goto fail;
            const char *rest = skip_ws(cur);
            if (*rest != '\0' && *rest != '#') {
                seterr(err, errlen, "line %d: trailing text after string value", line);
                free(sval);
                goto fail;
            }
            int rc = push_entry(t, table, key, sval, VAL_STRING, line);
            free(sval);
            if (rc != 0) { seterr(err, errlen, "out of memory"); goto fail; }
            continue;
        }

        if (*v == '\'') {
            seterr(err, errlen, "line %d: literal strings ('...') are not supported, "
                                "use \"...\"", line);
            goto fail;
        }
        if (*v == '[') {
            seterr(err, errlen, "line %d: arrays are not supported", line);
            goto fail;
        }
        if (*v == '{') {
            seterr(err, errlen, "line %d: inline tables are not supported", line);
            goto fail;
        }

        /* Bare value: up to an unquoted '#'. */
        char bare[128];
        const char *hash = strchr(v, '#');
        size_t vlen = hash ? (size_t)(hash - v) : strlen(v);
        while (vlen > 0 && (v[vlen - 1] == ' ' || v[vlen - 1] == '\t')) vlen--;
        if (vlen == 0 || vlen >= sizeof(bare)) {
            seterr(err, errlen, "line %d: invalid value for key '%s'", line, key);
            goto fail;
        }
        memcpy(bare, v, vlen);
        bare[vlen] = '\0';
        rtrim(bare);
        if (strchr(bare, '_') != NULL) {
            seterr(err, errlen, "line %d: numeric underscores are not supported "
                                "(key '%s')", line, key);
            goto fail;
        }
        if (push_entry(t, table, key, bare, VAL_BARE, line) != 0) {
            seterr(err, errlen, "out of memory");
            goto fail;
        }
    }

    /* Duplicate keys are a configuration bug, not a merge instruction. */
    for (size_t i = 0; i < t->n; i++) {
        for (size_t j = i + 1; j < t->n; j++) {
            if (strcmp(t->e[i].table, t->e[j].table) == 0 &&
                strcmp(t->e[i].key,   t->e[j].key)   == 0) {
                seterr(err, errlen, "line %d: key '%s' already defined at line %d",
                       t->e[j].line, t->e[j].key, t->e[i].line);
                goto fail;
            }
        }
    }

    free(text);
    return t;

fail:
    free(text);
    toml_min_free(t);
    return NULL;
}

void toml_min_free(toml_min *t)
{
    if (!t) return;
    for (size_t i = 0; i < t->n; i++) {
        free(t->e[i].table);
        free(t->e[i].key);
        free(t->e[i].val);
    }
    free(t->e);
    for (size_t i = 0; i < t->ntables; i++) free(t->tables[i]);
    free(t->tables);
    free(t);
}

/* --- lookup --------------------------------------------------------------- */

static const entry *find(const toml_min *t, const char *table, const char *key)
{
    if (!table) table = "";
    for (size_t i = 0; i < t->n; i++) {
        if (strcmp(t->e[i].table, table) == 0 && strcmp(t->e[i].key, key) == 0)
            return &t->e[i];
    }
    return NULL;
}

/* Renders "table.key" or "key" for error messages. */
static void full_name(char *buf, size_t buflen, const char *table, const char *key)
{
    if (table && *table) snprintf(buf, buflen, "%s.%s", table, key);
    else                 snprintf(buf, buflen, "%s", key);
}

int toml_min_has_table(const toml_min *t, const char *table)
{
    if (!t || !table) return 0;
    for (size_t i = 0; i < t->ntables; i++) {
        if (strcmp(t->tables[i], table) == 0) return 1;
    }
    return 0;
}

int toml_min_int(const toml_min *t, const char *table, const char *key,
                 int *out, char *err, size_t errlen)
{
    const entry *e = find(t, table, key);
    if (!e) return 0;

    char name[256];
    full_name(name, sizeof(name), table, key);

    if (e->kind != VAL_BARE) {
        seterr(err, errlen, "line %d: %s must be an integer, not a string", e->line, name);
        return -1;
    }
    /* A float where an integer belongs is a real mistake (filter_len = 4096.0
     * would truncate silently), so it is rejected rather than rounded. */
    if (strpbrk(e->val, ".eE") != NULL) {
        seterr(err, errlen, "line %d: %s must be an integer, got '%s'", e->line, name, e->val);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    long v = strtol(e->val, &end, 10);
    if (errno == ERANGE || end == e->val || (end && *end != '\0')) {
        seterr(err, errlen, "line %d: %s is not a valid integer ('%s')", e->line, name, e->val);
        return -1;
    }
    if (v < INT_MIN || v > INT_MAX) {
        seterr(err, errlen, "line %d: %s out of range (%ld)", e->line, name, v);
        return -1;
    }
    *out = (int)v;
    return 1;
}

int toml_min_double(const toml_min *t, const char *table, const char *key,
                    double *out, char *err, size_t errlen)
{
    const entry *e = find(t, table, key);
    if (!e) return 0;

    char name[256];
    full_name(name, sizeof(name), table, key);

    if (e->kind != VAL_BARE) {
        seterr(err, errlen, "line %d: %s must be a number, not a string", e->line, name);
        return -1;
    }

    errno = 0;
    char *end = NULL;
    double v = strtod(e->val, &end);
    if (errno == ERANGE || end == e->val || (end && *end != '\0')) {
        seterr(err, errlen, "line %d: %s is not a valid number ('%s')", e->line, name, e->val);
        return -1;
    }
    *out = v;
    return 1;
}

int toml_min_string(const toml_min *t, const char *table, const char *key,
                    const char **out, char *err, size_t errlen)
{
    const entry *e = find(t, table, key);
    if (!e) return 0;

    if (e->kind != VAL_STRING) {
        char name[256];
        full_name(name, sizeof(name), table, key);
        seterr(err, errlen, "line %d: %s must be a quoted string", e->line, name);
        return -1;
    }
    *out = e->val;
    return 1;
}

int toml_min_bool(const toml_min *t, const char *table, const char *key,
                  int *out, char *err, size_t errlen)
{
    const entry *e = find(t, table, key);
    if (!e) return 0;

    char name[256];
    full_name(name, sizeof(name), table, key);

    if (e->kind != VAL_BARE ||
        (strcmp(e->val, "true") != 0 && strcmp(e->val, "false") != 0)) {
        seterr(err, errlen, "line %d: %s must be true or false", e->line, name);
        return -1;
    }
    *out = (strcmp(e->val, "true") == 0);
    return 1;
}

int toml_min_check_keys(const toml_min *t, const char *const *allowed,
                        char *err, size_t errlen)
{
    for (size_t i = 0; i < t->n; i++) {
        char name[256];
        full_name(name, sizeof(name), t->e[i].table, t->e[i].key);

        int known = 0;
        for (const char *const *a = allowed; *a; a++) {
            if (strcmp(name, *a) == 0) { known = 1; break; }
        }
        if (!known) {
            seterr(err, errlen, "line %d: unknown key '%s'", t->e[i].line, name);
            return -1;
        }
    }
    return 0;
}
