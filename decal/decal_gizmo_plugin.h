/**************************************************************************/
/*  decal_gizmo_plugin.h                                                  */
/**************************************************************************/

#ifndef DECAL_GIZMO_PLUGIN_H
#define DECAL_GIZMO_PLUGIN_H

#ifdef TOOLS_ENABLED

#include "editor/spatial_editor_gizmos.h"

class DecalGizmoPlugin : public EditorSpatialGizmoPlugin {
	GDCLASS(DecalGizmoPlugin, EditorSpatialGizmoPlugin);

public:
	bool has_gizmo(Spatial *p_spatial);
	String get_name() const;
	int get_priority() const;

	void redraw(EditorSpatialGizmo *p_gizmo);

	DecalGizmoPlugin();
};

#endif // TOOLS_ENABLED

#endif // DECAL_GIZMO_PLUGIN_H
