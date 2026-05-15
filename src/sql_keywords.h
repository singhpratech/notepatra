// ─────────────────────────────────────────────────────────────────────────
//  sql_keywords.h — comprehensive SQL keyword union for QsciLexerSQL
//
//  Synthesised 2026-05-15 from PRIMARY VENDOR SOURCES (v0.1.84 palette
//  overhaul). The QScintilla lexer ships a small built-in keyword Set 0
//  that lags modern SQL — words like MERGE, OPENROWSET, RETURNING, LATERAL,
//  JSONB, PIVOT, ASOF, SUMMARIZE, OVER+PARTITION fall through to plain
//  identifier-text colour. This header fixes that in one shot via
//  SCI_SETKEYWORDS at lexer-attach time in lexerutils.cpp.
//
//  Sources (one URL per dialect):
//    [TSQL]   learn.microsoft.com/en-us/sql/t-sql/language-elements/reserved-keywords-transact-sql
//    [PG]     postgresql.org/docs/current/sql-keywords-appendix.html  (Appendix C)
//    [MY]     dev.mysql.com/doc/refman/8.0/en/keywords.html
//    [LITE]   sqlite.org/lang_keywords.html
//    [ORA]    docs.oracle.com/.../19/sqlrf/Oracle-SQL-Reserved-Words.html
//    [DUCK]   duckdb.org/docs/sql/keywords_and_identifiers
//
//  Lowercase throughout; QsciLexerSQL is case-insensitive. The 'within group'
//  two-token reserved phrase is painted as the two separate words (Scintilla
//  is space-delimited). Future-reserved-only words are excluded.
//
//  Dialect priority (per user 2026-05-15): T-SQL > PG > MySQL > DuckDB > others.
// ─────────────────────────────────────────────────────────────────────────
#pragma once

