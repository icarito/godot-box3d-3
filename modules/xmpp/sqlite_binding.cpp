/*************************************************************************/
/*  sqlite_binding.cpp                                                   */
/*************************************************************************/

#include "core/project_settings.h"
#include "sqlite_binding.h"

#include "core/class_db.h"
#include "core/error_macros.h"
#include "core/os/os.h"

// ---------------------------------------------------------------------------
// SQLiteQuery
// ---------------------------------------------------------------------------

SQLiteQuery::~SQLiteQuery() {
	finalize();
}

void SQLiteQuery::_bind_methods() {
	ClassDB::bind_method(D_METHOD("bind_int", "idx", "value"), &SQLiteQuery::bind_int);
	ClassDB::bind_method(D_METHOD("bind_double", "idx", "value"), &SQLiteQuery::bind_double);
	ClassDB::bind_method(D_METHOD("bind_text", "idx", "value"), &SQLiteQuery::bind_text);
	ClassDB::bind_method(D_METHOD("bind_null", "idx"), &SQLiteQuery::bind_null);

	ClassDB::bind_method(D_METHOD("step"), &SQLiteQuery::step);
	ClassDB::bind_method(D_METHOD("reset"), &SQLiteQuery::reset);

	ClassDB::bind_method(D_METHOD("column_count"), &SQLiteQuery::column_count);
	ClassDB::bind_method(D_METHOD("column_name", "idx"), &SQLiteQuery::column_name);
	ClassDB::bind_method(D_METHOD("column_type", "idx"), &SQLiteQuery::column_type);
	ClassDB::bind_method(D_METHOD("column_is_null", "idx"), &SQLiteQuery::column_is_null);
	ClassDB::bind_method(D_METHOD("column_int", "idx"), &SQLiteQuery::column_int);
	ClassDB::bind_method(D_METHOD("column_double", "idx"), &SQLiteQuery::column_double);
	ClassDB::bind_method(D_METHOD("column_text", "idx"), &SQLiteQuery::column_text);

	ClassDB::bind_method(D_METHOD("finalize"), &SQLiteQuery::finalize);

	BIND_CONSTANT(SQLITE_OK);
	BIND_CONSTANT(SQLITE_ROW);
	BIND_CONSTANT(SQLITE_DONE);
	BIND_CONSTANT(SQLITE_INTEGER);
	BIND_CONSTANT(SQLITE_FLOAT);
	BIND_CONSTANT(SQLITE_TEXT);
	BIND_CONSTANT(SQLITE_BLOB);
	BIND_CONSTANT(SQLITE_NULL);
}

void SQLiteQuery::bind_int(int p_idx, int p_value) {
	if (stmt != nullptr) {
		sqlite3_bind_int(stmt, p_idx, p_value);
	}
}

void SQLiteQuery::bind_double(int p_idx, double p_value) {
	if (stmt != nullptr) {
		sqlite3_bind_double(stmt, p_idx, p_value);
	}
}

void SQLiteQuery::bind_text(int p_idx, const String &p_value) {
	if (stmt != nullptr) {
		CharString utf8 = p_value.utf8();
		// SQLITE_TRANSIENT: SQLite copia; el CharString es temporal.
		sqlite3_bind_text(stmt, p_idx, utf8.get_data(), utf8.length(), SQLITE_TRANSIENT);
	}
}

void SQLiteQuery::bind_null(int p_idx) {
	if (stmt != nullptr) {
		sqlite3_bind_null(stmt, p_idx);
	}
}

int SQLiteQuery::step() {
	if (stmt == nullptr) {
		return SQLITE_MISUSE;
	}
	int rc = sqlite3_step(stmt);
	if (rc != SQLITE_ROW) {
		finished = true;
	}
	return rc;
}

int SQLiteQuery::reset() {
	if (stmt == nullptr) {
		return SQLITE_MISUSE;
	}
	finished = false;
	return sqlite3_reset(stmt);
}

int SQLiteQuery::column_count() const {
	return stmt != nullptr ? sqlite3_column_count(stmt) : 0;
}

String SQLiteQuery::column_name(int p_idx) const {
	if (stmt == nullptr) {
		return String();
	}
	const char *name = sqlite3_column_name(stmt, p_idx);
	return name != nullptr ? String::utf8(name) : String();
}

int SQLiteQuery::column_type(int p_idx) const {
	return stmt != nullptr ? sqlite3_column_type(stmt, p_idx) : SQLITE_NULL;
}

bool SQLiteQuery::column_is_null(int p_idx) const {
	return column_type(p_idx) == SQLITE_NULL;
}

int SQLiteQuery::column_int(int p_idx) const {
	return stmt != nullptr ? sqlite3_column_int(stmt, p_idx) : 0;
}

double SQLiteQuery::column_double(int p_idx) const {
	return stmt != nullptr ? sqlite3_column_double(stmt, p_idx) : 0.0;
}

String SQLiteQuery::column_text(int p_idx) const {
	if (stmt == nullptr) {
		return String();
	}
	const unsigned char *text = sqlite3_column_text(stmt, p_idx);
	if (text == nullptr) {
		return String();
	}
	int bytes = sqlite3_column_bytes(stmt, p_idx);
	return String::utf8((const char *)text, bytes);
}

