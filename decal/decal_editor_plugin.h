/**************************************************************************/
/*  decal_editor_plugin.h                                                 */
/**************************************************************************/

#ifndef DECAL_EDITOR_PLUGIN_H
#define DECAL_EDITOR_PLUGIN_H

#ifdef TOOLS_ENABLED

#include "decal_gizmo_plugin.h"
#include "editor/editor_node.h"
#include "editor/spatial_editor_gizmos.h"

class DecalEditorPlugin : public EditorPlugin {
	GDCLASS(DecalEditorPlugin, EditorPlugin);

	Ref<DecalGizmoPlugin> decal_gizmo_plugin;

public:
	DecalEditorPlugin(EditorNode *p_editor);
};

#endif // TOOLS_ENABLED

#endif // DECAL_EDITOR_PLUGIN_H
