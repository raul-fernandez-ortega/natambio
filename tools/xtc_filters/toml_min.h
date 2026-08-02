/*
 * Author: Raul Fernandez Ortega <natambio.audio@gmail.com>, 2022-2026
 *
 * Licensed under the GNU General Public License v3 (GPLv3); see the LICENSE file.
 */

#ifndef TOML_MIN_H
#define TOML_MIN_H

#include <stddef.h>

/* toml_min — reader for the strict TOML subset the XTC tools need.
 *
 * Deliberately NOT a full TOML implementation. It accepts only:
 *
 *   # comments, to end of line (not inside a quoted string)
 *   [table]                     one level, bare names
 *   key = 42                    integers
 *   key = 3.5                   floats
 *   key = "text"                basic strings, with \" \\ \n \t \r escapes
 *   key = true | false          booleans
 *
 * Anything else -- arrays, inline tables, dotted keys, literal strings,
 * multi-line strings, dates, numeric underscores, nested tables -- is a hard
 * error reporting the line number, never a silent misreading.
 *
 * The point of the subset is that it is a SUBSET: every file this reader
 * accepts is valid TOML with the same meaning, so the Python counterparts read
 * the very same files through the standard `tomllib` and cannot diverge.
 *
 * Configuration files are tiny, so lookups are a linear scan and the whole
 * document is kept in memory as parsed strings, converted on access.
 */

typedef struct toml_min toml_min;

/* Parses `path`. Returns NULL on failure, writing a message (with line number
 * where applicable) into `err`. The returned handle owns all its memory. */
toml_min *toml_min_load(const char *path, char *err, size_t errlen);

void toml_min_free(toml_min *t);

/* Value accessors. `table` is the table name, or "" / NULL for top level.
 *
 * Return value:
 *    1  key present and converted into *out
 *    0  key absent; *out untouched
 *   -1  key present but not of the requested type, or out of range; `err` set
 */
int toml_min_int(const toml_min *t, const char *table, const char *key,
                 int *out, char *err, size_t errlen);
int toml_min_double(const toml_min *t, const char *table, const char *key,
                    double *out, char *err, size_t errlen);
int toml_min_string(const toml_min *t, const char *table, const char *key,
                    const char **out, char *err, size_t errlen);
int toml_min_bool(const toml_min *t, const char *table, const char *key,
                  int *out, char *err, size_t errlen);

/* Returns 1 if the document declares `table` at all, 0 otherwise. Used to tell
 * "table missing" from "table present but empty", which are different mistakes. */
int toml_min_has_table(const toml_min *t, const char *table);

/* Rejects any key the caller does not know about. `allowed` is a NULL-terminated
 * list of "table.key" strings, using a bare "key" for the top level.
 *
 * This is what turns a typo into an error instead of a silently ignored line:
 * without it, `ild_alfa = 1.8` would leave ild_alpha at its default and the user
 * would spend an afternoon wondering why the parameter does nothing.
 *
 * Returns 0 if every key is known, -1 otherwise with `err` naming the offender. */
int toml_min_check_keys(const toml_min *t, const char *const *allowed,
                        char *err, size_t errlen);

#endif
