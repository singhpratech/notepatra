/**
 * notepad_core.h — C API for the Rust core library.
 * Memory-safe file I/O, text processing, search, hashing.
 */

#ifndef NOTEPAD_CORE_H
#define NOTEPAD_CORE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ═══════════ Result types ═══════════ */

typedef struct {
    char *text;         /* UTF-8 text (free with npc_free_string) */
    size_t text_len;
    char *encoding;     /* Detected encoding name */
    int eol_mode;       /* 0=LF, 1=CRLF, 2=CR */
    uint64_t file_size;
    int status;         /* 0=ok, 1=binary, 2=too_large, 3=error, 4=oom */
    char *error_msg;    /* Error message if status != 0 */
    int truncated;      /* 1 if file was truncated */
} FileLoadResult;

typedef struct {
    size_t *positions;  /* Array of byte offsets */
    size_t count;
} SearchResult;

typedef struct {
    char *text;
    size_t text_len;
} TextResult;

typedef struct {
    char *hex;
} HashResult;

/* ═══════════ File I/O ═══════════ */

FileLoadResult npc_load_file(const char *path);
int npc_save_file(const char *path, const char *text, size_t text_len, const char *encoding);

/* ═══════════ Text operations ═══════════ */

/* Sort: 0=asc, 1=desc, 2=int_asc, 3=int_desc, 4=len_asc, 5=len_desc */
TextResult npc_sort_lines(const char *text, size_t len, int mode);

/* Remove dupes: 0=all, 1=consecutive */
TextResult npc_remove_duplicates(const char *text, size_t len, int mode);

/* Remove empty: 0=empty, 1=blank */
TextResult npc_remove_empty_lines(const char *text, size_t len, int mode);

/* Trim: 0=trailing, 1=leading, 2=both */
TextResult npc_trim_lines(const char *text, size_t len, int mode);

TextResult npc_reverse_lines(const char *text, size_t len);
TextResult npc_join_lines(const char *text, size_t len, const char *separator);

/* Case: 0=upper, 1=lower, 2=title, 3=sentence, 4=invert */
TextResult npc_convert_case(const char *text, size_t len, int mode);

/* Whitespace: 0=tab_to_space, 1=space_to_tab */
TextResult npc_convert_whitespace(const char *text, size_t len, int tab_width, int mode);

/* ═══════════ Search ═══════════ */

SearchResult npc_find_all(const char *text, size_t len, const char *pattern,
                          int is_regex, int case_sensitive, int whole_word);
size_t npc_count_matches(const char *text, size_t len, const char *pattern,
                         int is_regex, int case_sensitive);
TextResult npc_replace_all(const char *text, size_t len, const char *pattern,
                           const char *replacement, int is_regex, int case_sensitive);

/* ═══════════ Hashing ═══════════ */

/* algo: 0=md5, 1=sha1, 2=sha256, 3=sha512 */
HashResult npc_hash(const char *data, size_t len, int algo);
TextResult npc_base64_encode(const char *data, size_t len);
TextResult npc_base64_decode(const char *data, size_t len);
TextResult npc_url_encode(const char *text, size_t len);
TextResult npc_url_decode(const char *text, size_t len);

/* ═══════════ Diff / Compare ═══════════ */

typedef struct {
    int tag;          /* 0=equal, 1=insert, 2=delete */
    int left_line;    /* line in left file (0 if insert) */
    int right_line;   /* line in right file (0 if delete) */
    char *text;       /* line content */
} DiffLine;

typedef struct {
    DiffLine *lines;
    size_t count;
    int added, removed, changed;
} DiffResult;

DiffResult npc_diff(const char *left, size_t left_len, const char *right, size_t right_len);
void npc_free_diff(DiffResult result);

/* ═══════════ SQL Formatter ═══════════ */

/* dialect: "ansi" | "postgres" | "mysql" | "mssql" | "sqlite" | "plsql"
 *          (NULL or empty = "ansi"). */
TextResult npc_format_sql(const char *text, size_t len, int indent_width,
                          int uppercase, const char *dialect);

/* v0.1.49 — compact one-line-where-possible SQL formatter. Same dialect
 * support as npc_format_sql; only the line-break policy differs. */
TextResult npc_format_sql_compact(const char *text, size_t len, int indent_width,
                                  int uppercase, const char *dialect);

/* ═══════════ JSON Formatter + Fixer ═══════════ */

TextResult npc_format_json(const char *text, size_t len, int indent);
TextResult npc_minify_json(const char *text, size_t len);
TextResult npc_fix_json(const char *text, size_t len);
TextResult npc_fix_json_report(const char *text, size_t len);

/* ═══════════ HTML Formatter ═══════════ */

TextResult npc_format_html(const char *text, size_t len, int indent);

/* ═══════════ Bracket Fixer ═══════════ */

TextResult npc_fix_brackets(const char *text, size_t len);
TextResult npc_check_brackets(const char *text, size_t len);

/* ═══════════ Memory management ═══════════ */

void npc_free_string(char *s);
void npc_free_matches(SearchResult result);
void npc_free_text_result(TextResult result);

/* v0.1.87 — file-text buffer free (NOT a CString — boxed byte slice).
 * Use this for FileLoadResult.text. Other char* fields (encoding,
 * error_msg) remain CStrings and use npc_free_string. */
void npc_free_file_text(char *text, size_t text_len);

#ifdef __cplusplus
}
#endif

#endif /* NOTEPAD_CORE_H */
