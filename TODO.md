* add file@table_name -> should look for table in the multi-table format
* spatial requires explicit loading: `read_any` uses `st_read()` in a CTE that is bound at
  macro-expansion time, so the `spatial` extension must already be loaded before calling
  `read_any` on any file — even non-spatial ones (DuckDB resolves all CTE functions at bind
  time). Workaround: call `magic_load_extensions(file_path)` before `read_any` (done ✓).
  Proper fixes still outstanding:
    b) Convert `read_any` from a SQL macro to a C++ table function so it can `LOAD spatial`
       dynamically before dispatching to `st_read`.
    c) Split `read_any` into a spatial and non-spatial path at the SQL layer to avoid binding
       `st_read` unless the file is actually spatial.
