# magic

Magic lets you examine files to determine their type (via [libmagic](https://man7.org/linux/man-pages/man3/libmagic.3.html)), and — built on top of that — read **almost anything** with a single function, `read_any('<str>')`: local and remote files, databases and lakehouses, archives, and cloud resources, with the right reader picked automatically.

```sql
--- Install (once)
INSTALL magic FROM community;

--- Update (to check for updates)
UPDATE EXTENSIONS (magic);

--- Load
LOAD magic;
```

## Inspecting files

```sql
--- Autodetected type/mime for files in the current folder
SELECT magic_mime(file), magic_type(file), file FROM glob('*');

--- Remote files (httpfs is auto-loaded on demand)
SELECT magic_type('https://raw.githubusercontent.com/duckdb/duckdb/main/data/parquet-testing/adam_genotypes.parquet');

--- Which extensions does a file need to be read with read_any()?
SELECT magic_required_extensions('s3://my-bucket/data.avro');   -- [httpfs, avro]
SELECT magic_required_extensions('config.yaml');                -- [yaml@community]
```

`magic_required_extensions` tags community extensions as `name@community` (vs plain `name` for core), so an installer knows to use `INSTALL name FROM community`.

## `read_any('<str>')`

`read_any` resolves the string, picks the format/backend, and returns rows. Auto-detection uses the file's **magic bytes**, its **name** (extension or URI/ARN prefix), or you can force it with `format:='<name>'`.

```sql
FROM read_any('map.gpkg');                      -- spatial (GeoPackage), sniffed by content (magic)
FROM read_any('s3://bucket/events.jsonl');      -- remote (httpfs auto-loaded)
FROM read_any('mystery_file', format:='csv');   -- explicit override
```

### Selecting a sub-object: `path@selector`

For sources that contain more than one table/sheet/member, append `@<selector>`:

```sql
FROM read_any('sales.duckdb@main.orders');      -- a table inside a DuckDB database
FROM read_any('archive.zip@data/january.csv');  -- a member inside an archive
FROM read_any('postgres://user:pass@host/db@public.customers');
```

The `@` is split **authority-aware**: a credentials `@` in a URL (`user:pass@host`) is never mistaken for a selector, and a selector may itself contain `/` (e.g. `archive.zip@dir/file.csv`).

Ask a source what it contains by leaving the selector off — you get the candidate list (same as for a missing one):

```sql
FROM read_any('sales.duckdb');
-- Error: Database "sales.duckdb" has multiple tables. Candidates: main.orders, main.customers, ...

FROM read_any('archive.zip@nope.csv');
-- Error: archive "archive.zip" has no member "nope.csv". Available members: data/january.csv, ...
```

### Databases & lakehouses (catalogs)

`read_any('<connection>@<schema>.<table>')` attaches the catalog read-only, picks the table, and scans it. (Auth, where needed, requires a normal `CREATE SECRET`, just like a manual `ATTACH`.)

| Backend | String shape | Requires |
|---|---|---|
| DuckDB file | `sales.duckdb@schema.table` | core |
| SQLite | `app.sqlite@table` | `sqlite` |
| Postgres | `postgres://…@schema.table` | `postgres` |
| MySQL | `mysql://…@schema.table` | `mysql` |
| MongoDB | `mongodb://…@db.collection` | `mongo@community` |
| Iceberg (S3 Tables) | `arn:aws:s3tables:…:bucket/name@ns.table` | `iceberg` |
| DuckLake | `ducklake:metadata.db@schema.table` | `ducklake` |
| MotherDuck | `md:mydb@schema.table` | `motherduck` |
| Quack (client/server) | `quack:host:port@schema.table` | `quack` |

### Remote, archives & cloud resources

| Input | Handled as |
|---|---|
| `s3://`, `https://`, `gcs://`, `r2://`, `az://`, `abfss://`, `gh://` | filesystem → inner format auto-detected (`httpfs`/`azure`/`gh@community`) |
| `arn:aws:s3:::bucket/key` | remapped to `s3://bucket/key` (region/credentials via httpfs/aws) |
| `archive.zip@member` / `zip://archive.zip/member` | member read from a zip (`zipfs@community`); nests over http: `zip://https://…/x.zip/a.csv` |
| `archive.tar@member` / `tar://…` | member read from a tar (`tarfs@community`) |

### File formats

| Format | Detection | Requires |
|---|---|---|
| `csv` / `tsv` | magic (`text/plain`, `text/csv`) + ext `.csv` `.tsv` | core |
| `json` | magic (`json`) + ext `.json` `.jsonl` `.ndjson` | `json` |
| `parquet` | magic (`Apache Parquet`) + ext `.parquet` | `parquet` |
| `avro` | magic (`Apache Avro`) + ext `.avro` | `avro` |
| `arrow` | ext `.arrow` `.arrows` `.ipc` | `nanoarrow@community` |
| `excel` | magic (`Microsoft Excel`) + ext `.xlsx` `.xls` | `excel` |
| `ods` | magic (`OpenDocument Spreadsheet`) + ext `.ods` | `rusty_sheet@community` |
| `xml` | magic (`text/xml`) + ext `.xml` | `webbed@community` |
| `yaml` | ext `.yaml` `.yml` | `yaml@community` |
| `ics` | magic (`text/calendar`) + ext `.ics` `.ical` | core |
| `ipynb` | ext `.ipynb` (one row per cell) | `json` |
| `har` | ext `.har` (one row per request) | `json` |
| `stat` | ext `.dta` `.sav` `.zsav` `.por` `.sas7bdat` `.xpt` (Stata/SPSS/SAS) | `read_stat@community` |
| `vortex` | ext `.vortex` | `vortex` |
| `lance` | ext `.lance` (a directory) | `lance` |
| `spatial` | GeoJSON/Shapefile/KML/FlatGeobuf/GeoPackage/… | `spatial` |
| `blob` | fallback / explicit — the raw file as one `BLOB` | core |

### Discovering what `read_any` supports

Query the capability table instead of guessing:

```sql
FROM magic_capabilities();                                          -- everything
SELECT * FROM magic_capabilities() WHERE kind = 'catalog';          -- the @schema.table backends
SELECT format FROM magic_capabilities() WHERE list_contains(pattern, '.parquet');
SELECT format FROM magic_capabilities() WHERE extension LIKE '%@community';
```

Columns: `format`, `kind` (`file`/`catalog`/`filesystem`/`archive`), `magic` (`VARCHAR[]` of libmagic types/mimes, or `NULL`), `pattern` (`VARCHAR[]` of extensions/URI prefixes, or `NULL`), `extension` (required extension, `NULL` if core). Both `magic` and `pattern` `NULL` means the format is explicit-`format:=` only.

---

This repository is based on https://github.com/duckdb/extension-template, check it out if you want to build and ship your own DuckDB extension.

### Building

```
VCPKG_TOOLCHAIN_PATH='/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake' GEN=ninja make
-- or without ninja
VCPKG_TOOLCHAIN_PATH='/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake' make
```
