#include "decal_gizmo_plugin.h"

#ifdef TOOLS_ENABLED

#include "decal.h"

DecalGizmoPlugin::DecalGizmoPlugin() {
	create_material("decal_box", Color(0.9, 0.6, 0.2, 0.6), false, false, false);
}

bool DecalGizmoPlugin::has_gizmo(Spatial *p_spatial) {
	return Object::cast_to<Decal>(p_spatial) != nullptr;
}

String DecalGizmoPlugin::get_name() const {
	return "Decal";
}

int DecalGizmoPlugin::get_priority() const {
	return -1;
}

void DecalGizmoPlugin::redraw(EditorSpatialGizmo *p_gizmo) {
	Decal *decal = Object::cast_to<Decal>(p_gizmo->get_spatial_node());
	ERR_FAIL_COND(!decal);

	p_gizmo->clear();

	Ref<Material> material = get_material("decal_box", p_gizmo);

	Vector3 size = decal->get_size();

	Vector<Vector3> lines;

	// Box wireframe: the decal projects along local -Y (Godot 4 semantics).
	Vector3 h = size / 2.0;
	static const int corner_count = 8;
	Vector3 corners[corner_count] = {
		Vector3(-h.x, h.y, -h.z),
		Vector3(h.x, h.y, -h.z),
		Vector3(h.x, h.y, h.z),
		Vector3(-h.x, h.y, h.z),
		Vector3(-h.x, -h.y, -h.z),
		Vector3(h.x, -h.y, -h.z),
		Vector3(h.x, -h.y, h.z),
		Vector3(-h.x, -h.y, h.z)
	};

	static const int edge_pairs[12][2] = {
		{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
		{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
		{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
	};

	for (int i = 0; i < 12; i++) {
		lines.push_back(corners[edge_pairs[i][0]]);
		lines.push_back(corners[edge_pairs[i][1]]);
	}

	p_gizmo->add_lines(lines, material, false);

	// Projection arrow along -Y.
	Vector3 arrow_top = Vector3(0, 0, 0);
	Vector3 arrow_bottom = Vector3(0, -h.y, 0);
	Vector3 side1 = Vector3(-h.x * 0.3, -h.y - 0.25, 0);
	Vector3 side2 = Vector3(h.x * 0.3, -h.y - 0.25, 0);
	lines.push_back(arrow_top);
	lines.push_back(Vector3(0, -h.y - 0.25, 0));
	lines.push_back(Vector3(0, -h.y - 0.25, 0));
	lines.push_back(side1);
	lines.push_back(Vector3(0, -h.y - 0.25, 0));
	lines.push_back(side2);
	lines.push_back(side1);
	lines.push_back(arrow_bottom + Vector3(0, 0, 0));
	lines.push_back(side2);
	lines.push_back(arrow_bottom);

	p_gizmo->add_lines(lines, material, false);
}

#endif // TOOLS_ENABLED