namespace notepatra {

// ───── RESERVED (UNION across T-SQL / PG / MySQL / SQLite / Oracle / DuckDB) ─────
// Fed to SCI_SETKEYWORDS slot 0 — the primary keyword colour (npKeyword blue).
inline constexpr const char *kSqlReserved =
    "absolute access action add admin after aggregate algorithm all allocate "
    "alter always analyse analyze and any are array as asc asensitive "
    "assertion at atomic attach authorization autoincrement avg backup "
    "before begin between bigint binary bit blob body boolean both break "
    "browse btree bulk by call called cascade cascaded case cast catalog "
    "chain change char character check checkpoint class clob close cluster "
    "clustered coalesce collate collation column columns comment commit "
    "committed compute condition connect connection constraint constraints "
    "contains containstable continue convert corresponding count create "
    "cross cube cume_dist current current_catalog current_date current_path "
    "current_role current_schema current_time current_timestamp current_user "
    "cursor cycle data database databases date day dbcc deallocate dec "
    "decimal declare default deferrable deferred define delete dense_rank "
    "deny desc describe descriptor deterministic detach diagnostics "
    "directory disconnect disk distinct distinctrow distributed do domain "
    "double drop dual dump dynamic each element else empty enclosed end "
    "errlvl escape escaped except exception exclude exclusive exec execute "
    "exists exit explain extension external extract false fetch file "
    "fillfactor filter first first_value float float4 float8 following for "
    "force foreign found freetext freetexttable from full fulltext function "
    "general generated get global go goto grant granted greatest group "
    "grouping groups handler having hierarchyid hold holdlock host hour "
    "identified identity if ignore ilike immediate immutable import in "
    "include including increment index indexed indexes indicator inherit "
    "initial initially inner inout input insensitive insert instead int "
    "int1 int2 int3 int4 int8 integer intersect intersection interval into "
    "invoker is isnull isolation iterate join json json_array json_arrayagg "
    "json_exists json_object json_objectagg json_query json_table "
    "json_value key keys kill label lag language large last last_value "
    "lateral lead leading leakproof least leave left less level library "
    "like like_regex limit linear lines listagg listen ln load local "
    "localtime localtimestamp location lock locked log long longblob "
    "longtext loop low_priority lower lpad ltrim map match matched "
    "materialized max maxextents maxvalue measures member merge method "
    "middleint min minus minute minvalue mlslabel mod mode modifies module "
    "modify month move multiset names national natural nchar nclob nested "
    "nesting new next no nocheck none nonclustered normalize not nothing "
    "notify notnull nowait null nullif nulls numeric object of off offline "
    "offset oids old on online only open opendatasource openquery openrowset "
    "openxml operator option optionally or order ordering ordinality others "
    "out outer outfile output over overlaps owner pad parameter parser "
    "partial partition path pctfree percent percentile_cont percentile_disc "
    "percent_rank pivot plan policy power preceding precision prepare "
    "preserve primary print prior private privileges proc procedure public "
    "purge query raise raiserror range rank raw read reads readtext "
    "read_write real reconfigure recursive ref references referencing "
    "regexp reindex relative release rename repeat repeatable replace "
    "replication require resignal resource restart restore restrict result "
    "return returning returns revert revoke right rlike role rollback "
    "rollup routine row rowcount rowguidcol rowid rownum row_number rows "
    "rule rpad rtrim savepoint schema schemas scope scroll search second "
    "section secondary security securityaudit select semantickeyphrasetable "
    "semanticsimilaritydetailstable semanticsimilaritytable sensitive "
    "separator sequence session session_user set setof sets setuser share "
    "show shutdown signal similar simple size skip smallint snapshot some "
    "soname spatial specific specifictype sql sqlcode sqlerror sqlexception "
    "sqlstate sqlwarning sql_big_result sql_calc_found_rows sql_small_result "
    "ssl stable start starting state statement static statistics stddev_pop "
    "stddev_samp storage stored straight_join strict string structure "
    "submultiset substring substring_regex successful sum symmetric synonym "
    "sysdate system system_time system_user table tables tablesample "
    "tablespace temp template temporary terminated text textsize then ties "
    "time timestamp timezone_hour timezone_minute tinyblob tinyint tinytext "
    "to top trailing tran transaction translate translate_regex translation "
    "treat trigger triggers trim true truncate try_convert tsequal type "
    "uescape uid unbounded uncommitted under undo unicode union unique "
    "unknown unlisten unlock unlogged unnest unpivot unsigned until update "
    "updatetext upper usage use user using utc_date utc_time utc_timestamp "
    "vacuum validate value values varbinary varchar varchar2 varcharacter "
    "variable variadic varying var_pop var_samp verbose view virtual "
    "visible volatile waitfor when whenever where while widget width_bucket "
    "window with within without work write writetext x509 xa xmlagg "
    "xmlattributes xmlcast xmlcomment xmlconcat xmldocument xmlelement "
    "xmlexists xmlforest xmlnamespaces xmlparse xmlpi xmlquery xmlserialize "
    "xmltable xor year yes zerofill zone "
    /* T-SQL only [TSQL] */
    "withingroup "
    /* PG only [PG] */
    "concurrently freeze placing "
    /* MySQL only [MY] */
    "delayed div high_priority master_bind sql_no_cache "
    /* SQLite only [LITE] */
    "abort glob pragma "
    /* Oracle only [ORA] */
    "audit nested_table_id noaudit nocompress "
    /* DuckDB additions [DUCK] — confirmed first-class dialect per user 2026-05-15 */
    "summarize asof positional bernoulli reservoir sample install attach detach "
    "export try macro";

// ───── BUILTIN FUNCTIONS (scalar / aggregate / window / JSON / DuckDB) ─────
// Fed to SCI_SETKEYWORDS slot 1 — secondary keyword colour (npKeyword2 magenta
// in SSMS theme, teal in dark). Painted bold to stand out from RESERVED.
inline constexpr const char *kSqlFunctions =
    "abs acos any_value array_agg ascii asin atan atan2 avg "
    "bit_and bit_count bit_length bit_or bit_xor btrim "
    "cardinality ceil ceiling char_length character_length charindex chr "
    "coalesce concat concat_ws convert corr cos cosh cot count covar_pop "
    "covar_samp cume_dist current_date current_time current_timestamp "
    "current_user datalength dateadd datediff datename datepart day "
    "dayname dayofmonth dayofweek dayofyear degrees dense_rank exp "
    "extract first_value floor format from_days from_unixtime greatest "
    "group_concat grouping hex hour ifnull initcap instr isdate isnull "
    "json_array json_arrayagg json_exists json_object json_objectagg "
    "json_query json_table json_value lag last_day last_value lcase lead "
    "least left len length listagg ln locate log log10 log2 lower lpad "
    "ltrim makedate max md5 microsecond mid min minute mod "
    "month monthname now nth_value ntile nullif octet_length parsename "
    "patindex percentile_cont percentile_disc percent_rank pi position "
    "power quarter quote radians rand random rank regexp_count regexp_like "
    "regexp_replace regexp_substr regexp_matches regexp_extract repeat "
    "replace reverse right round row_number rpad rtrim second session_user "
    "sha1 sha2 sign sin sinh soundex space sqrt stddev stddev_pop stddev_samp "
    "str strcmp str_to_date substr substring sum sysdate sysdatetime "
    "systimestamp system_user tan tanh time timediff timestampadd "
    "timestampdiff to_char to_date to_number to_timestamp translate trim "
    "trunc truncate ucase unhex unicode unix_timestamp upper user uuid "
    "var_pop var_samp variance version week weekday weekofyear xmlagg "
    "xmlconcat xmlelement xmlexists xmlforest xmlparse xmlpi xmlquery "
    "xmlserialize year "
    /* DuckDB table functions and string macros [DUCK] */
    "read_csv read_csv_auto read_parquet read_json read_json_auto "
    "parquet_scan json_scan duckdb_extensions duckdb_functions "
    "duckdb_settings duckdb_databases duckdb_tables duckdb_columns "
    "duckdb_views duckdb_types string_split string_split_regex "
    "list_value struct_pack map_from_entries";

// ───── BUILTIN TYPES (data types across all 6 dialects) ─────
// Fed to SCI_SETKEYWORDS slot 4 ("User-defined keyword 1") — picked up by
// npp_palette.cpp's "user defined" branch which routes to npKeyword2.
inline constexpr const char *kSqlTypes =
    "bigint binary bit blob bool boolean box bpchar bytea char character "
    "cidr citext clob date datetime datetime2 datetimeoffset dec decfloat "
    "decimal double enum float float4 float8 geography geometry "
    "geometrycollection hierarchyid image inet int int1 int2 int3 int4 "
    "int8 integer interval json jsonb line linestring longblob longtext "
    "macaddr macaddr8 mediumblob mediumint mediumtext money multilinestring "
    "multipoint multipolygon national nchar nclob ntext numeric nvarchar "
    "oid path point polygon raw real rowid rowversion serial set "
    "smalldatetime smallint smallmoney smallserial sql_variant text time "
    "timestamp timestamptz timetz tinyblob tinyint tinytext tsquery "
    "tsvector uniqueidentifier uuid varbinary varchar varchar2 varying xml "
    "year "
    /* DuckDB extended integer family [DUCK] */
    "hugeint uhugeint utinyint usmallint uinteger ubigint "
    "struct list union_tag";

}  // namespace notepatra
