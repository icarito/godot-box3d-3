/**************************************************************************/
/*  box3d_compound.h                                                      */
/*  Box3D compound shapes: bake/serialize helper + a Godot Shape resource  */
/*  for streaming static level tiles.                                      */
/*                                                                         */
/*  Compounds are static-body only and immutable after creation. The       */
/*  intended workflow (see box3d docs/compound.md):                        */
/*    1. author the tile geometry (meshes / hulls / primitives)            */
/*    2. bake it once with Box3DCompound and serialize the bytes           */
/*    3. store the bytes on disk or in a streaming cache                   */
/*    4. at runtime put those bytes on a Box3DCompoundShape and attach it  */
/*       to a static body (the engine uses the buffer without copying)     */
/**************************************************************************/

#ifndef BOX3D_COMPOUND_H
#define BOX3D_COMPOUND_H

#include "core/reference.h"
#include "scene/resources/shape.h"

#include <box3d/box3d.h>

// Bake + serialize helper. Owns the temporary mesh/hull data created while
// filling the definition; everything is cloned by b3CreateCompound at bake
// time, so the temporaries are freed right after.
class Box3DCompound : public Reference {
	GDCLASS(Box3DCompound, Reference);

	// Child meshes/hulls created by the add_* calls. The definition arrays are
	// built in bake() so the pointers handed to Box3D stay valid for the call.
	Vector<b3MeshData *> owned_meshes;
	Vector<b3HullData *> owned_hulls;
	Vector<b3CompoundMeshDef> mesh_defs;
	Vector<b3CompoundHullDef> hull_defs;
	Vector<b3CompoundSphereDef> sphere_defs;
	Vector<b3CompoundCapsuleDef> capsule_defs;
	Vector<Vector<b3SurfaceMaterial> > mesh_materials;

	void _free_owned();

protected:
	static void _bind_methods();

public:
	void add_mesh(const PoolVector<Vector3> &p_faces, const Transform &p_xform = Transform(), const Vector3 &p_scale = Vector3(1, 1, 1));
	void add_hull(const PoolVector<Vector3> &p_points, const Transform &p_xform = Transform());
	void add_box(const Vector3 &p_half_extents, const Transform &p_xform = Transform());
	void add_sphere(real_t p_radius, const Transform &p_xform = Transform());
	void add_capsule(real_t p_radius, real_t p_height, const Transform &p_xform = Transform());
	void clear();
	int get_child_count() const;

	// Bakes the definition and returns the serialized compound. Empty on error.
	PoolByteArray bake();

	// Header/offset validation without keeping the buffer.
	bool is_valid_compound(const PoolByteArray &p_bytes);

	// Byte size a baked compound with the current children would have.
	int get_bake_size();

	Box3DCompound();
	~Box3DCompound();
};

// Godot Shape resource backed by a serialized compound. Static-body only:
// the Box3D backend ignores it (with one error) on any other body type.
class Box3DCompoundShape : public Shape {
	GDCLASS(Box3DCompoundShape, Shape);

	PoolByteArray compound_bytes;

protected:
	static void _bind_methods();

public:
	void set_compound_bytes(const PoolByteArray &p_bytes);
	PoolByteArray get_compound_bytes() const { return compound_bytes; }

	virtual Vector<Vector3> get_debug_mesh_lines() { return Vector<Vector3>(); }
	virtual real_t get_enclosing_radius() const { return 0; }

	Box3DCompoundShape();
};

#endif // BOX3D_COMPOUND_H
