//===----------------------------------------------------------------------===//
//                         magic extension
//
// read_attacheable_database.hpp
//
// Generic "attach a catalog (any storage TYPE) read-only + hidden, pick a
// single table, scan it" table function. A type-parameterized generalization of
// the in-tree read_duckdb. Intended to later be upstreamed into duckdb/duckdb.
//===----------------------------------------------------------------------===//

#pragma once

#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"

namespace duckdb {

struct ReadAttacheableDatabase {
	//! read_attacheable_database(path, type:='duckdb', relative_path:='', options:=MAP{})
	//! Attaches `path` read-only + hidden as a `type` catalog, selects the table
	//! named by `relative_path` (or the sole table), and scans it.
	static TableFunction GetFunction();
};

struct SplitIntoComponents {
	//! split_into_components(str) -> STRUCT(path VARCHAR, selector VARCHAR)
	//! Splits a `path@selector` input on the trailing `@` (last `@`, after the
	//! last path separator). The `selector` is returned raw/uninterpreted.
	static ScalarFunction GetFunction();
};

} // namespace duckdb
