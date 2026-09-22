/**************************************************************************/
/*  box3d_compound.cpp                                                    */
/**************************************************************************/

#include "box3d_compound.h"

#include "box3d_types.h"
#include "core/local_vector.h"
#include "core/class_db.h"
#include "servers/physics_server.h"

#include <string.h>

/* Box3DCompound */

Box3DCompound::Box3DCompound() {
}

Box3DCompound::~Box3DCompound() {
	_free_owned();
}

void Box3DCompound::_free_owned() {
	for (int i = 0; i < owned_meshes.size(); i++) {
		b3DestroyMesh(owned_meshes[i]);
	}
	owned_meshes.clear();
	for (int i = 0; i < owned_hulls.size(); i++) {
		b3DestroyHull(owned_hulls[i]);
	}
	owned_hulls.clear();
	mesh_defs.clear();
	hull_defs.clear();
	sphere_defs.clear();
	capsule_defs.clear();
	mesh_materials.clear();
}

void Box3DCompound::clear() {
	_free_owned();
}

int Box3DCompound::get_child_count() const {
	return mesh_defs.size() + hull_defs.size() + sphere_defs.size() + capsule_defs.size();
}

void Box3DCompound::add_mesh(const PoolVector<Vector3> &p_faces, const Transform &p_xform, const Vector3 &p_scale) {
	int triangle_count = p_faces.size() / 3;
	ERR_FAIL_COND_MSG(p_faces.size() % 3 != 0 || triangle_count < 1,
			"Box3DCompound::add_mesh necesita un multiplo de 3 vertices (sopa de triangulos) y al menos un triangulo.");

	PoolVector<Vector3>::Read r = p_faces.read();
	int vertex_count = p_faces.size();
	LocalVector<b3Vec3> verts;
	verts.resize(vertex_count);
	for (int i = 0; i < vertex_count; i++) {
		verts[i] = b3_vec(r[i]);
	}
	LocalVector<int32_t> indices;
	indices.resize(vertex_count);
	for (int i = 0; i < vertex_count; i++) {
		indices[i] = i;
	}
	LocalVector<uint8_t> material_indices;
	material_indices.resize(triangle_count);
	for (int i = 0; i < triangle_count; i++) {
		material_indices[i] = 0;
	}

	b3MeshDef mdef = { 0 };
	mdef.vertices = verts.ptr();
	mdef.indices = indices.ptr();
	mdef.materialIndices = material_indices.ptr();
	mdef.vertexCount = vertex_count;
	mdef.triangleCount = triangle_count;
	mdef.weldVertices = true;
	mdef.weldTolerance = B3_LINEAR_SLOP;
	mdef.identifyEdges = true;

	b3MeshData *mesh = b3CreateMesh(&mdef, nullptr, 0);
	ERR_FAIL_NULL_MSG(mesh, "Box3DCompound: no se pudo hornear el mesh (revisar vertices degenerados).");
	owned_meshes.push_back(mesh);

	// One material slot per mesh child; box3d allows up to
	// B3_MAX_COMPOUND_MESH_MATERIALS per child.
	Vector<b3SurfaceMaterial> mats;
	b3SurfaceMaterial mat;
	memset(&mat, 0, sizeof(mat));
	mat.friction = 0.6f;
	mat.restitution = 0.0f;
	mats.push_back(mat);
	mesh_materials.push_back(mats);

	b3CompoundMeshDef cdef;
	memset(&cdef, 0, sizeof(cdef));
	cdef.meshData = mesh;
	cdef.transform = b3_transform(p_xform);
	cdef.scale = b3_vec(p_scale);
	cdef.materialCount = 1;
	mesh_defs.push_back(cdef);
	// Point at the stored copy, not the local: b3CreateCompound clones the
	// material array at bake time, but the pointer must stay valid until then.
	mesh_defs.write[mesh_defs.size() - 1].materials = mesh_materials.write[mesh_materials.size() - 1].ptrw();
}

