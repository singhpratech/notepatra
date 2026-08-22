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
    size_t *lengths;    /* Byte length of each match, parallel to positions.
                         * Under a regex the match length is unrelated to the
                         * pattern length, so callers must not infer it. */
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
/* Strict validation: empty text = valid JSON, otherwise a parse error with
 * line/column. Never repairs, unlike npc_format_json. */
TextResult npc_json_parse_error(const char *text, size_t len);
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

/* ═══════════ SSH Key Generator ═══════════ */

/* ── v0.1.129 — SSH key generation + OS-entropy bytes (keygen.rs) ──────────
 * All randomness: rand_core::OsRng (getrandom) — the OS CSPRNG, NOT RDRAND-
 * first like QRandomGenerator::system(). Pure Rust (RustCrypto ssh-key).     */

/* alg: 0 = Ed25519, 1 = ECDSA P-256, 2 = ECDSA P-384, 3 = RSA.
 * bits: RSA only (2048 | 3072 | 4096); ignored otherwise.
 * comment / passphrase: UTF-8, may be NULL or "". A non-empty passphrase
 * encrypts the private key (OpenSSH format, aes256-ctr + bcrypt-pbkdf, 16 rounds,
 * same as `ssh-keygen`). */
typedef struct {
    int    ok;                 /* 1 on success */
    char  *private_pem;        /* "-----BEGIN OPENSSH PRIVATE KEY-----…", trailing \n */
    size_t private_len;
    char  *public_line;        /* "ssh-ed25519 AAAA… comment\n" (authorized_keys line) */
    size_t public_len;
    char  *fingerprint;        /* "SHA256:…" (base64, no padding) — same as ssh-keygen -l */
    size_t fingerprint_len;
    char  *error_msg;          /* CString, or NULL; free with npc_free_string */
} SshKeyResult;
SshKeyResult npc_ssh_keygen(int alg, int bits, const char *comment, const char *passphrase);
/* Zero-fills every buffer before freeing (best effort — the C++ copies can't be). */
void npc_free_ssh_key(SshKeyResult r);

/* Fill `buf[0..len)` from the OS CSPRNG. Returns 1 on success, 0 on failure
 * (buf untouched). For the password generator's draws. */
int npc_random_bytes(unsigned char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* NOTEPAD_CORE_H */
