#define DUCKDB_EXTENSION_MAIN

#include <magic.h>

#define STATIC_MAGIC_FILE

#ifdef STATIC_MAGIC_FILE
#include "magic_mgc.hpp"
#endif
#include "magic_extension.hpp"
#include "duckdb.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/parser/parsed_data/create_scalar_function_info.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"

namespace duckdb {

// clang-format off
static const DefaultTableMacro dynamic_sql_examples_table_macros[] = {
    {DEFAULT_SCHEMA, "read_any", {"file_name", nullptr}, {{"format", "'auto'"}, {nullptr, nullptr}}, R"(
----CREATE OR REPLACE MACRO read_any(file_name, format:='auto') AS TABLE (
       WITH "json_case" as (FROM read_json_auto(file_name))
           , "csv_case" as (FROM read_csv(file_name))
           , "parquet_case" as (FROM read_parquet(file_name))
           , "avro_case" as (FROM read_avro(file_name))
           , "blob_case" as (FROM read_blob(file_name))
           , "spatial_case" as (FROM st_read(file_name))
           , "vortex_case" as (FROM read_vortex(file_name))
           , "excel_case" as (FROM read_xlsx(file_name))
       -- TODO (post v1.5.0): add support for community extensions only available on stable releases:
       --   - Arrow IPC (.arrow)  via nanoarrow:  magic returns 'data', detect by file extension
       --   - XML                 via webbed:      magic returns 'text/xml', detect via magic_mime
       --   - HDF5 (.h5/.hdf5)   via h5db:        magic returns 'application/x-hdf5', detect via magic_mime
       --   - ODS                 via rusty_sheet: magic returns 'OpenDocument Spreadsheet', detect via magic_type
       FROM query_table(
             CASE
               WHEN format=='blob' THEN 'blob_case'
               WHEN format == 'spatial' OR format LIKE 'geo%' OR (format=='auto' AND (file_name ILIKE '%.geojson' OR file_name ILIKE '%.fgb' OR file_name ILIKE '%.prj' OR file_name ILIKE '%.shp')) THEN 'spatial_case'
               WHEN format=='json' OR (format=='auto' AND magic_mime(file_name) ILIKE '%json') OR (file_name LIKE '%.json') THEN 'json_case'
               WHEN format=='csv' OR (format=='auto' AND (magic_mime(file_name) ILIKE 'text/plain' OR magic_mime(file_name) ILIKE 'text/csv')) THEN 'csv_case'
               WHEN format=='parquet' OR (format=='auto' AND magic_type(file_name) ILIKE 'Apache Parquet%') THEN 'parquet_case'
               WHEN format=='avro' OR (format=='auto' AND magic_type(file_name) ILIKE 'Apache Avro%') THEN 'avro_case'
               WHEN format=='vortex' OR (format=='auto' AND file_name ILIKE '%.vortex') THEN 'vortex_case'
               WHEN format=='excel' OR format=='xlsx' OR (format=='auto' AND magic_type(file_name) ILIKE 'Microsoft Excel%') THEN 'excel_case'
               WHEN format=='auto' THEN error('read_any can not auto recognize a valid format, try explicitly: FROM read_any("' || file_name ||'", format:="csv"), explcitly supported formats are csv, json, parquet, avro, vortex, excel, spatial and blob')
             ELSE error('read_any explicitly provided format is not one of: csv | json | parquet | avro | vortex | excel | blob | spatial (or geo alias) | auto"')
             END
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

// Map (type_str, mime_str, file_path) to the list of DuckDB extension names
// that must be loaded before calling read_any() on the file.
// Mirrors the format-detection logic in the read_any table macro.
static vector<string> DetectRequiredExtensions(const string &type_str,
                                               const string &mime_str,
                                               const string &file_path) {
  auto lower_path = StringUtil::Lower(file_path);
  auto lower_mime = StringUtil::Lower(mime_str);
  auto lower_type = StringUtil::Lower(type_str);

  // Spatial (detected by extension — magic does not distinguish these formats)
  if (StringUtil::EndsWith(lower_path, ".geojson") ||
      StringUtil::EndsWith(lower_path, ".fgb") ||
      StringUtil::EndsWith(lower_path, ".prj") ||
      StringUtil::EndsWith(lower_path, ".shp")) {
    return {"spatial"};
  }

  // Vortex (detected by extension)
  if (StringUtil::EndsWith(lower_path, ".vortex")) {
    return {"vortex"};
  }

  // JSON
  if (StringUtil::Contains(lower_mime, "json") ||
      StringUtil::EndsWith(lower_path, ".json")) {
    return {"json"};
  }

  // CSV — built-in, no extension needed
  if (StringUtil::Contains(lower_mime, "text/plain") ||
      StringUtil::Contains(lower_mime, "text/csv")) {
    return {};
  }

  // Parquet
  if (StringUtil::StartsWith(lower_type, "apache parquet")) {
    return {"parquet"};
  }

  // Avro
  if (StringUtil::StartsWith(lower_type, "apache avro")) {
    return {"avro"};
  }

  // Excel
  if (StringUtil::StartsWith(lower_type, "microsoft excel")) {
    return {"excel"};
  }

  // Blob / unknown — no extension needed
  return {};
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

    // Read a buffer from the file for magic detection
    char buffer[1024] = {};
    size_t bytes_read = 0;
    try {
      auto handle = fs.OpenFile(name, FileFlags::FILE_FLAGS_READ);
      bytes_read = fs.Read(*handle, buffer, sizeof(buffer) - 1);
    } catch (...) {
      list_data[i] = {child_offset, 0};
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
