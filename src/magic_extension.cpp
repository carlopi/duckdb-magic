#define DUCKDB_EXTENSION_MAIN

#include <magic.h>

#define STATIC_MAGIC_FILE

#ifdef STATIC_MAGIC_FILE
#include "magic_mgc.hpp"
#endif
#include "magic_extension.hpp"
#include "read_attacheable_database.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/main/extension_helper.hpp"

namespace duckdb {

// clang-format off
static const DefaultTableMacro dynamic_sql_examples_table_macros[] = {
    {DEFAULT_SCHEMA, "read_any_impl", {"file_name", "format", "relative_path", nullptr}, {{nullptr, nullptr}}, R"(
----CREATE OR REPLACE MACRO read_any_impl(file_name, format, relative_path) AS TABLE (
       WITH "json_case" as (FROM read_json_auto(file_name))
           , "csv_case" as (FROM read_csv(file_name))
           , "parquet_case" as (FROM read_parquet(file_name))
           , "avro_case" as (FROM read_avro(file_name))
           , "arrow_case" as (FROM read_arrow(file_name))
           , "read_stat_case" as (FROM read_stat(file_name))
           , "duckdb_case" as (FROM read_attacheable_database(file_name, type:='duckdb', relative_path:=relative_path))
           , "s3_tables_case" as (FROM read_attacheable_database(file_name, type:='iceberg', options:=MAP {'endpoint_type': 's3_tables'}, relative_path:=relative_path))
           , "postgres_case" as (FROM read_attacheable_database(file_name, type:='postgres', relative_path:=relative_path))
           , "mysql_case" as (FROM read_attacheable_database(file_name, type:='mysql', relative_path:=relative_path))
           , "lance_case" as (FROM __lance_scan(file_name))
           , "ducklake_case" as (FROM read_attacheable_database(regexp_replace(file_name, '^ducklake:', ''), type:='ducklake', relative_path:=relative_path))
           , "motherduck_case" as (FROM read_attacheable_database(regexp_replace(file_name, '^md:', ''), type:='motherduck', relative_path:=relative_path))
           , "mongo_case" as (FROM read_attacheable_database(file_name, type:='mongo@community', relative_path:=relative_path))
           , "sqlite_case" as (FROM read_attacheable_database(file_name, type:='sqlite', relative_path:=relative_path))
           , "blob_case" as (FROM read_blob(file_name))
           , "spatial_case" as (FROM st_read(file_name))
           , "vortex_case" as (FROM read_vortex(file_name))
           , "excel_case" as (FROM read_xlsx(file_name))
           , "yaml_case" as (FROM read_yaml(file_name))
           , "ics_case" as (
               WITH raw AS (
                   SELECT row_number() OVER () AS rn, column0 AS line
                   FROM read_csv(file_name, header=false, sep=e'\x01', columns={'column0': 'VARCHAR'})
               ),
               events AS (
                   SELECT rn, line,
                       count(*) FILTER (WHERE line='BEGIN:VEVENT') OVER (ORDER BY rn) AS event_idx
                   FROM raw
               ),
               inside AS (
                   SELECT line, event_idx FROM events
                   WHERE event_idx > 0
                     AND line NOT IN ('BEGIN:VEVENT', 'END:VEVENT', 'BEGIN:VCALENDAR', 'END:VCALENDAR')
                     AND line NOT LIKE 'VERSION:%' AND line NOT LIKE 'PRODID:%' AND line NOT LIKE 'DTSTAMP:%'
               ),
               kv AS (
                   SELECT event_idx,
                       regexp_extract(line, '^([^;:]+)[;:]', 1)  AS key,
                       substr(line, position(':' IN line) + 1)    AS value
                   FROM inside
               ),
               pivoted AS (
                   SELECT
                       max(value) FILTER (WHERE key='UID')      AS uid,
                       max(value) FILTER (WHERE key='SUMMARY')  AS summary,
                       max(value) FILTER (WHERE key='LOCATION') AS location,
                       max(value) FILTER (WHERE key='DTSTART')  AS dtstart_raw,
                       max(value) FILTER (WHERE key='DTEND')    AS dtend_raw,
                       regexp_replace(max(value) FILTER (WHERE key='ORGANIZER'), '^mailto:', '') AS organizer,
                       list(regexp_replace(value, '^mailto:', '')) FILTER (WHERE key='ATTENDEE') AS attendees,
                   FROM kv GROUP BY event_idx ORDER BY event_idx
               )
               SELECT uid, summary, location,
                   coalesce(
                       try_strptime(dtstart_raw, '%Y%m%dT%H%M%SZ'),
                       try_strptime(dtstart_raw, '%Y%m%dT%H%M%S'),
                       try_strptime(dtstart_raw, '%Y%m%d')
                   ) AS dtstart,
                   coalesce(
                       try_strptime(dtend_raw, '%Y%m%dT%H%M%SZ'),
                       try_strptime(dtend_raw, '%Y%m%dT%H%M%S'),
                       try_strptime(dtend_raw, '%Y%m%d')
                   ) AS dtend,
                   organizer, attendees,
               FROM pivoted
           )
           , "ipynb_case" as (
               WITH nb AS (FROM read_json_auto(file_name))
               SELECT
                   cell_idx,
                   cell.cell_type,
                   array_to_string(cell.source, '') AS source,
                   cell.execution_count,
                   array_to_string(
                       flatten(list(o.text) FILTER (WHERE o.output_type='stream')),
                       '') AS stdout,
                   array_to_string(
                       flatten(list(struct_extract(o.data, 'text/plain')) FILTER (WHERE o.output_type='execute_result')),
                       '') AS result,
               FROM nb, UNNEST(cells) WITH ORDINALITY AS t(cell, cell_idx)
               LEFT JOIN UNNEST(cell.outputs) AS t2(o) ON true
               GROUP BY ALL
               ORDER BY cell_idx
           )
           , "ods_case" as (FROM read_sheet(file_name))
           , "xml_case" as (FROM read_xml(file_name))
           , "har_case" as (
               SELECT
                   entry.startedDateTime                   AS started_at,
                   entry.request.method                    AS method,
                   entry.request.url                       AS url,
                   entry.response.status                   AS status,
                   entry.response.statusText               AS status_text,
                   entry.time                              AS total_ms,
                   entry.timings.send                      AS send_ms,
                   entry.timings.wait                      AS wait_ms,
                   entry.timings.receive                   AS receive_ms,
                   entry.request.bodySize                  AS req_body_bytes,
                   entry.response.content.size             AS resp_body_bytes,
                   entry.response.content.mimeType         AS resp_mime,
               FROM (
                   SELECT UNNEST(log.entries) AS entry
                   FROM read_json_auto(file_name, maximum_object_size=100000000)
               )
           )
       -- TODO (post v1.5.0): add support for community extensions only available on stable releases:
       --   - HDF5 (.h5/.hdf5)   via h5db:        magic returns 'application/x-hdf5', detect via magic_mime
       --                         h5_read() requires a dataset path arg (not just a file path), so it can't fit
       --                         the read_any(file) pattern directly. Options: (a) use h5_tree() to show structure
       --                         only, or (b) add an optional dataset parameter to read_any() so the user can
       --                         pass read_any('file.h5', dataset:='/measurements').
       FROM query_table(
             CASE
               WHEN format=='blob' THEN 'blob_case'
               -- S3 Tables (Iceberg): detect by ARN prefix; checked before any
               -- magic_*() call since those would try to stat the non-file ARN.
               WHEN format=='s3tables' OR format=='s3_tables' OR (format=='auto' AND file_name ILIKE 'arn:aws:s3tables:%') THEN 's3_tables_case'
               -- Postgres connection string: detect by URI scheme (not a file)
               WHEN format=='postgres' OR (format=='auto' AND (file_name ILIKE 'postgres://%' OR file_name ILIKE 'postgresql://%')) THEN 'postgres_case'
               -- MySQL connection string: detect by URI scheme (not a file)
               WHEN format=='mysql' OR (format=='auto' AND file_name ILIKE 'mysql://%') THEN 'mysql_case'
               -- Lance dataset: a directory (magic can't sniff it), detect by .lance suffix
               WHEN format=='lance' OR (format=='auto' AND file_name ILIKE '%.lance') THEN 'lance_case'
               -- DuckLake catalog: ducklake: prefix (metadata is itself a DuckDB/SQLite/Postgres db,
               -- so the prefix is required to override plain duckdb detection)
               WHEN format=='ducklake' OR (format=='auto' AND file_name ILIKE 'ducklake:%') THEN 'ducklake_case'
               -- MotherDuck: md: prefix → strip + attach as motherduck
               WHEN format=='motherduck' OR format=='md' OR (format=='auto' AND file_name ILIKE 'md:%') THEN 'motherduck_case'
               -- MongoDB: mongodb:// connection string → attach as mongo (community)
               WHEN format=='mongo' OR (format=='auto' AND (file_name ILIKE 'mongodb://%' OR file_name ILIKE 'mongodb+srv://%')) THEN 'mongo_case'
               -- NOTE: .gml is excluded (GDAL fetches remote XSD schema, hangs without network)
               --       .osm is excluded (GDAL OSM driver requires a config file, crashes without it)
               --       .gpx is excluded (GDAL GPX driver crashes on read in current spatial version)
               WHEN format == 'spatial' OR format == 'gpkg' OR format LIKE 'geo%' OR (format=='auto' AND (file_name ILIKE '%.geojson' OR file_name ILIKE '%.geojsonl' OR file_name ILIKE '%.ndgeojson' OR file_name ILIKE '%.topojson' OR file_name ILIKE '%.fgb' OR file_name ILIKE '%.prj' OR file_name ILIKE '%.shp' OR file_name ILIKE '%.kml' OR magic_mime(file_name) ILIKE '%geopackage%')) THEN 'spatial_case'
               WHEN format=='ipynb' OR format=='notebook' OR (format=='auto' AND file_name ILIKE '%.ipynb') THEN 'ipynb_case'
               WHEN format=='har' OR (format=='auto' AND file_name ILIKE '%.har') THEN 'har_case'
               WHEN format=='json' OR (format=='auto' AND (magic_mime(file_name) ILIKE '%json' OR file_name ILIKE '%.json' OR file_name ILIKE '%.jsonl' OR file_name ILIKE '%.ndjson')) THEN 'json_case'
               WHEN format=='yaml' OR format=='yml' OR (format=='auto' AND (file_name ILIKE '%.yaml' OR file_name ILIKE '%.yml')) THEN 'yaml_case'
               WHEN format=='ics' OR format=='ical' OR format=='calendar' OR (format=='auto' AND (magic_mime(file_name) ILIKE 'text/calendar' OR file_name ILIKE '%.ics' OR file_name ILIKE '%.ical')) THEN 'ics_case'
               WHEN format=='csv' OR (format=='auto' AND (magic_mime(file_name) ILIKE 'text/plain' OR magic_mime(file_name) ILIKE 'text/csv')) THEN 'csv_case'
               WHEN format=='parquet' OR (format=='auto' AND magic_type(file_name) ILIKE 'Apache Parquet%') THEN 'parquet_case'
               WHEN format=='avro' OR (format=='auto' AND magic_type(file_name) ILIKE 'Apache Avro%') THEN 'avro_case'
               WHEN format=='vortex' OR (format=='auto' AND file_name ILIKE '%.vortex') THEN 'vortex_case'
               -- Arrow IPC: magic returns generic 'data', detect by file extension
               WHEN format=='arrow' OR format=='ipc' OR (format=='auto' AND (file_name ILIKE '%.arrow' OR file_name ILIKE '%.arrows' OR file_name ILIKE '%.ipc')) THEN 'arrow_case'
               -- Stata/SPSS/SAS via read_stat: magic returns generic 'data', detect by extension
               WHEN format=='stat' OR format=='read_stat' OR (format=='auto' AND (file_name ILIKE '%.dta' OR file_name ILIKE '%.sav' OR file_name ILIKE '%.zsav' OR file_name ILIKE '%.por' OR file_name ILIKE '%.sas7bdat' OR file_name ILIKE '%.xpt')) THEN 'read_stat_case'
               -- DuckDB database file: read via read_attacheable_database (built-in, no extension)
               WHEN format=='duckdb' OR (format=='auto' AND magic_type(file_name) ILIKE 'DuckDB database file%') THEN 'duckdb_case'
               WHEN format=='sqlite' OR (format=='auto' AND (magic_type(file_name) ILIKE 'SQLite format 3%' OR file_name ILIKE '%.sqlite' OR file_name ILIKE '%.sqlite3')) THEN 'sqlite_case'
               WHEN format=='excel' OR format=='xlsx' OR (format=='auto' AND magic_type(file_name) ILIKE 'Microsoft Excel%') THEN 'excel_case'
               WHEN format=='ods' OR (format=='auto' AND (magic_type(file_name) ILIKE 'OpenDocument Spreadsheet%' OR magic_mime(file_name) ILIKE '%opendocument.spreadsheet%' OR file_name ILIKE '%.ods')) THEN 'ods_case'
               WHEN format=='xml' OR (format=='auto' AND (magic_mime(file_name) ILIKE 'text/xml' OR file_name ILIKE '%.xml')) THEN 'xml_case'
               WHEN format=='auto' THEN error('read_any can not auto recognize a valid format, try explicitly: FROM read_any("' || file_name ||'", format:="csv"), explcitly supported formats are csv, json, har, ics, ipynb, parquet, avro, arrow, stat, duckdb, sqlite, s3tables, postgres, mysql, mongo, ducklake, motherduck, lance, vortex, excel, ods, xml, yaml, spatial and blob')
             ELSE error('read_any explicitly provided format is not one of: csv | json | har | ics (or ical/calendar alias) | ipynb (or notebook alias) | parquet | avro | arrow (or ipc alias) | stat (or read_stat alias) | duckdb | sqlite | s3tables | postgres | mysql | mongo | ducklake | motherduck (or md alias) | lance | vortex | excel | ods | xml | yaml | blob | spatial (or geo*/gpkg alias) | auto"')
             END
       )
----   );
    )"},
    {DEFAULT_SCHEMA, "read_any", {"file_name", nullptr}, {{"format", "'auto'"}, {nullptr, nullptr}}, R"(
----CREATE OR REPLACE MACRO read_any(file_name, format:='auto') AS TABLE (
       -- Split an optional `path@selector` suffix, then normalize the path before
       -- detection/read:
       --   * archive member  file.zip@dir/x.parquet -> zip://file.zip/dir/x.parquet
       --                     file.tar@dir/x.parquet -> tar://file.tar/dir/x.parquet
       --     (selector may contain '/', so it is folded into the scheme URL and the
       --      relative_path is cleared; the inner format is then auto-detected)
       --   * plain S3 object ARN  arn:aws:s3:::bucket/key -> s3://bucket/key
       FROM read_any_impl(
           CASE
             WHEN lower(split_into_components(file_name).path) LIKE '%.zip' AND split_into_components(file_name).selector != '' AND list_contains(magic_archive_members(split_into_components(file_name).path), split_into_components(file_name).selector)
               THEN 'zip://' || split_into_components(file_name).path || '/' || split_into_components(file_name).selector
             WHEN lower(split_into_components(file_name).path) LIKE '%.zip'
               THEN error('read_any: archive "' || split_into_components(file_name).path || '" ' || CASE WHEN split_into_components(file_name).selector = '' THEN 'needs a member, e.g. read_any(''' || split_into_components(file_name).path || '@<member>'')' ELSE 'has no member "' || split_into_components(file_name).selector || '"' END || '. Available members: ' || array_to_string(magic_archive_members(split_into_components(file_name).path), ', '))
             WHEN lower(split_into_components(file_name).path) LIKE '%.tar' AND split_into_components(file_name).selector != '' AND list_contains(magic_archive_members(split_into_components(file_name).path), split_into_components(file_name).selector)
               THEN 'tar://' || split_into_components(file_name).path || '/' || split_into_components(file_name).selector
             WHEN lower(split_into_components(file_name).path) LIKE '%.tar'
               THEN error('read_any: archive "' || split_into_components(file_name).path || '" ' || CASE WHEN split_into_components(file_name).selector = '' THEN 'needs a member, e.g. read_any(''' || split_into_components(file_name).path || '@<member>'')' ELSE 'has no member "' || split_into_components(file_name).selector || '"' END || '. Available members: ' || array_to_string(magic_archive_members(split_into_components(file_name).path), ', '))
             ELSE regexp_replace(split_into_components(file_name).path, '^arn:aws:s3:::', 's3://')
           END,
           format,
           CASE
             WHEN lower(split_into_components(file_name).path) LIKE '%.zip' OR lower(split_into_components(file_name).path) LIKE '%.tar'
               THEN ''
             ELSE split_into_components(file_name).selector
           END
       )
----   );
    )"},
    {DEFAULT_SCHEMA, "read_har", {"file_name", nullptr}, {{nullptr, nullptr}}, R"(
----CREATE OR REPLACE MACRO read_har(file_name) AS TABLE (
       SELECT
           entry.startedDateTime                   AS started_at,
           entry.request.method                    AS method,
           entry.request.url                       AS url,
           entry.response.status                   AS status,
           entry.response.statusText               AS status_text,
           entry.time                              AS total_ms,
           entry.timings.send                      AS send_ms,
           entry.timings.wait                      AS wait_ms,
           entry.timings.receive                   AS receive_ms,
           entry.request.bodySize                  AS req_body_bytes,
           entry.response.content.size             AS resp_body_bytes,
           entry.response.content.mimeType         AS resp_mime,
       FROM (
           SELECT UNNEST(log.entries) AS entry
           FROM read_json_auto(file_name, maximum_object_size=100000000)
       )
----   );
    )"},
	{nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}
	};
// clang-format on

template <bool MIME_TYPE>
struct MagicFunctionLocalState : public FunctionLocalState {
  explicit MagicFunctionLocalState() : FunctionLocalState() {

    /* MAGIC_MIME tells magic to return a mime of the file,
       but you can specify different things	*/

    if (MIME_TYPE) {
      magic_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_ERROR);
    } else {
      magic_cookie = magic_open(MAGIC_NONE | MAGIC_ERROR);
    }

    if (magic_cookie == NULL) {
      throw std::runtime_error("Unable to initialize magic library");
      return;
    }

#ifndef STATIC_MAGIC_FILE
    if (magic_load(magic_cookie,
                   "/Users/carlo/duckdblabs/extension-template/build/release/"
                   "vcpkg_installed/arm64-osx/share/libmagic/misc/magic.mgc") !=
        0) {
      string message(magic_error(magic_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
#else
    void *buff[1] = {const_cast<unsigned char *>(&magic_mgc[0])};
    size_t z[1] = {size_t(magic_mgc_size)};
    if (magic_load_buffers(magic_cookie, buff, z, 1) != 0) {
      string message(magic_error(magic_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
#endif
  }
  ~MagicFunctionLocalState() {
    if (magic_cookie) {
      magic_close(magic_cookie);
    }
  }
  magic_t magic_cookie;
};

template <bool MIME>
inline unique_ptr<FunctionLocalState>
MagicFunctionLocalStateFun(ExpressionState &state,
                           const BoundFunctionExpression &expr,
                           FunctionData *bind_data) {
  auto res = make_uniq<MagicFunctionLocalState<MIME>>();
  return std::move(res);
}

// Local state holding both MAGIC_NONE and MAGIC_MIME_TYPE cookies,
// used by magic_required_extensions().
struct MagicBothLocalState : public FunctionLocalState {
  explicit MagicBothLocalState() : FunctionLocalState() {
    magic_type_cookie = magic_open(MAGIC_NONE | MAGIC_ERROR);
    magic_mime_cookie = magic_open(MAGIC_MIME_TYPE | MAGIC_ERROR);

    if (!magic_type_cookie || !magic_mime_cookie) {
      throw std::runtime_error("Unable to initialize magic library");
    }

#ifdef STATIC_MAGIC_FILE
    void *buff[1] = {const_cast<unsigned char *>(&magic_mgc[0])};
    size_t z[1] = {size_t(magic_mgc_size)};
    if (magic_load_buffers(magic_type_cookie, buff, z, 1) != 0) {
      string message(magic_error(magic_type_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
    if (magic_load_buffers(magic_mime_cookie, buff, z, 1) != 0) {
      string message(magic_error(magic_mime_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
#else
    if (magic_load(magic_type_cookie, nullptr) != 0) {
      string message(magic_error(magic_type_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
    if (magic_load(magic_mime_cookie, nullptr) != 0) {
      string message(magic_error(magic_mime_cookie));
      throw std::runtime_error("Cannot load magic database " + message);
    }
#endif
  }

  ~MagicBothLocalState() {
    if (magic_type_cookie)
      magic_close(magic_type_cookie);
    if (magic_mime_cookie)
      magic_close(magic_mime_cookie);
  }

  magic_t magic_type_cookie;
  magic_t magic_mime_cookie;
};

static unique_ptr<FunctionLocalState>
MagicBothLocalStateFun(ExpressionState &state,
                       const BoundFunctionExpression &expr,
                       FunctionData *bind_data) {
  return make_uniq<MagicBothLocalState>();
}

// Try to install+load a community filesystem extension for the given path URI.
// Checks allow_community_extensions before installing, and uses the community
// repository explicitly. Best-effort: failures surface from the actual reader.
static void TryEnsureCommunityFilesystem(ClientContext &context,
                                         const string &ext_name) {
  if (context.db->ExtensionIsLoaded(ext_name)) {
    return;
  }
  try {
    if (Settings::Get<AutoinstallKnownExtensionsSetting>(context) &&
        Settings::Get<AllowCommunityExtensionsSetting>(context)) {
      auto community_repo = ExtensionRepository::GetRepositoryByUrl(
          "http://community-extensions.duckdb.org");
      ExtensionInstallOptions options;
      options.repository = community_repo;
      ExtensionHelper::InstallExtension(context, ext_name, options);
    }
    if (Settings::Get<AutoloadKnownExtensionsSetting>(context)) {
      ExtensionHelper::LoadExternalExtension(context, ext_name);
    }
  } catch (...) {
    // best-effort — errors will surface from the actual reader
  }
}

// URI scheme → (extension_name, is_community) mappings.
// Order matters: longer/more-specific prefixes first.
struct FilesystemScheme {
  const char *scheme;
  const char *extension;
  bool community;
};
static const FilesystemScheme FILESYSTEM_SCHEMES[] = {
    // HTTP/S and S3-compatible (httpfs core ext)
    {"https://", "httpfs", false},
    {"http://",  "httpfs", false},
    {"s3://",    "httpfs", false},
    {"s3a://",   "httpfs", false},
    {"s3n://",   "httpfs", false},
    {"gcs://",   "httpfs", false},
    {"gs://",    "httpfs", false},
    {"r2://",    "httpfs", false},
    // Azure (azure core ext)
    {"abfss://", "azure",  false},
    {"abfs://",  "azure",  false},
    {"az://",    "azure",  false},
    // GitHub (gh community ext)
    {"gh://",    "gh",     true},
    // Archive filesystems (community): read a member by inner path, e.g.
    // read_any('zip://archive.zip/dir/file.csv') — the inner format is then
    // auto-detected. Inner paths contain '/', so this is the scheme route, not @.
    {"zip://",   "zipfs",  true},
    {"tar://",   "tarfs",  true},
    {nullptr,    nullptr,  false},
};

static const FilesystemScheme *DetectFilesystemScheme(const string &lower_path) {
  for (idx_t i = 0; FILESYSTEM_SCHEMES[i].scheme != nullptr; i++) {
    if (StringUtil::StartsWith(lower_path, FILESYSTEM_SCHEMES[i].scheme)) {
      return &FILESYSTEM_SCHEMES[i];
    }
  }
  return nullptr;
}

// Dispatch to the appropriate filesystem loader based on the path URI scheme.
static void TryEnsureFilesystem(ClientContext &context, const string &path) {
  auto lower = StringUtil::Lower(path);
  auto *scheme = DetectFilesystemScheme(lower);
  if (!scheme) {
    return;
  }
  if (scheme->community) {
    TryEnsureCommunityFilesystem(context, scheme->extension);
  } else {
    ExtensionHelper::TryAutoLoadExtension(context, scheme->extension);
  }
}

// magic_archive_members(path) -> VARCHAR[]
// Lists the member file paths inside a .zip/.tar archive, ensuring the archive
// filesystem (zipfs/tarfs, both community) is loaded first. Used by read_any to
// produce a candidate list when an archive member is missing/unspecified.
static void MagicArchiveMembersFun(DataChunk &args, ExpressionState &state,
                                   Vector &result) {
  auto &context = state.GetContext();
  auto count = args.size();
  auto &input = args.data[0];

  UnifiedVectorFormat idata;
  input.ToUnifiedFormat(count, idata);
  auto in_strings = UnifiedVectorFormat::GetData<string_t>(idata);

  for (idx_t i = 0; i < count; i++) {
    auto idx = idata.sel->get_index(i);
    if (!idata.validity.RowIsValid(idx)) {
      FlatVector::SetNull(result, i, true);
      continue;
    }
    auto path = in_strings[idx].GetString();
    auto lower = StringUtil::Lower(path);
    string scheme;
    string ext;
    if (StringUtil::EndsWith(lower, ".zip")) {
      scheme = "zip";
      ext = "zipfs";
    } else if (StringUtil::EndsWith(lower, ".tar")) {
      scheme = "tar";
      ext = "tarfs";
    }

    vector<Value> members;
    if (!scheme.empty()) {
      // Ensure the archive filesystem. Install only if autoinstall+community are
      // allowed, but LOAD unconditionally (loading an already-installed extension
      // is harmless) so the membership check is reliable even with autoload off.
      if (!context.db->ExtensionIsLoaded(ext)) {
        try {
          if (Settings::Get<AutoinstallKnownExtensionsSetting>(context) &&
              Settings::Get<AllowCommunityExtensionsSetting>(context)) {
            auto repo = ExtensionRepository::GetRepositoryByUrl(
                "http://community-extensions.duckdb.org");
            ExtensionInstallOptions options;
            options.repository = repo;
            ExtensionHelper::InstallExtension(context, ext, options);
          }
        } catch (...) {
        }
        try {
          ExtensionHelper::LoadExternalExtension(context, ext);
        } catch (...) {
        }
      }
      auto &fs = FileSystem::GetFileSystem(context);
      string prefix = scheme + "://" + path + "/";
      try {
        for (auto &entry : fs.Glob(prefix + "**")) {
          auto &fp = entry.path;
          members.push_back(Value(StringUtil::StartsWith(fp, prefix)
                                      ? fp.substr(prefix.size())
                                      : fp));
        }
      } catch (...) {
        // archive filesystem unavailable / unreadable archive -> empty list
      }
    }
    result.SetValue(i, Value::LIST(LogicalType::VARCHAR, std::move(members)));
  }
  if (count == 1) {
    result.SetVectorType(VectorType::CONSTANT_VECTOR);
  }
}

// Detect which DuckDB extensions are required for the file format only
// (not the filesystem). Mirrors the format-detection logic in read_any.
static vector<string> DetectFormatExtensions(const string &type_str,
                                             const string &mime_str,
                                             const string &file_path) {
  auto lower_path = StringUtil::Lower(file_path);
  auto lower_mime = StringUtil::Lower(mime_str);
  auto lower_type = StringUtil::Lower(type_str);

  // S3 Tables (Iceberg) — detected by ARN prefix (not a file; checked first)
  if (StringUtil::StartsWith(lower_path, "arn:aws:s3tables:")) {
    return {"iceberg"};
  }

  // DuckLake catalog (ducklake: prefix) — read the metadata db as a lakehouse
  if (StringUtil::StartsWith(lower_path, "ducklake:")) {
    return {"ducklake"};
  }

  // MotherDuck (md: prefix)
  if (StringUtil::StartsWith(lower_path, "md:")) {
    return {"motherduck"};
  }

  // MongoDB (mongodb:// connection string) — community storage extension
  if (StringUtil::StartsWith(lower_path, "mongodb://") ||
      StringUtil::StartsWith(lower_path, "mongodb+srv://")) {
    return {"mongo@community"};
  }

  // Spatial — GeoPackage uniquely detectable via mime
  if (StringUtil::Contains(lower_mime, "geopackage")) {
    return {"spatial"};
  }

  // Spatial — detected by extension (magic does not distinguish these formats)
  // NOTE: .gml excluded — GDAL fetches remote XSD schema on open, hangs without network
  // NOTE: .osm excluded — GDAL OSM driver requires a config file, crashes without it
  // NOTE: .gpx excluded — GDAL GPX driver crashes on read in current spatial version
  if (StringUtil::EndsWith(lower_path, ".geojson") ||
      StringUtil::EndsWith(lower_path, ".geojsonl") ||
      StringUtil::EndsWith(lower_path, ".ndgeojson") ||
      StringUtil::EndsWith(lower_path, ".topojson") ||
      StringUtil::EndsWith(lower_path, ".fgb") ||
      StringUtil::EndsWith(lower_path, ".prj") ||
      StringUtil::EndsWith(lower_path, ".shp") ||
      StringUtil::EndsWith(lower_path, ".kml")) {
    return {"spatial"};
  }

  // Vortex (detected by extension)
  if (StringUtil::EndsWith(lower_path, ".vortex")) {
    return {"vortex"};
  }

  // Arrow IPC (detected by extension — magic returns generic 'data')
  if (StringUtil::EndsWith(lower_path, ".arrow") ||
      StringUtil::EndsWith(lower_path, ".arrows") ||
      StringUtil::EndsWith(lower_path, ".ipc")) {
    return {"nanoarrow@community"};
  }

  // Stata/SPSS/SAS via read_stat (community) — detected by extension
  if (StringUtil::EndsWith(lower_path, ".dta") ||
      StringUtil::EndsWith(lower_path, ".sav") ||
      StringUtil::EndsWith(lower_path, ".zsav") ||
      StringUtil::EndsWith(lower_path, ".por") ||
      StringUtil::EndsWith(lower_path, ".sas7bdat") ||
      StringUtil::EndsWith(lower_path, ".xpt")) {
    return {"read_stat@community"};
  }

  // DuckDB database file — read via built-in read_attacheable_database (no extension)
  if (StringUtil::StartsWith(lower_type, "duckdb database file") ||
      StringUtil::EndsWith(lower_path, ".duckdb") ||
      StringUtil::EndsWith(lower_path, ".ddb")) {
    return {};
  }

  // SQLite database file — read via read_attacheable_database (needs sqlite_scanner)
  if (StringUtil::StartsWith(lower_type, "sqlite format 3") ||
      StringUtil::EndsWith(lower_path, ".sqlite") ||
      StringUtil::EndsWith(lower_path, ".sqlite3")) {
    return {"sqlite"};
  }

  // Lance dataset (a directory) — detected by extension
  if (StringUtil::EndsWith(lower_path, ".lance")) {
    return {"lance"};
  }

  // YAML (detected by extension — magic returns text/plain)
  if (StringUtil::EndsWith(lower_path, ".yaml") ||
      StringUtil::EndsWith(lower_path, ".yml")) {
    return {"yaml@community"};
  }

  // Jupyter notebook — detected by extension before generic JSON catch
  if (StringUtil::EndsWith(lower_path, ".ipynb")) {
    return {"json"};
  }

  // HAR (HTTP Archive) — detected by extension before generic JSON catch
  if (StringUtil::EndsWith(lower_path, ".har")) {
    return {"json"};
  }

  // ICS / iCalendar — detected by mime or extension
  if (StringUtil::Contains(lower_mime, "text/calendar") ||
      StringUtil::EndsWith(lower_path, ".ics") ||
      StringUtil::EndsWith(lower_path, ".ical")) {
    return {};
  }

  // JSON (including newline-delimited variants)
  if (StringUtil::Contains(lower_mime, "json") ||
      StringUtil::EndsWith(lower_path, ".json") ||
      StringUtil::EndsWith(lower_path, ".jsonl") ||
      StringUtil::EndsWith(lower_path, ".ndjson")) {
    return {"json"};
  }

  // CSV — built-in, no extension needed
  if (StringUtil::Contains(lower_mime, "text/plain") ||
      StringUtil::Contains(lower_mime, "text/csv")) {
    return {};
  }

  // Parquet — magic type primary, extension fallback for remote/unreadable files
  if (StringUtil::StartsWith(lower_type, "apache parquet") ||
      StringUtil::EndsWith(lower_path, ".parquet")) {
    return {"parquet"};
  }

  // Avro — magic type primary, extension fallback
  if (StringUtil::StartsWith(lower_type, "apache avro") ||
      StringUtil::EndsWith(lower_path, ".avro")) {
    return {"avro"};
  }

  // Excel — magic type primary, extension fallback
  if (StringUtil::StartsWith(lower_type, "microsoft excel") ||
      StringUtil::EndsWith(lower_path, ".xlsx") ||
      StringUtil::EndsWith(lower_path, ".xls")) {
    return {"excel"};
  }

  // ODS (OpenDocument Spreadsheet) — via rusty_sheet
  if (StringUtil::StartsWith(lower_type, "opendocument spreadsheet") ||
      StringUtil::Contains(lower_mime, "opendocument.spreadsheet") ||
      StringUtil::EndsWith(lower_path, ".ods")) {
    return {"rusty_sheet@community"};
  }

  // XML — via webbed (checked after spatial so KML/GML don't match)
  if (StringUtil::Contains(lower_mime, "text/xml") ||
      StringUtil::EndsWith(lower_path, ".xml")) {
    return {"webbed@community"};
  }

  // Blob / unknown — no extension needed
  return {};
}

// Full required-extensions list: filesystem extension (e.g. "gh") prepended
// to the format extension(s). This is what magic_required_extensions() returns.
// Community extensions are reported as "name@community" (so callers install them
// from the community repo); core extensions stay plain "name". The tag is added
// at the definition site: filesystems via the scheme table's `community` flag,
// formats via DetectFormatExtensions returning the tagged name directly.
static vector<string> DetectRequiredExtensions(const string &type_str,
                                               const string &mime_str,
                                               const string &file_path) {
  auto lower_path = StringUtil::Lower(file_path);

  vector<string> result;

  // Filesystem extension (by URI scheme) — prepended before format extensions
  auto *scheme = DetectFilesystemScheme(lower_path);
  if (scheme) {
    result.push_back(scheme->community ? string(scheme->extension) + "@community"
                                       : string(scheme->extension));
  }

  auto format_exts = DetectFormatExtensions(type_str, mime_str, file_path);
  result.insert(result.end(), format_exts.begin(), format_exts.end());
  return result;
}

static void MagicRequiredExtensionsFun(DataChunk &args, ExpressionState &state,
                                       Vector &result) {
  auto &name_vector = args.data[0];
  auto &local = ExecuteFunctionState::GetFunctionState(state)
                    ->Cast<MagicBothLocalState>();
  auto &fs = FileSystem::GetFileSystem(state.GetContext());

  auto list_data = ListVector::GetData(result);
  idx_t child_offset = 0;

  for (idx_t i = 0; i < args.size(); i++) {
    auto val = name_vector.GetValue(i);
    if (val.IsNull()) {
      FlatVector::SetNull(result, i, true);
      list_data[i] = {child_offset, 0};
      continue;
    }

    auto name = val.GetValue<string>();

    // Detect URI-scheme extensions before attempting to open the file so that
    // e.g. magic_required_extensions('gh://...') returns ['gh'] even when the
    // gh extension is not installed and the open would fail.
    auto uri_exts = DetectRequiredExtensions("", "", name);

    // Ensure any filesystem extension (e.g. gh) is loaded before opening
    TryEnsureFilesystem(state.GetContext(), name);

    // Read a buffer from the file for magic detection
    char buffer[1024] = {};
    size_t bytes_read = 0;
    try {
      auto handle = fs.OpenFile(name, FileFlags::FILE_FLAGS_READ);
      bytes_read = fs.Read(*handle, buffer, sizeof(buffer) - 1);
    } catch (...) {
      // File unreadable — return whatever we detected from the URI scheme alone
      list_data[i] = {child_offset, uri_exts.size()};
      for (auto &ext : uri_exts) {
        ListVector::PushBack(result, Value(ext));
        child_offset++;
      }
      continue;
    }

    string type_str, mime_str;
    const char *r;
    r = magic_buffer(local.magic_type_cookie, buffer, bytes_read);
    if (r)
      type_str = r;
    r = magic_buffer(local.magic_mime_cookie, buffer, bytes_read);
    if (r)
      mime_str = r;

    auto exts = DetectRequiredExtensions(type_str, mime_str, name);
    list_data[i] = {child_offset, exts.size()};
    for (auto &ext : exts) {
      ListVector::PushBack(result, Value(ext));
      child_offset++;
    }
  }
}

template <bool MIME>
inline void MagicScalarFun(DataChunk &args, ExpressionState &state,
                           Vector &result) {
  auto &name_vector = args.data[0];
  UnaryExecutor::ExecuteWithNulls<string_t, string_t>(
      name_vector, result, args.size(),
      [&](string_t name, ValidityMask &mask, idx_t idx) {
        if (mask.RowIsValid(idx) == false) {
          return string_t();
        }
        auto &localState = ExecuteFunctionState::GetFunctionState(state)
                               ->Cast<MagicFunctionLocalState<MIME>>();

        string terminated = name.GetString();
        const char *actual_file = terminated.c_str();

        const char *magic_full;

#ifndef MAGIC_ON_BUFFER
        const bool onBuffer = true;
#else
        const bool onBuffer = false;
#endif

        if (onBuffer) {
          auto &fs = FileSystem::GetFileSystem(state.GetContext());
          TryEnsureFilesystem(state.GetContext(), terminated);
          auto handle = fs.OpenFile(actual_file, FileFlags::FILE_FLAGS_READ);
          char buffer[1024] = {};
          auto bytes_read = fs.Read(*handle, buffer, sizeof(buffer) - 1);

          magic_full =
              magic_buffer(localState.magic_cookie, buffer, bytes_read);
        } else {
          magic_full = magic_file(localState.magic_cookie, actual_file);
        }
        if (!magic_full) {
          mask.SetInvalid(idx);
          return string_t();
        }
        string X(magic_full);

        return StringVector::AddString(result, X);
      });
}

static void LoadInternal(ExtensionLoader &loader) {
  loader.SetDescription("Detect file types via magic library");

  // Register magic_type
  {
    ScalarFunction fn("magic_type", {LogicalType::VARCHAR},
                      LogicalType::VARCHAR, MagicScalarFun<false>, nullptr,
                      nullptr, nullptr, MagicFunctionLocalStateFun<false>);
    CreateScalarFunctionInfo info(fn);
    FunctionDescription desc;
    desc.parameter_names = {"file_path"};
    desc.parameter_types = {LogicalType::VARCHAR};
    desc.description =
        "Returns the file type description for the given file path using the "
        "libmagic database (e.g. 'Apache Parquet', 'JSON data').";
    desc.examples = {
        "SELECT magic_type('myfile.parquet');",
        "SELECT file, magic_type(file) AS type FROM glob('data/**/*');",
    };
    desc.categories = {"magic"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
  }

  // Register magic_mime
  {
    ScalarFunction fn("magic_mime", {LogicalType::VARCHAR},
                      LogicalType::VARCHAR, MagicScalarFun<true>, nullptr,
                      nullptr, nullptr, MagicFunctionLocalStateFun<true>);
    CreateScalarFunctionInfo info(fn);
    FunctionDescription desc;
    desc.parameter_names = {"file_path"};
    desc.parameter_types = {LogicalType::VARCHAR};
    desc.description =
        "Returns the MIME type for the given file path using the libmagic "
        "database (e.g. 'application/json', 'text/plain').";
    desc.examples = {
        "SELECT magic_mime('myfile.json');",
        "SELECT file, magic_mime(file) AS mime FROM glob('data/**/*');",
    };
    desc.categories = {"magic"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
  }

  // Register magic_required_extensions
  {
    ScalarFunction fn("magic_required_extensions", {LogicalType::VARCHAR},
                      LogicalType::LIST(LogicalType::VARCHAR),
                      MagicRequiredExtensionsFun, nullptr, nullptr, nullptr,
                      MagicBothLocalStateFun);
    CreateScalarFunctionInfo info(fn);
    FunctionDescription desc;
    desc.parameter_names = {"file_path"};
    desc.parameter_types = {LogicalType::VARCHAR};
    desc.description =
        "Returns the list of DuckDB extensions that must be loaded before "
        "reading the given file with read_any(). Returns an empty list for "
        "built-in formats (CSV, blob).";
    desc.examples = {
        "SELECT magic_required_extensions('myfile.json');",
        "SELECT file, magic_required_extensions(file) AS exts FROM "
        "glob('data/**/*');",
    };
    desc.categories = {"magic"};
    info.descriptions.push_back(std::move(desc));
    loader.RegisterFunction(std::move(info));
  }

  // Register read_attacheable_database (generic hidden-attach + scan)
  loader.RegisterFunction(ReadAttacheableDatabase::GetFunction());

  // Register split_into_components (path@selector splitter)
  loader.RegisterFunction(SplitIntoComponents::GetFunction());

  // Register magic_archive_members (zip/tar member listing for candidate errors)
  loader.RegisterFunction(ScalarFunction("magic_archive_members",
                                         {LogicalType::VARCHAR},
                                         LogicalType::LIST(LogicalType::VARCHAR),
                                         MagicArchiveMembersFun));

  // Register read_any table macro
  for (idx_t index = 0;
       dynamic_sql_examples_table_macros[index].name != nullptr; index++) {
    auto table_info = DefaultTableFunctionGenerator::CreateTableMacroInfo(
        dynamic_sql_examples_table_macros[index]);
    loader.RegisterFunction(*table_info);
  }
}

void MagicExtension::Load(ExtensionLoader &loader) { LoadInternal(loader); }
std::string MagicExtension::Name() { return "magic"; }

std::string MagicExtension::Version() const {
#ifdef EXT_VERSION_MAGIC
  return EXT_VERSION_MAGIC;
#else
  return "";
#endif
}

} // namespace duckdb

extern "C" {

DUCKDB_CPP_EXTENSION_ENTRY(magic, loader) { duckdb::LoadInternal(loader); }
}

#ifndef DUCKDB_EXTENSION_MAIN
#error DUCKDB_EXTENSION_MAIN not defined
#endif
