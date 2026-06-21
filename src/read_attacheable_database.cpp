#include "read_attacheable_database.hpp"

#include "duckdb/catalog/catalog.hpp"
#include "duckdb/catalog/catalog_entry/schema_catalog_entry.hpp"
#include "duckdb/catalog/catalog_entry/table_catalog_entry.hpp"
#include "duckdb/catalog/entry_lookup_info.hpp"
#include "duckdb/common/enums/access_mode.hpp"
#include "duckdb/common/string_util.hpp"
#include "duckdb/common/types/value.hpp"
#include "duckdb/execution/execution_context.hpp"
#include "duckdb/main/attached_database.hpp"
#include "duckdb/main/client_context.hpp"
#include "duckdb/main/database_manager.hpp"
#include "duckdb/parser/parsed_data/attach_info.hpp"
#include "duckdb/parser/qualified_name.hpp"

namespace duckdb {

//===--------------------------------------------------------------------===//
// Hidden attach wrapper — attaches in ctor lifetime, detaches in dtor.
// Mirrors read_duckdb's AttachedDatabaseWrapper.
//===--------------------------------------------------------------------===//
struct AttacheableDatabaseWrapper {
	AttacheableDatabaseWrapper(ClientContext &context_p, shared_ptr<AttachedDatabase> attached_database_p)
	    : context(context_p), attached_database(std::move(attached_database_p)) {
	}
	~AttacheableDatabaseWrapper() {
		if (attached_database) {
			auto &db_manager = DatabaseManager::Get(context);
			auto name = attached_database->GetName();
			attached_database.reset();
			db_manager.DetachDatabase(context, name, OnEntryNotFound::RETURN_NULL);
		}
	}

	ClientContext &context;
	shared_ptr<AttachedDatabase> attached_database;
};

//===--------------------------------------------------------------------===//
// Bind / state
//===--------------------------------------------------------------------===//
struct ReadAttacheableBindData : public TableFunctionData {
	shared_ptr<AttacheableDatabaseWrapper> db_wrapper;
	string schema_name;
	string table_name;
	vector<LogicalType> types;
	vector<string> names;
	// the underlying table's scan, bound during bind() (prepares lazy catalogs)
	TableFunction scan_function;
	unique_ptr<FunctionData> scan_bind_data;

