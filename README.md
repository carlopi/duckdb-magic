# magic

This extension, Magic, allow you to examine files and determine their type, based on https://man7.org/linux/man-pages/man3/libmagic.3.html linux utility.


```sql
--- Install (once)
INSTALL magic FROM community;

--- Update (to check for updates)
UPDATE EXTENSIONS (magic);

--- Load
LOAD magic;
```

Example, discover which mime_types is a given [remote] file[s]:
```sql
--- Discover autodetected types for files in your current folder
SELECT magic_mime(file), magic_type(file), file
    FROM glob('*');

--- Needs to be performed once per session to query remote files
LOAD httpfs;

--- Discover autodetected types for a remote file
SELECT magic_mime(file), magic_type(file), file
    FROM glob('https://raw.githubusercontent.com/duckdb/duckdb/main/data/parquet-testing/adam_genotypes.parquet');
```

Example, read any file with autodetection (on the content or the name):
```sql
--- Needs to be performed once per session to query remote files
LOAD httpfs;

FROM read_any('https://raw.githubusercontent.com/duckdb/duckdb/main/data/parquet-testing/adam_genotypes.parquet');
```

### Supported formats in `read_any`

| Format | Description | Detection | Requires |
|--------|-------------|-----------|----------|
| `csv` | Delimited text (CSV/TSV) | magic mime (`text/plain`, `text/csv`) | core |
| `json` | JSON and newline-delimited JSON | magic mime + file ext (`.json`, `.jsonl`, `.ndjson`) | `json` |
| `parquet` | Apache Parquet columnar format | magic type (`Apache Parquet`) | `parquet` |
| `avro` | Apache Avro serialization format | magic type (`Apache Avro`) | `avro` |
| `excel` | Microsoft Excel workbooks | magic type (`Microsoft Excel`) | `excel` |
| `ods` | OpenDocument Spreadsheet | magic type + file ext (`.ods`) | `rusty_sheet` (community) |
| `yaml` | YAML data files | file ext (`.yaml`, `.yml`) | `yaml` (community) |
| `xml` | XML documents | magic mime (`text/xml`) + file ext (`.xml`) | `webbed` (community) |
| `ics` | iCalendar / calendar events | magic mime (`text/calendar`) + file ext (`.ics`, `.ical`) | core |
| `ipynb` | Jupyter notebooks — one row per cell | file ext (`.ipynb`) | `json` |
| `har` | HTTP Archive — one row per request | file ext (`.har`) | `json` |
| `spatial` | GeoJSON / GeoJSONL / NDGeoJSON | file ext (`.geojson`, `.geojsonl`, `.ndgeojson`) | `spatial` |
| `spatial` | TopoJSON | file ext (`.topojson`) | `spatial` |
| `spatial` | FlatGeobuf | file ext (`.fgb`) | `spatial` |
| `spatial` | Shapefile | file ext (`.shp`, `.prj`) | `spatial` |
| `spatial` | KML | file ext (`.kml`) | `spatial` |
| `spatial` | GeoPackage | magic mime (`geopackage`) | `spatial` |
| `spatial` | Other GDAL-supported formats | explicit `format:='spatial'` only | `spatial` |
| `vortex` | Vortex columnar format | file ext (`.vortex`) | `vortex` (community) |
| `lance` | Lance columnar format | file ext (`.lance`) | `lance` (community) |
| `blob` | Raw binary — returns the file as a single blob value | fallback / explicit | core |

Use `format:='<name>'` to override auto-detection, e.g. `FROM read_any('myfile', format:='csv')`.

This repository is based on https://github.com/duckdb/extension-template, check it out if you want to build and ship your own DuckDB extension.

---

### Building

```
VCPKG_TOOLCHAIN_PATH='/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake' GEN=ninja make
-- or without ninja
VCPKG_TOOLCHAIN_PATH='/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake' make
```
