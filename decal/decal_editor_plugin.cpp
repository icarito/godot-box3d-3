#include "decal_editor_plugin.h"

#ifdef TOOLS_ENABLED

DecalEditorPlugin::DecalEditorPlugin(EditorNode *p_editor) {
	decal_gizmo_plugin.instance();
	add_spatial_gizmo_plugin(decal_gizmo_plugin);
}

#endif // TOOLS_ENABLED