void SQLiteQuery::finalize() {
	if (stmt != nullptr) {
		sqlite3_finalize(stmt);
		stmt = nullptr;
	}
}

// ---------------------------------------------------------------------------
// SQLiteBinding
// ---------------------------------------------------------------------------

SQLiteBinding::~SQLiteBinding() {
	close();
}

void SQLiteBinding::_bind_methods() {
	ClassDB::bind_method(D_METHOD("open", "path"), &SQLiteBinding::open);
	ClassDB::bind_method(D_METHOD("close"), &SQLiteBinding::close);
	ClassDB::bind_method(D_METHOD("is_open"), &SQLiteBinding::is_open);
	ClassDB::bind_method(D_METHOD("exec", "sql"), &SQLiteBinding::exec);
	ClassDB::bind_method(D_METHOD("last_insert_rowid"), &SQLiteBinding::last_insert_rowid);
	ClassDB::bind_method(D_METHOD("changes"), &SQLiteBinding::changes);
	ClassDB::bind_method(D_METHOD("last_error"), &SQLiteBinding::last_error);
	ClassDB::bind_method(D_METHOD("prepare", "sql"), &SQLiteBinding::prepare);
	ClassDB::bind_method(D_METHOD("query", "sql", "params"), &SQLiteBinding::query, DEFVAL(Array()));

	BIND_CONSTANT(SQLITE_OK);
}

int SQLiteBinding::open(const String &p_path) {
	close();
	// user:// / res:// -> ruta real; SQLite no entiende los esquemas de Godot
	// (sin esto el historial nunca se creaba: SQLITE_CANTOPEN).
	CharString utf8 = ProjectSettings::get_singleton()->globalize_path(p_path).utf8();
	int rc = sqlite3_open_v2(utf8.get_data(), &db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
	if (rc != SQLITE_OK) {
		if (db != nullptr) {
			sqlite3_close_v2(db);
			db = nullptr;
		}
		return rc;
	}
	// WAL mejora lectura/escritura concurrente; foreign_keys por claridad.
	sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
	sqlite3_exec(db, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
	sqlite3_busy_timeout(db, 5000);
	return SQLITE_OK;
}

void SQLiteBinding::close() {
	if (db != nullptr) {
		sqlite3_close_v2(db);
		db = nullptr;
	}
}

bool SQLiteBinding::is_open() const {
	return db != nullptr;
}

int SQLiteBinding::exec(const String &p_sql) {
	if (db == nullptr) {
		return SQLITE_MISUSE;
	}
	CharString utf8 = p_sql.utf8();
	return sqlite3_exec(db, utf8.get_data(), nullptr, nullptr, nullptr);
}

int SQLiteBinding::last_insert_rowid() const {
	return db != nullptr ? (int)sqlite3_last_insert_rowid(db) : 0;
}

int SQLiteBinding::changes() const {
	return db != nullptr ? sqlite3_changes(db) : 0;
}

String SQLiteBinding::last_error() const {
	if (db == nullptr) {
		return "database not open";
	}
	const char *msg = sqlite3_errmsg(db);
	return msg != nullptr ? String::utf8(msg) : String();
}

Ref<SQLiteQuery> SQLiteBinding::prepare(const String &p_sql) {
	if (db == nullptr) {
		return Ref<SQLiteQuery>();
	}
	sqlite3_stmt *stmt = nullptr;
	CharString utf8 = p_sql.utf8();
	int rc = sqlite3_prepare_v2(db, utf8.get_data(), utf8.length(), &stmt, nullptr);
	if (rc != SQLITE_OK || stmt == nullptr) {
		return Ref<SQLiteQuery>();
	}
	return Ref<SQLiteQuery>(memnew(SQLiteQuery(stmt)));
}

Array SQLiteBinding::query(const String &p_sql, const Array &p_params) {
	Array rows;
	Ref<SQLiteQuery> q = prepare(p_sql);
	if (q.is_null()) {
		return rows;
	}
	for (int i = 0; i < p_params.size(); i++) {
		Variant v = p_params[i];
		switch (v.get_type()) {
			case Variant::INT:
				q->bind_int(i + 1, v);
				break;
			case Variant::REAL:
				q->bind_double(i + 1, v);
				break;
			case Variant::NIL:
				q->bind_null(i + 1);
				break;
			default:
				q->bind_text(i + 1, v);
				break;
		}
	}
	int cols = q->column_count();
	while (q->step() == SQLITE_ROW) {
		Dictionary row;
		for (int c = 0; c < cols; c++) {
			String name = q->column_name(c);
			switch (q->column_type(c)) {
				case SQLITE_INTEGER:
					row[name] = q->column_int(c);
					break;
				case SQLITE_FLOAT:
					row[name] = q->column_double(c);
					break;
				case SQLITE_NULL:
					row[name] = Variant();
					break;
				default:
					row[name] = q->column_text(c);
					break;
			}
		}
		rows.push_back(row);
	}
	q->finalize();
	return rows;
}