	unique_ptr<FunctionData> Copy() const override {
		auto result = make_uniq<ReadAttacheableBindData>();
		result->db_wrapper = db_wrapper;
		result->schema_name = schema_name;
		result->table_name = table_name;
		result->types = types;
		result->names = names;
		result->scan_function = scan_function;
		if (scan_bind_data) {
			result->scan_bind_data = scan_bind_data->Copy();
		}
		return std::move(result);
	}
	bool Equals(const FunctionData &other_p) const override {
		return this == &other_p;
	}
};

struct ReadAttacheableGlobalState : public GlobalTableFunctionState {
	unique_ptr<GlobalTableFunctionState> inner_global;
	vector<ColumnIndex> column_indexes;
};

struct ReadAttacheableLocalState : public LocalTableFunctionState {
	unique_ptr<LocalTableFunctionState> inner_local;
};

//===--------------------------------------------------------------------===//
// Helpers
//===--------------------------------------------------------------------===//
static void ParseOptionsMap(const Value &map_value, unordered_map<string, Value> &out) {
	if (map_value.IsNull()) {
		return;
	}
	auto &entries = ListValue::GetChildren(map_value);
	for (auto &entry : entries) {
		auto &kv = StructValue::GetChildren(entry); // [key, value]
		auto key = StringValue::Get(kv[0].DefaultCastAs(LogicalType::VARCHAR));
		out[StringUtil::Lower(key)] = kv[1];
	}
}

static string BuildCandidateList(Catalog &catalog, ClientContext &context) {
	// Only user-facing tables, capped — internal schemas (pg_catalog, ...) are huge
	// and irrelevant as selection candidates.
	const idx_t MAX_CANDIDATES = 50;
	vector<string> candidates;
	catalog.ScanSchemas(context, [&](SchemaCatalogEntry &schema) {
		if (schema.internal || candidates.size() >= MAX_CANDIDATES) {
			return;
		}
		schema.Scan(context, CatalogType::TABLE_ENTRY, [&](CatalogEntry &entry) {
			if (candidates.size() >= MAX_CANDIDATES) {
				return;
			}
			if (entry.type == CatalogType::TABLE_ENTRY && !entry.internal) {
				candidates.push_back(schema.name + "." + entry.name);
			}
		});
	});
	if (candidates.empty()) {
		return string();
	}
	return "\nCandidates: " + StringUtil::Join(candidates, ", ");
}

//===--------------------------------------------------------------------===//
// Bind: attach (hidden, read-only) → enumerate → decide single table
//===--------------------------------------------------------------------===//
static unique_ptr<FunctionData> ReadAttacheableBind(ClientContext &context, TableFunctionBindInput &input,
                                                    vector<LogicalType> &return_types, vector<string> &names) {
	auto path = StringValue::Get(input.inputs[0]);

	string db_type = "duckdb";
	string schema_filter;
	string table_filter;
	string relative_path;
	unordered_map<string, Value> attach_kv;

	for (auto &param : input.named_parameters) {
		auto key = StringUtil::Lower(param.first);
		if (key == "type") {
			db_type = StringUtil::Lower(StringValue::Get(param.second));
		} else if (key == "relative_path") {
			relative_path = StringValue::Get(param.second);
		} else if (key == "options") {
			ParseOptionsMap(param.second, attach_kv);
		}
	}

	// relative_path like "schema.table" (or just "table") selects the table; it
	// is parsed with DuckDB's own qualified-name parser (handles quoting, e.g.
	// 'ns."weird.name"'). Empty relative_path => single-table auto-selection.
	if (!relative_path.empty()) {
		auto qname = QualifiedName::Parse(relative_path);
		schema_filter = qname.schema; // INVALID_SCHEMA ("") when there is no dot
		table_filter = qname.name;
	}

	// Build attach options: TYPE + extra options + read-only + hidden.
	attach_kv["type"] = Value(db_type);
	attach_kv["read_only"] = Value::BOOLEAN(true);
	AttachOptions attach_options(attach_kv, AccessMode::READ_ONLY);
	attach_options.visibility = AttachVisibility::HIDDEN;

	AttachInfo info;
	info.path = path;
	// invalid UTF-8 prefix so a user cannot collide with this hidden name
	info.name = "\x80__magic_attacheable_" + db_type + "_" + path;
	info.on_conflict = OnCreateConflict::IGNORE_ON_CONFLICT;
	// Storage extensions (e.g. iceberg) read their options from AttachInfo.options,
	// NOT AttachOptions.options. Mirror what the `ATTACH ... (TYPE x, ...)` parser
	// does and populate both from the same kv map.
	info.options = attach_kv;

	auto &db_manager = DatabaseManager::Get(context);
	auto attached = db_manager.AttachDatabase(context, info, attach_options);
	auto wrapper = make_shared_ptr<AttacheableDatabaseWrapper>(context, std::move(attached));

	auto &catalog = wrapper->attached_database->GetCatalog();

	// Resolve the target table.
	optional_ptr<TableCatalogEntry> table_ptr;
	if (!table_filter.empty()) {
		// Explicit selection: look the table up directly — no full catalog
		// enumeration (important for large catalogs like iceberg, and it returns
		// the real entry rather than a dummy). Schema defaults to the catalog's
		// default schema when relative_path carried no '.'.
		auto schema = schema_filter.empty() ? string(DEFAULT_SCHEMA) : schema_filter;
		table_ptr =
		    catalog.GetEntry<TableCatalogEntry>(context, schema, table_filter, OnEntryNotFound::RETURN_NULL);
		if (!table_ptr) {
			throw BinderException("Database \"%s\" (type \"%s\") has no table \"%s\"%s", path, db_type, relative_path,
			                      BuildCandidateList(catalog, context));
		}
	} else {
		// No selection: enumerate, auto-pick the sole table, else list candidates.
		// Skip internal schemas/tables (pg_catalog, information_schema, ...) — for a
		// remote catalog like postgres these number in the hundreds and resolving
		// each one is a network round-trip. Early-exit once we know it is >1 table.
		vector<reference<TableCatalogEntry>> matches;
		catalog.ScanSchemas(context, [&](SchemaCatalogEntry &schema) {
			if (schema.internal || matches.size() >= 2) {
				return;
			}
			schema.Scan(context, CatalogType::TABLE_ENTRY, [&](CatalogEntry &entry) {
				if (matches.size() >= 2) {
					return;
				}
				if (entry.type == CatalogType::TABLE_ENTRY && !entry.internal) {
					matches.push_back(entry.Cast<TableCatalogEntry>());
				}
			});
		});
		if (matches.size() != 1) {
			string err = matches.empty() ? "does not have any tables" : "has multiple tables";
			throw BinderException(
			    "Database \"%s\" (type \"%s\") %s\nSelect a table using relative_path:='<schema.table>' (or "
			    "read_any('<file>@<table>'))%s",
			    path, db_type, err, BuildCandidateList(catalog, context));
		}
		table_ptr = &matches[0].get();
	}
	auto &table = *table_ptr;

	auto result = make_uniq<ReadAttacheableBindData>();
	result->db_wrapper = wrapper;
	result->schema_name = table.ParentSchema().name;
	result->table_name = table.name;

	// Bind the underlying scan here (the 3-arg overload; catalogs like iceberg
	// prepare their scan + populate columns at this point). The 2-arg overload
	// throws for such catalogs; for plain tables the base delegates to it.
	EntryLookupInfo scan_lookup(CatalogType::TABLE_ENTRY, result->table_name);
	result->scan_function = table.GetScanFunction(context, result->scan_bind_data, scan_lookup);

	for (auto &col : table.GetColumns().Logical()) {
		return_types.push_back(col.Type());
		names.push_back(col.Name());
	}
	result->types = return_types;
	result->names = names;
	return std::move(result);
}

//===--------------------------------------------------------------------===//
// Scan delegation to the underlying table's scan function
//===--------------------------------------------------------------------===//
static unique_ptr<GlobalTableFunctionState> ReadAttacheableInitGlobal(ClientContext &context,
                                                                      TableFunctionInitInput &input) {
	auto &bind_data = input.bind_data->Cast<ReadAttacheableBindData>();
	auto gstate = make_uniq<ReadAttacheableGlobalState>();

	// scan all of the table's columns; the executor projects above us.
	for (idx_t i = 0; i < bind_data.types.size(); i++) {
		gstate->column_indexes.emplace_back(i);
	}

	TableFunctionInitInput inner_input(bind_data.scan_bind_data.get(), gstate->column_indexes, vector<idx_t>(), nullptr);
	gstate->inner_global = bind_data.scan_function.init_global(context, inner_input);
	return std::move(gstate);
}

static unique_ptr<LocalTableFunctionState> ReadAttacheableInitLocal(ExecutionContext &context,
                                                                    TableFunctionInitInput &input,
                                                                    GlobalTableFunctionState *global_state) {
	auto &bind_data = input.bind_data->Cast<ReadAttacheableBindData>();
	auto &gstate = global_state->Cast<ReadAttacheableGlobalState>();
	auto lstate = make_uniq<ReadAttacheableLocalState>();
	if (bind_data.scan_function.init_local) {
		TableFunctionInitInput inner_input(bind_data.scan_bind_data.get(), gstate.column_indexes, vector<idx_t>(),
		                                   nullptr);
		lstate->inner_local = bind_data.scan_function.init_local(context, inner_input, gstate.inner_global.get());
	}
	return std::move(lstate);
}

static void ReadAttacheableScan(ClientContext &context, TableFunctionInput &data, DataChunk &output) {
	auto &bind_data = data.bind_data->Cast<ReadAttacheableBindData>();
	auto &gstate = data.global_state->Cast<ReadAttacheableGlobalState>();
	auto &lstate = data.local_state->Cast<ReadAttacheableLocalState>();

	TableFunctionInput inner_input(bind_data.scan_bind_data.get(), lstate.inner_local.get(), gstate.inner_global.get());
	bind_data.scan_function.function(context, inner_input, output);
}

//===--------------------------------------------------------------------===//
// split_into_components(str) -> STRUCT(path, selector)
//===--------------------------------------------------------------------===//
// Split on the LAST '@' that appears after the last path separator. This skips
// a credentials '@' (postgres://user:pass@host/db) and a directory-name '@'
// (./a@b/file.db), and never touches an HTTP query string (?...).
static void SplitPathSelector(const string &input, string &path, string &selector) {
	auto at = input.find_last_of('@');
	if (at == string::npos) {
		path = input;
		selector = "";
		return;
	}
	auto last_sep = input.find_last_of("/\\");
	if (last_sep != string::npos && at < last_sep) {
		// the '@' is part of the path (credentials / directory name), not a selector
		path = input;
		selector = "";
		return;
	}
	path = input.substr(0, at);
	selector = input.substr(at + 1);
}

static void SplitIntoComponentsFun(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &input = args.data[0];

	UnifiedVectorFormat idata;
	input.ToUnifiedFormat(count, idata);
	auto in_strings = UnifiedVectorFormat::GetData<string_t>(idata);

	auto &children = StructVector::GetEntries(result);
	auto &path_child = *children[0];
	auto &selector_child = *children[1];

	for (idx_t i = 0; i < count; i++) {
		auto idx = idata.sel->get_index(i);
		if (!idata.validity.RowIsValid(idx)) {
			FlatVector::SetNull(result, i, true);
			continue;
		}
		string path;
		string selector;
		SplitPathSelector(in_strings[idx].GetString(), path, selector);
		path_child.SetValue(i, Value(path));
		selector_child.SetValue(i, Value(selector));
	}
	if (count == 1) {
		result.SetVectorType(VectorType::CONSTANT_VECTOR);
	}
}

ScalarFunction SplitIntoComponents::GetFunction() {
	child_list_t<LogicalType> struct_children;
	struct_children.emplace_back("path", LogicalType::VARCHAR);
	struct_children.emplace_back("selector", LogicalType::VARCHAR);
	auto return_type = LogicalType::STRUCT(std::move(struct_children));
	return ScalarFunction("split_into_components", {LogicalType::VARCHAR}, return_type, SplitIntoComponentsFun);
}

//===--------------------------------------------------------------------===//
// Registration
//===--------------------------------------------------------------------===//
TableFunction ReadAttacheableDatabase::GetFunction() {
	TableFunction fn("read_attacheable_database", {LogicalType::VARCHAR}, ReadAttacheableScan, ReadAttacheableBind,
	                 ReadAttacheableInitGlobal, ReadAttacheableInitLocal);
	fn.named_parameters["type"] = LogicalType::VARCHAR;
	fn.named_parameters["relative_path"] = LogicalType::VARCHAR;
	fn.named_parameters["options"] = LogicalType::MAP(LogicalType::VARCHAR, LogicalType::VARCHAR);
	return fn;
}

} // namespace duckdb
