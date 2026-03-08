* ~~lancedb~~ (done: lance community extension, `.lance` extension detection)
* ??

* add file@table_name -> should look for table in the multi-table format

* ~~autoload relevant filesystems (e.g. httpfs for remote URLs, s3 for s3:// paths) before attempting read_any~~ (done: TryEnsureFilesystem dispatches by URI scheme; magic_required_extensions returns [fs_ext, format_ext] combos)
