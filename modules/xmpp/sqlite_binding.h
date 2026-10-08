#ifndef SQLITE_BINDING_H
#define SQLITE_BINDING_H

/*************************************************************************/
/*  sqlite_binding.h                                                     */
/*************************************************************************/
/* Binding mínimo de SQLite para xat (historial por bare JID).            */
/*                                                                       */
/*  Política de hilos: SQLite puede usarse desde cualquier hilo (modo    */
/*  serialized), pero xat NO lo toca desde el hilo de libstrophe: el      */
/*  historial se maneja en el hilo principal / de UI.                     */
/*                                                                       */
/*  Uso típico desde GDScript:                                            */
/*    var db = SQLiteBinding.new()  # o ClassDB.instance("SQLiteBinding") */
/*    db.open("user://history/agente@server.db")                          */
/*    db.exec("CREATE TABLE IF NOT EXISTS msgs(...)")                     */
/*    var q = db.prepare("INSERT INTO msgs(mid,body) VALUES(?,?)")        */
/*    q.bind_text(1, "id"); q.bind_text(2, "hola"); q.step(); q.finalize()*/
/*************************************************************************/

#include "core/array.h"
#include "core/dictionary.h"
#include "core/reference.h"
#include "core/ustring.h"

#include <sqlite3.h>

class SQLiteBinding;

class SQLiteQuery : public Reference {
	GDCLASS(SQLiteQuery, Reference);

	sqlite3_stmt *stmt = nullptr;
	bool finished = false;

protected:
	static void _bind_methods();

public:
	SQLiteQuery() {}
	SQLiteQuery(sqlite3_stmt *p_stmt) :
			stmt(p_stmt) {}
	~SQLiteQuery();

	void bind_int(int p_idx, int p_value);
	void bind_double(int p_idx, double p_value);
	void bind_text(int p_idx, const String &p_value);
	void bind_null(int p_idx);

	int step();
	int reset();

	int column_count() const;
	String column_name(int p_idx) const;
	int column_type(int p_idx) const;
	bool column_is_null(int p_idx) const;
	int column_int(int p_idx) const;
	double column_double(int p_idx) const;
	String column_text(int p_idx) const;

	void finalize();

	_FORCE_INLINE_ sqlite3_stmt *get_stmt() const { return stmt; }
};

class SQLiteBinding : public Reference {
	GDCLASS(SQLiteBinding, Reference);

	sqlite3 *db = nullptr;

protected:
	static void _bind_methods();

public:
	SQLiteBinding() {}
	~SQLiteBinding();

	int open(const String &p_path);
	void close();
	bool is_open() const;

	int exec(const String &p_sql);
	int last_insert_rowid() const;
	int changes() const;
	String last_error() const;

	Ref<SQLiteQuery> prepare(const String &p_sql);
	// Conveniencia: corre un SELECT y devuelve filas como Dictionaries
	// (clave = nombre de columna). `p_params` se bindea posicional (1..n).
	Array query(const String &p_sql, const Array &p_params = Array());
};

#endif // SQLITE_BINDING_H
