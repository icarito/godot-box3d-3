#include "register_types.h"

#include "decal.h"

#ifdef TOOLS_ENABLED
#include "decal_editor_plugin.h"
#include "editor/editor_plugin.h"
#endif

void register_decal_types() {
	ClassDB::register_class<Decal>();

#ifdef TOOLS_ENABLED
	EditorPlugins::add_by_type<DecalEditorPlugin>();
#endif
}

void unregister_decal_types() {
}