void Box3DCompound::add_hull(const PoolVector<Vector3> &p_points, const Transform &p_xform) {
	ERR_FAIL_COND_MSG(p_points.size() < 4, "Box3DCompound::add_hull necesita al menos 4 puntos.");

	PoolVector<Vector3>::Read r = p_points.read();
	LocalVector<b3Vec3> pts;
	pts.resize(p_points.size());
	for (int i = 0; i < p_points.size(); i++) {
		pts[i] = b3_vec(r[i]);
	}
	b3HullData *hull = b3CreateHull(pts.ptr(), p_points.size(), p_points.size());
	ERR_FAIL_NULL_MSG(hull, "Box3DCompound: no se pudo hornear el hull (excede B3_MAX_HULL_VERTICES/FACES/EDGES).");
	owned_hulls.push_back(hull);

	b3CompoundHullDef cdef;
	memset(&cdef, 0, sizeof(cdef));
	cdef.hull = hull;
	cdef.transform = b3_transform(p_xform);
	cdef.material.friction = 0.6f;
	hull_defs.push_back(cdef);
}

void Box3DCompound::add_box(const Vector3 &p_half_extents, const Transform &p_xform) {
	// A box is baked as an 8-point hull: b3MakeBoxHull returns a struct with
	// internal pointers that must not outlive the call, and the compound only
	// takes a hull pointer, so a hull keeps the ownership simple.
	PoolVector<Vector3> pts;
	pts.resize(8);
	for (int i = 0; i < 8; i++) {
		pts.set(i, Vector3(
						   (i & 1) ? p_half_extents.x : -p_half_extents.x,
						   (i & 2) ? p_half_extents.y : -p_half_extents.y,
						   (i & 4) ? p_half_extents.z : -p_half_extents.z));
	}
	add_hull(pts, p_xform);
}

void Box3DCompound::add_sphere(real_t p_radius, const Transform &p_xform) {
	b3CompoundSphereDef cdef;
	memset(&cdef, 0, sizeof(cdef));
	cdef.sphere.radius = MAX((float)p_radius, B3_LINEAR_SLOP);
	cdef.sphere.center = b3_vec(p_xform.origin);
	cdef.material.friction = 0.6f;
	sphere_defs.push_back(cdef);
}

void Box3DCompound::add_capsule(real_t p_radius, real_t p_height, const Transform &p_xform) {
	// Same convention as the module's CapsuleShape: Z-aligned, height is the
	// mid-section and the caps extend past it by the radius.
	float radius = MAX((float)p_radius, B3_LINEAR_SLOP);
	float height = (float)p_height;
	b3CompoundCapsuleDef cdef;
	memset(&cdef, 0, sizeof(cdef));
	cdef.capsule.center1 = b3_vec(p_xform.xform(Vector3(0, 0, -height * 0.5)));
	cdef.capsule.center2 = b3_vec(p_xform.xform(Vector3(0, 0, height * 0.5)));
	cdef.capsule.radius = radius;
	cdef.material.friction = 0.6f;
	capsule_defs.push_back(cdef);
}

PoolByteArray Box3DCompound::bake() {
	ERR_FAIL_COND_V_MSG(get_child_count() < 1, PoolByteArray(), "Box3DCompound::bake sin hijos.");

	b3CompoundDef def;
	memset(&def, 0, sizeof(def));
	// cast away const: the vectors are only read by b3CreateCompound
	Vector<b3CompoundMeshDef> &meshes = mesh_defs;
	Vector<b3CompoundHullDef> &hulls = hull_defs;
	Vector<b3CompoundSphereDef> &spheres = sphere_defs;
	Vector<b3CompoundCapsuleDef> &capsules = capsule_defs;
	if (meshes.size()) {
		def.meshes = meshes.ptrw();
		def.meshCount = meshes.size();
	}
	if (hulls.size()) {
		def.hulls = hulls.ptrw();
		def.hullCount = hulls.size();
	}
	if (spheres.size()) {
		def.spheres = spheres.ptrw();
		def.sphereCount = spheres.size();
	}
	if (capsules.size()) {
		def.capsules = capsules.ptrw();
		def.capsuleCount = capsules.size();
	}

	b3CompoundData *compound = b3CreateCompound(&def);
	ERR_FAIL_NULL_V_MSG(compound, PoolByteArray(), "Box3DCompound: b3CreateCompound fallo.");

	int byte_count = compound->byteCount;
	uint8_t *raw = b3ConvertCompoundToBytes(compound);
	PoolByteArray out;
	out.resize(byte_count);
	{
		PoolByteArray::Write w = out.write();
		memcpy(w.ptr(), raw, byte_count);
	}
	// The buffer belongs to the caller now; the allocation made by
	// b3CreateCompound is still ours to free (byteCount is untouched).
	b3DestroyCompound(compound);
	return out;
}

bool Box3DCompound::is_valid_compound(const PoolByteArray &p_bytes) {
	if (p_bytes.size() < (int)sizeof(b3CompoundData)) {
		return false;
	}
	PoolVector<uint8_t> buf;
	buf.resize(p_bytes.size());
	{
		PoolByteArray::Read r = p_bytes.read();
		PoolVector<uint8_t>::Write w = buf.write();
		memcpy(w.ptr(), r.ptr(), p_bytes.size());
	}
	PoolVector<uint8_t>::Write w2 = buf.write();
	b3CompoundData *c = b3ConvertBytesToCompound((uint8_t *)w2.ptr(), buf.size());
	return c != nullptr;
}

int Box3DCompound::get_bake_size() {
	return bake().size();
}

void Box3DCompound::_bind_methods() {
	ClassDB::bind_method(D_METHOD("add_mesh", "faces", "xform", "scale"), &Box3DCompound::add_mesh, DEFVAL(Transform()), DEFVAL(Vector3(1, 1, 1)));
	ClassDB::bind_method(D_METHOD("add_hull", "points", "xform"), &Box3DCompound::add_hull, DEFVAL(Transform()));
	ClassDB::bind_method(D_METHOD("add_box", "half_extents", "xform"), &Box3DCompound::add_box, DEFVAL(Transform()));
	ClassDB::bind_method(D_METHOD("add_sphere", "radius", "xform"), &Box3DCompound::add_sphere, DEFVAL(Transform()));
	ClassDB::bind_method(D_METHOD("add_capsule", "radius", "height", "xform"), &Box3DCompound::add_capsule, DEFVAL(Transform()));
	ClassDB::bind_method(D_METHOD("clear"), &Box3DCompound::clear);
	ClassDB::bind_method(D_METHOD("get_child_count"), &Box3DCompound::get_child_count);
	ClassDB::bind_method(D_METHOD("bake"), &Box3DCompound::bake);
	ClassDB::bind_method(D_METHOD("get_bake_size"), &Box3DCompound::get_bake_size);
	ClassDB::bind_method(D_METHOD("is_valid_compound", "bytes"), &Box3DCompound::is_valid_compound);
}

/* Box3DCompoundShape */

Box3DCompoundShape::Box3DCompoundShape() :
		Shape(PhysicsServer::get_singleton()->shape_create(PhysicsServer::SHAPE_CUSTOM)) {
}

void Box3DCompoundShape::set_compound_bytes(const PoolByteArray &p_bytes) {
	compound_bytes = p_bytes;
	PhysicsServer::get_singleton()->shape_set_data(get_shape(), p_bytes);
	notify_change_to_owners();
}

void Box3DCompoundShape::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_compound_bytes", "bytes"), &Box3DCompoundShape::set_compound_bytes);
	ClassDB::bind_method(D_METHOD("get_compound_bytes"), &Box3DCompoundShape::get_compound_bytes);
	ADD_PROPERTY(PropertyInfo(Variant::POOL_BYTE_ARRAY, "compound_bytes", PROPERTY_HINT_NONE, "", PROPERTY_USAGE_NOEDITOR | PROPERTY_USAGE_INTERNAL), "set_compound_bytes", "get_compound_bytes");
}
