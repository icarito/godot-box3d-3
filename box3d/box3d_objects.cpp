/**************************************************************************/
/*  box3d_objects.cpp                                                     */
/**************************************************************************/

#include "box3d_objects.h"

#include "core/object.h"
#include "core/project_settings.h"

static b3BodyType b3_body_type(PhysicsServer::BodyMode p_mode) {
	switch (p_mode) {
		case PhysicsServer::BODY_MODE_STATIC:
			return b3_staticBody;
		case PhysicsServer::BODY_MODE_KINEMATIC:
			return b3_kinematicBody;
		default:
			// CHARACTER is RIGID with its rotation pinned, see apply_motion_locks().
			return b3_dynamicBody;
	}
}

static b3Matrix3 b3_scale_matrix(const b3Matrix3 &p_m, float p_s) {
	b3Matrix3 m = p_m;
	m.cx.x *= p_s;
	m.cx.y *= p_s;
	m.cx.z *= p_s;
	m.cy.x *= p_s;
	m.cy.y *= p_s;
	m.cy.z *= p_s;
	m.cz.x *= p_s;
	m.cz.y *= p_s;
	m.cz.z *= p_s;
	return m;
}

static Transform b3_shape_transform(const Transform &p_body, const Transform &p_shape) {
	return Transform(Basis().scaled(p_body.basis.get_scale()), Vector3()) * p_shape;
}

/* Box3DShape */

/* Box3DBody */

Box3DBody::~Box3DBody() {
	_destroy_in_world();
	for (int i = 0; i < shapes.size(); i++) {
		free_shape_geometry(i);
	}
}

void Box3DBody::set_space(Box3DSpace *p_space) {
	if (space == p_space) {
		return;
	}
	if (space) {
		// Exception filter joints die with the body in the world; forget them
		// on both sides so they are rebuilt cleanly if we come back.
		for (Map<RID, b3JointId>::Element *E = exception_joints.front(); E; E = E->next()) {
			Box3DBody *other = space->owner_body(E->key());
			if (other) {
				other->exception_joints.erase(self);
			}
		}
		exception_joints.clear();
		_destroy_in_world();
		space->bodies.erase(this);
	}
	space = p_space;
	if (space) {
		space->bodies.push_back(this);
		_create_in_world();
		apply_exceptions();
	}
	for (Set<Box3DJoint *>::Element *E = joints.front(); E; E = E->next()) {
		E->get()->rebuild();
	}
}

void Box3DBody::_create_in_world() {
	ERR_FAIL_COND(!space || B3_IS_NULL(space->world));

	b3BodyDef def = b3DefaultBodyDef();
	def.type = b3_body_type(mode);
	def.position = b3_pos(transform.origin);
	def.rotation = b3_quat(transform.basis);
	def.linearVelocity = b3_vec(linear_velocity);
	def.angularVelocity = b3_vec(angular_velocity);
	def.gravityScale = gravity_from_area ? 0.0 : gravity_scale;
	def.enableSleep = can_sleep;
	def.isAwake = !sleeping;
	def.isBullet = ccd;
	def.userData = this;

	id = b3CreateBody(space->world, &def);
	ERR_FAIL_COND(B3_IS_NULL(id));

	for (int i = 0; i < shapes.size(); i++) {
		_create_shape(i);
	}

	apply_mass();
	apply_damping();
	apply_motion_locks();
	was_awake = true;
}

void Box3DBody::_destroy_in_world() {
	if (!in_world()) {
		return;
	}
	// Cache the last simulated state so re-entering a space resumes where we left off.
	transform = get_transform();
	linear_velocity = get_linear_velocity();
	angular_velocity = get_angular_velocity();

	b3DestroyBody(id); // Destroys the body's shapes and joints too.
	id = b3_nullBodyId;
	for (int i = 0; i < shapes.size(); i++) {
		shapes.write[i].id = b3_nullShapeId;
	}
}

// Godot decides body collision with an OR: two bodies interact when either
// one's mask sees the other's layer, and a body's own mask says what it detects,
// not what may detect it. Box3D's filter is an AND -- both directions must agree
// -- so a prop carrying its own layer fell straight through a floor whose mask
// only lists layer 1, which is how nearly every level is set up. Queries and
// areas already sidestep the mismatch through their reserved bits; body pairs
// could not, because both sides are ordinary layers.
//
// So body shapes accept every layer in the broadphase and the real rule is
// applied here. Box3D calls this only for pairs where one shape asked for it
// and only for awake dynamic bodies, which is exactly the set that matters.
static bool godot_collision_filter(b3ShapeId p_a, b3ShapeId p_b, void *) {
	if (b3Shape_IsSensor(p_a) || b3Shape_IsSensor(p_b)) {
		return true; // areas are filtered correctly by BOX3D_SENSOR_BIT already
	}
	Box3DEntity *entity_a = (Box3DEntity *)b3Body_GetUserData(b3Shape_GetBody(p_a));
	Box3DEntity *entity_b = (Box3DEntity *)b3Body_GetUserData(b3Shape_GetBody(p_b));
	if (!entity_a || !entity_b || entity_a->is_area || entity_b->is_area) {
		return true;
	}
	const Box3DBody *a = (const Box3DBody *)entity_a;
	const Box3DBody *b = (const Box3DBody *)entity_b;
	return (a->collision_layer & b->collision_mask) != 0 || (b->collision_layer & a->collision_mask) != 0;
}

static bool b3_fill_shape_def(b3ShapeDef &r_def, Box3DBody *p_body, int p_idx, bool p_sensor) {
	r_def = b3DefaultShapeDef();
	r_def.baseMaterial.friction = p_body->friction;
	r_def.baseMaterial.restitution = p_body->bounce;
	r_def.filter.categoryBits = p_body->collision_layer;
	// Every layer, so body pairs always reach godot_collision_filter(). The
	// reserved bits keep filtering queries and sensors here, where the semantics
	// already match Godot.
	r_def.filter.maskBits = (uint64_t)0xFFFFFFFF | BOX3D_QUERY_BIT | BOX3D_SENSOR_BIT;
	r_def.enableCustomFiltering = true;
	r_def.enableSensorEvents = true;
	r_def.isSensor = p_sensor;
	r_def.updateBodyMass = false;
	r_def.userData = (void *)(intptr_t)p_idx;
	return true;
}

// Creates the b3 shape for a Godot shape definition under a body. Returns the
// shape id; geometry conversions that fail report and return null.
// A convex point set with no volume (a flat quad, a line) cannot become a hull.
// Find its plane normal from the largest spanning triangle and push the points
// out to both sides, which turns the plate into a thin prism Box3D can build.
// Returns false when the points are not merely flat but degenerate beyond use.
static bool b3_thicken_flat_points(const LocalVector<b3Vec3> &p_points, LocalVector<b3Vec3> &r_thickened) {
	const int count = (int)p_points.size();
	if (count < 3) {
		return false;
	}

	const Vector3 base = g_vec(p_points[0]);
	real_t best = 0.0;
	int far_idx = -1;
	for (int i = 1; i < count; i++) {
		real_t d = (g_vec(p_points[i]) - base).length_squared();
		if (d > best) {
			best = d;
			far_idx = i;
		}
	}
	if (far_idx < 0 || best <= CMP_EPSILON) {
		return false; // every point is the same point
	}

	const Vector3 edge = (g_vec(p_points[far_idx]) - base).normalized();
	best = 0.0;
	int off_idx = -1;
	for (int i = 1; i < count; i++) {
		Vector3 v = g_vec(p_points[i]) - base;
		real_t d = (v - edge * edge.dot(v)).length_squared();
		if (d > best) {
			best = d;
			off_idx = i;
		}
	}
	if (off_idx < 0 || best <= CMP_EPSILON) {
		return false; // collinear, there is no plane to thicken
	}

	const Vector3 normal = edge.cross(g_vec(p_points[off_idx]) - base).normalized();
	if (normal.length_squared() < CMP_EPSILON) {
		return false;
	}

	const Vector3 offset = normal * (real_t)(2.0f * B3_LINEAR_SLOP);
	r_thickened.resize(count * 2);
	for (int i = 0; i < count; i++) {
		Vector3 p = g_vec(p_points[i]);
		r_thickened[i] = b3_vec(p + offset);
		r_thickened[count + i] = b3_vec(p - offset);
	}
	return true;
}

static b3ShapeId b3_create_godot_shape(b3BodyId p_id, const b3ShapeDef &p_def, Box3DShape *p_shape,
		const Transform &p_xform) {
	// A CollisionShape's local basis routinely carries scale, and level geometry
	// exported from modelling tools is full of mirrored ones (negative
	// determinant). b3_transform() keeps only the rotation, so the scale has to
	// ride along separately. Godot decomposes a basis as rotation * scale, with
	// the scale going all-negative for a mirror, and Box3D applies its scale in
	// the same order: point, then scale, then rotate, then translate. It also
	// reverses hull winding for a reflected scale, so mirrors come out solid.
	const Vector3 basis_scale = p_xform.basis.get_scale();
	// Spheres and capsules take no scale argument, so theirs is baked into the
	// geometry. Only a uniform factor makes sense there; take the largest.
	const float uniform_scale = MAX(Math::abs(basis_scale.x), MAX(Math::abs(basis_scale.y), Math::abs(basis_scale.z)));

	switch (p_shape->type) {
		case PhysicsServer::SHAPE_BOX: {
			Vector3 he = p_shape->data;
			// Pre-scale the half extents (b3SafeScale would clamp sub-0.01
			// import scales), then let the transformed hull bake the rotation
			// and translation. Mirrored scales flip the winding on the way in;
			// the hull builder rebuilds outward winding from the point set.
			b3BoxHull hull = b3MakeBoxHull(MAX((float)he.x * (float)Math::abs(basis_scale.x), B3_LINEAR_SLOP), MAX((float)he.y * (float)Math::abs(basis_scale.y), B3_LINEAR_SLOP), MAX((float)he.z * (float)Math::abs(basis_scale.z), B3_LINEAR_SLOP));
			// Box3D bakes the local transform into a world-owned clone of the hull,
			// so the stack copy above does not need to outlive this call.
			return b3CreateTransformedHullShape(p_id, &p_def, &hull.base, b3_transform(Transform(Basis(p_xform.basis.get_rotation_quat()), p_xform.origin)), b3Vec3_one);
		}
		case PhysicsServer::SHAPE_SPHERE: {
			b3Sphere sphere = { b3_vec(p_xform.origin), (float)p_shape->data * uniform_scale };
			return b3CreateSphereShape(p_id, &p_def, &sphere);
		}
		case PhysicsServer::SHAPE_CAPSULE: {
			Dictionary d = p_shape->data;
			float radius = d.has("radius") ? (float)(real_t)d["radius"] : 0.5;
			float height = d.has("height") ? (float)(real_t)d["height"] : 1.0;
			// Godot 3's Bullet module maps CapsuleShape to btCapsuleShapeZ, so a
			// capsule is Z-aligned here (the shape's local Y is the capsule axis
			// in Godot 4, but every 3.x scene was authored against Bullet).
			// Godot's height is the mid-section; the caps extend past it by radius.
			// b3CreateCapsuleShape carries no transform, so the shape transform's
			// rotation is baked into the axis endpoints.
			// The basis already carries the scale, so transforming the endpoints
			// stretches the axis; only the radius needs the uniform factor.
			b3Capsule capsule = {
				b3_vec(p_xform.basis.xform(Vector3(0, 0, -height * 0.5)) + p_xform.origin),
				b3_vec(p_xform.basis.xform(Vector3(0, 0, height * 0.5)) + p_xform.origin),
				MAX(radius * uniform_scale, B3_LINEAR_SLOP)
			};
			return b3CreateCapsuleShape(p_id, &p_def, &capsule);
		}
		case PhysicsServer::SHAPE_CYLINDER: {
			Dictionary d = p_shape->data;
			float radius = d.has("radius") ? (float)(real_t)d["radius"] : 0.5;
			float height = d.has("height") ? (float)(real_t)d["height"] : 1.0;
			// Box3D builds its tessellated cylinder around +y from the offset;
			// shift down so the hull is centered like Godot's. The world clones
			// the hull, so the temporary does not need to outlive this call.
			// Pre-scale like the convex path: b3SafeScale clamps sub-0.01
			// import scales, so build the hull in the scaled frame directly.
			// Box3D's cylinder is Y-aligned and Godot's is too; a non-uniform
			// scale maps onto radius/height by the basis axes.
			b3HullData *hull = b3CreateCylinder(height * MAX(Math::abs(basis_scale.y), 0.001), MAX(radius * MAX(Math::abs(basis_scale.x), Math::abs(basis_scale.z)), B3_LINEAR_SLOP), -height * 0.5, 24);
			if (!hull) {
				return b3_nullShapeId;
			}
			Basis rot_basis = Basis(p_xform.basis.get_rotation_quat());
			b3ShapeId sid = b3CreateTransformedHullShape(p_id, &p_def, hull, b3_transform(Transform(rot_basis, p_xform.origin)), b3Vec3_one);
			b3DestroyHull(hull);
			return sid;
		}
		case PhysicsServer::SHAPE_CONVEX_POLYGON: {
			PoolVector3Array points = p_shape->data;
			int count = points.size();
			if (count < 4) {
				ERR_PRINT("Box3D: a convex polygon shape needs at least 4 points, ignoring it.");
				return b3_nullShapeId;
			}
			PoolVector3Array::Read r = points.read();
			LocalVector<b3Vec3> b3points;
			b3points.resize(count);
			for (int i = 0; i < count; i++) {
				// Pre-scale here: b3SafeScale clamps scale components to
				// B3_MIN_SCALE (0.01), which would inflate Collada-style
				// import scales of 1/320 back up to 1/100. Baking the scale
				// into the points keeps the real size, and passes b3SafeScale
				// a neutral one so it has nothing to clamp.
				b3points[i] = b3_vec(r[i] * basis_scale);
			}
			b3HullData *hull = b3CreateHull(b3points.ptr(), count, count);
			if (!hull) {
				// Godot accepts flat convex shapes and Bullet collides them as
				// zero-thickness plates. Box3D's hull builder needs a real
				// volume, so give a degenerate point set one and retry.
				LocalVector<b3Vec3> thickened;
				if (b3_thicken_flat_points(b3points, thickened)) {
					hull = b3CreateHull(thickened.ptr(), (int)thickened.size(), (int)thickened.size());
				}
			}
			if (!hull) {
				ERR_PRINT("Box3D: failed to build a convex hull from the given points, ignoring it.");
				return b3_nullShapeId;
			}
			// Points are pre-scaled; the transform carries rotation only.
			b3ShapeId sid = b3CreateTransformedHullShape(p_id, &p_def, hull, b3_transform(Transform(Basis(p_xform.basis.get_rotation_quat()), p_xform.origin)), b3Vec3_one);
			b3DestroyHull(hull); // Cloned into the world hull database.
			return sid;
		}
		default:
			return b3_nullShapeId;
	}
}

void Box3DBody::free_shape_geometry(int p_idx) {
	ShapeInstance &si = shapes.write[p_idx];
	if (si.mesh_data) {
		b3DestroyMesh(si.mesh_data);
		si.mesh_data = nullptr;
	}
	if (si.height_data) {
		b3DestroyHeightField(si.height_data);
		si.height_data = nullptr;
	}
}

void Box3DBody::_create_shape(int p_idx) {
	ShapeInstance &si = shapes.write[p_idx];
	si.id = b3_nullShapeId;
	if (!in_world() || si.disabled || !si.shape) {
		return;
	}

	b3ShapeDef def;
	b3_fill_shape_def(def, this, p_idx, false);
	const Transform shape_xform = b3_shape_transform(transform, si.xform);

	switch (si.shape->type) {
		case PhysicsServer::SHAPE_CONCAVE_POLYGON: {
			// Box3D mesh shapes only generate contacts on static bodies.
			if (b3Body_GetType(id) != b3_staticBody) {
				ERR_PRINT_ONCE("Box3D: concave polygon (trimesh) shapes only collide on static bodies, ignoring it.");
				return;
			}
			PoolVector3Array faces = si.shape->data;
			int triangles = faces.size() / 3;
			if (faces.size() == 0) {
				// A ConcavePolygonShape with no faces is legal and simply has no
				// collision; Godot's own backends stay quiet about it.
				return;
			}
			if (triangles < 1) {
				ERR_PRINT("Box3D: a concave polygon shape needs at least one triangle, ignoring it.");
				return;
			}
			PoolVector3Array::Read r = faces.read();
			int vertex_count = faces.size();
			LocalVector<b3Vec3> verts;
			verts.resize(vertex_count);
			// Every triangle is emitted twice, once with each winding, because
			// Box3D treats a mesh triangle as one-sided while Godot's own
			// backend collides a ConcavePolygonShape from both faces. Level
			// geometry authored for Godot -- CSG shapes and imported meshes
			// alike -- therefore carries whatever winding it happens to have.
			//
			// Sweeps no longer need this (sweep_meshes() casts triangle by
			// triangle, and b3ShapeCast has no notion of facing), but a falling
			// RigidBody is resolved by the solver, which does: a crate landing
			// on a back-facing floor fell straight through the level.
			//
			// ponytail: 2x triangles in the mesh BVH. The traversal cost is
			// still doubled; only the per-triangle work is skipped. Halving the
			// traversal too would mean a separate mirrored shape, which every
			// query path would then have to know to skip.
			LocalVector<int> indices;
			indices.resize(vertex_count * 2);
			for (int i = 0; i < vertex_count; i++) {
				verts[i] = b3_vec(r[i]);
			}
			// Interleaved rather than appended: the mirror of triangle t is at
			// index 2t+1, so a mesh query can skip the copies by testing one bit
			// without having to know how many triangles there are. Our own mesh
			// paths do exactly that -- they are facing-agnostic, so the copies
			// are pure duplicate work for them. Only the solver wants both.
			for (int t = 0; t < triangles; t++) {
				indices[t * 6 + 0] = t * 3 + 0;
				indices[t * 6 + 1] = t * 3 + 1;
				indices[t * 6 + 2] = t * 3 + 2;
				indices[t * 6 + 3] = t * 3 + 0;
				indices[t * 6 + 4] = t * 3 + 2;
				indices[t * 6 + 5] = t * 3 + 1;
			}
			// Bake the local transform into the vertices before building, so the
			// BVH is built once instead of built, thrown away and rebuilt.
			if (shape_xform != Transform()) {
				for (int i = 0; i < vertex_count; i++) {
					verts[i] = b3_vec(shape_xform.xform(g_vec(verts[i])));
				}
			}
			b3MeshDef mdef = { 0 };
			mdef.vertices = verts.ptr();
			mdef.indices = indices.ptr();
			mdef.vertexCount = vertex_count;
			mdef.triangleCount = triangles * 2;
			mdef.weldVertices = true;
			mdef.weldTolerance = B3_LINEAR_SLOP;
			mdef.identifyEdges = true;
			free_shape_geometry(p_idx);
			si.mesh_data = b3CreateMesh(&mdef, nullptr, 0);
			ERR_FAIL_NULL(si.mesh_data);
			si.id = b3CreateMeshShape(id, &def, si.mesh_data, b3Vec3_one);
			
		} break;
		case PhysicsServer::SHAPE_HEIGHTMAP: {
			if (b3Body_GetType(id) != b3_staticBody) {
				ERR_PRINT_ONCE("Box3D: height map shapes only collide on static bodies, ignoring it.");
				return;
			}
			Dictionary d = si.shape->data;
			int width = d.has("width") ? (int)(real_t)d["width"] : 0;
			int depth = d.has("depth") ? (int)(real_t)d["depth"] : 0;
			PoolRealArray heights = d.has("heights") ? (PoolRealArray)(Variant)d["heights"] : PoolRealArray();
			ERR_FAIL_COND(width < 2 || depth < 2 || heights.size() < width * depth);
			PoolRealArray::Read r = heights.read();
			LocalVector<float> h;
			h.resize(width * depth);
			for (int i = 0; i < width * depth; i++) {
				h[i] = r[i];
			}
			b3HeightFieldDef hfdef = { 0 };
			hfdef.heights = h.ptr();
			hfdef.scale = { 1.0f, 1.0f, 1.0f };
			hfdef.countX = width;
			hfdef.countZ = depth;
			hfdef.globalMinimumHeight = d.has("min_height") ? (float)(real_t)d["min_height"] : -1e30f;
			hfdef.globalMaximumHeight = d.has("max_height") ? (float)(real_t)d["max_height"] : 1e30f;
			free_shape_geometry(p_idx);
			si.height_data = b3CreateHeightField(&hfdef);
			ERR_FAIL_NULL(si.height_data);
			// ponytail: Godot centers the height map grid on the body origin while
			// Box3D's grid starts at (0,0); the offset cannot be baked into a height
			// field, so height maps created off-origin lose that offset.
			si.id = b3CreateHeightFieldShape(id, &def, si.height_data);
		} break;
		case PhysicsServer::SHAPE_RAY: {
			// Box3D has no ray shape: rays take part in casts only, never in
			// contact solving, so a ray shape on a body is a no-op here.
		} break;
		default: {
			si.id = b3_create_godot_shape(id, def, si.shape, shape_xform);
			if (B3_IS_NULL(si.id)) {
				ERR_PRINT("Box3D: failed to create shape type " + itos(si.shape->type) + ", ignoring it.");
			}
		} break;
	}
}

void Box3DBody::rebuild_shapes() {
	// ponytail: rebuilds every shape on any shape change, so building a body with
	// n shapes is O(n^2). Bodies carry a handful of shapes; make it incremental if
	// a real scene ever shows this in a profile.
	if (!in_world()) {
		return;
	}
	for (int i = 0; i < shapes.size(); i++) {
		if (B3_IS_NON_NULL(shapes[i].id)) {
			b3DestroyShape(shapes[i].id, false);
			shapes.write[i].id = b3_nullShapeId;
		}
		free_shape_geometry(i);
	}
	for (int i = 0; i < shapes.size(); i++) {
		_create_shape(i);
	}
	apply_mass();
}

void Box3DBody::apply_mass() {
	if (!in_world()) {
		return;
	}

	// Shapes are created with updateBodyMass = false, which leaves the body's mass
	// flagged dirty. Box3D asserts when it steps a body that still carries that flag,
	// static and kinematic ones included, so this has to run for every body type.
	b3Body_ApplyMassFromShapes(id);

	if (b3Body_GetType(id) != b3_dynamicBody) {
		return;
	}

	const float m = MAX((float)mass, (float)CMP_EPSILON);
	b3MassData md = b3Body_GetMassData(id);
	if (md.mass > 0.0f) {
		// Shapes give the shape of the inertia tensor, Godot's mass property gives its scale.
		md.inertia = b3_scale_matrix(md.inertia, m / md.mass);
	} else {
		// A shapeless rigid body is legal in Godot. Give it something solvable.
		md.center = b3Vec3_zero;
		b3Matrix3 unit = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } };
		md.inertia = b3_scale_matrix(unit, m);
	}
	md.mass = m;
	b3Body_SetMassData(id, md);
}

void Box3DBody::apply_damping(real_t p_linear, real_t p_angular) {
	if (!in_world()) {
		return;
	}
	// A negative body damp means "use the space default", which Godot's World
	// pushes in as an area parameter. An area override passes explicit values.
	real_t ld = p_linear >= 0 ? p_linear : (linear_damp >= 0 ? linear_damp : (space ? space->area_linear_damp : 0.0));
	real_t ad = p_angular >= 0 ? p_angular : (angular_damp >= 0 ? angular_damp : (space ? space->area_angular_damp : 0.0));
	b3Body_SetLinearDamping(id, MAX((float)ld, 0.0f));
	b3Body_SetAngularDamping(id, MAX((float)ad, 0.0f));
}

void Box3DBody::apply_filter() {
	if (!in_world()) {
		return;
	}
	b3Filter filter = b3DefaultFilter();
	filter.categoryBits = collision_layer;
	filter.maskBits = (uint64_t)0xFFFFFFFF | BOX3D_QUERY_BIT | BOX3D_SENSOR_BIT;
	for (int i = 0; i < shapes.size(); i++) {
		if (B3_IS_NON_NULL(shapes[i].id)) {
			b3Shape_SetFilter(shapes[i].id, filter, true);
		}
	}
}

void Box3DBody::apply_material() {
	if (!in_world()) {
		return;
	}
	for (int i = 0; i < shapes.size(); i++) {
		if (B3_IS_NON_NULL(shapes[i].id)) {
			b3Shape_SetFriction(shapes[i].id, friction);
			b3Shape_SetRestitution(shapes[i].id, bounce);
		}
	}
}

void Box3DBody::apply_motion_locks() {
	if (!in_world()) {
		return;
	}
	uint32_t axes = locked_axes;
	if (mode == PhysicsServer::BODY_MODE_CHARACTER) {
		axes |= PhysicsServer::BODY_AXIS_ANGULAR_X | PhysicsServer::BODY_AXIS_ANGULAR_Y | PhysicsServer::BODY_AXIS_ANGULAR_Z;
	}
	b3MotionLocks locks;
	locks.linearX = axes & PhysicsServer::BODY_AXIS_LINEAR_X;
	locks.linearY = axes & PhysicsServer::BODY_AXIS_LINEAR_Y;
	locks.linearZ = axes & PhysicsServer::BODY_AXIS_LINEAR_Z;
	locks.angularX = axes & PhysicsServer::BODY_AXIS_ANGULAR_X;
	locks.angularY = axes & PhysicsServer::BODY_AXIS_ANGULAR_Y;
	locks.angularZ = axes & PhysicsServer::BODY_AXIS_ANGULAR_Z;
	b3Body_SetMotionLocks(id, locks);
}

void Box3DBody::apply_exceptions() {
	// Exception pairs are realized as Box3D filter joints, which disable
	// collision between the two bodies wherever both exist.
	if (!space || !in_world()) {
		return;
	}
	for (Set<RID>::Element *E = exceptions.front(); E; E = E->next()) {
		if (exception_joints.has(E->get())) {
			continue;
		}
		Box3DBody *other = space->owner_body(E->get());
		if (!other || !other->in_world()) {
			continue;
		}
		b3FilterJointDef def = b3DefaultFilterJointDef();
		def.base.bodyIdA = id;
		def.base.bodyIdB = other->id;
		b3JointId jid = b3CreateFilterJoint(space->world, &def);
		if (B3_IS_NON_NULL(jid)) {
			exception_joints[E->get()] = jid;
			other->exception_joints[self] = jid;
		}
	}
}

void Box3DBody::clear_exceptions() {
	// The b3 filter joints die with the body itself; only the records matter.
	exceptions.clear();
	exception_joints.clear();
}

Transform Box3DBody::get_transform() const {
	if (!in_world()) {
		return transform;
	}
	Transform result = g_transform(b3Body_GetPosition(id), b3Body_GetRotation(id));
	result.basis.scale_local(transform.basis.get_scale());
	return result;
}

Vector3 Box3DBody::get_linear_velocity() const {
	return in_world() ? g_vec(b3Body_GetLinearVelocity(id)) : linear_velocity;
}

Vector3 Box3DBody::get_angular_velocity() const {
	return in_world() ? g_vec(b3Body_GetAngularVelocity(id)) : angular_velocity;
}

bool Box3DBody::is_sleeping() const {
	return in_world() ? !b3Body_IsAwake(id) : sleeping;
}

void Box3DBody::dispatch_force_integration(real_t p_delta) {
	if (fi_callback_id == 0 || !in_world()) {
		return;
	}

	// Same rule as the Bullet backend: report while moving, plus the one frame
	// where the body falls asleep, so nodes settle on the final transform.
	const bool awake = b3Body_IsAwake(id);
	const bool report = awake || awake != was_awake;
	was_awake = awake;
	if (!report) {
		return;
	}

	Object *obj = ObjectDB::get_instance(fi_callback_id);
	if (!obj) {
		fi_callback_id = 0;
		return;
	}

	Box3DDirectBodyState *state = Box3DDirectBodyState::get_singleton();
	state->body = this;
	state->delta = p_delta;

	Variant v_state = state;
	const Variant *argv[2] = { &v_state, &fi_callback_udata };
	const int argc = (fi_callback_udata.get_type() == Variant::NIL) ? 1 : 2;
	Variant::CallError err;
	obj->call(fi_callback_method, argv, argc, err);

	state->body = nullptr;
}

void Box3DBody::collect_contacts() {
	contacts.clear();
	if (!in_world() || max_contacts_reported <= 0 || mode == PhysicsServer::BODY_MODE_STATIC) {
		return;
	}

	int capacity = b3Body_GetContactCapacity(id);
	if (capacity <= 0) {
		return;
	}
	LocalVector<b3ContactData> data;
	data.resize(capacity);
	int count = b3Body_GetContactData(id, data.ptr(), capacity);

	b3Pos own_center = b3Body_GetWorldCenter(id);
	Transform own_xform = get_transform();

	for (int i = 0; i < count && contacts.size() < max_contacts_reported; i++) {
		const b3ContactData &cd = data[i];

		// Identify which side of the pair we are on.
		b3BodyId body_a = b3Shape_GetBody(cd.shapeIdA);
		bool ours_is_a = B3_ID_EQUALS(body_a, id);
		b3ShapeId own_shape = ours_is_a ? cd.shapeIdA : cd.shapeIdB;
		b3ShapeId other_shape = ours_is_a ? cd.shapeIdB : cd.shapeIdA;
		b3BodyId other_id = b3Shape_GetBody(other_shape);
		Box3DBody *other = (Box3DBody *)b3Body_GetUserData(other_id);
		if (!other || ((Box3DEntity *)other)->is_area) {
			continue;
		}

		for (int m = 0; m < cd.manifoldCount; m++) {
			const b3Manifold &manifold = cd.manifolds[m];
			if (manifold.pointCount == 0) {
				continue;
			}
			// Speculative manifolds may only carry separated points; report
			// pairs that are actually touching.
			const b3ManifoldPoint &pt = manifold.points[0];
			if (pt.separation > B3_LINEAR_SLOP) {
				continue;
			}

			Box3DContact c;
			// Anchors are relative to each side's center of mass, in world space.
			b3Vec3 anchor = ours_is_a ? pt.anchorA : pt.anchorB;
			Vector3 world_pt = g_pos(own_center) + g_vec(anchor);
			c.world_position = world_pt;
			c.local_position = world_pt - own_xform.origin;
			// The manifold normal runs from shape A to shape B; Godot wants it
			// pointing from the collider toward the reporting body.
			c.normal = ours_is_a ? -g_vec(manifold.normal) : g_vec(manifold.normal);
			c.impulse = pt.normalImpulse;
			c.local_shape = (int)(intptr_t)b3Shape_GetUserData(own_shape);
			c.collider = other->self;
			c.collider_id = other->instance_id;
			c.collider_shape = (int)(intptr_t)b3Shape_GetUserData(other_shape);
			contacts.push_back(c);
			break; // one report per touching pair, like the Bullet backend
		}
	}
}

/* Box3DArea */

Box3DArea::~Box3DArea() {
	_destroy_in_world();
}

void Box3DArea::set_space(Box3DSpace *p_space) {
	if (space == p_space) {
		return;
	}
	if (space) {
		_destroy_in_world();
		space->areas.erase(this);
	}
	space = p_space;
	if (space) {
		space->areas.push_back(this);
		_create_in_world();
	}
}

void Box3DArea::set_transform(const Transform &p_transform) {
	const Vector3 old_scale = transform.basis.get_scale();
	transform = p_transform;
	if (in_world()) {
		b3Body_SetTransform(id, b3_pos(p_transform.origin), b3_quat(p_transform.basis));
		if (!old_scale.is_equal_approx(transform.basis.get_scale())) {
			rebuild_shapes();
		}
	}
}

void Box3DArea::_create_in_world() {
	ERR_FAIL_COND(!space || B3_IS_NULL(space->world));

	// Kinematic so the sensors keep moving with the area and never sleep.
	b3BodyDef def = b3DefaultBodyDef();
	def.type = b3_kinematicBody;
	def.position = b3_pos(transform.origin);
	def.rotation = b3_quat(transform.basis);
	def.userData = this;

	id = b3CreateBody(space->world, &def);
	ERR_FAIL_COND(B3_IS_NULL(id));
	is_area = true;

	for (int i = 0; i < shapes.size(); i++) {
		_create_shape(i);
	}
	// Sensor shapes are created with updateBodyMass = false; Box3D asserts on
	// step while the body still carries that flag, statics included.
	b3Body_ApplyMassFromShapes(id);
}

void Box3DArea::_destroy_in_world() {
	if (!in_world()) {
		return;
	}
	b3DestroyBody(id);
	id = b3_nullBodyId;
	for (int i = 0; i < shapes.size(); i++) {
		shapes.write[i].id = b3_nullShapeId;
	}
}

void Box3DArea::_create_shape(int p_idx) {
	ShapeInstance &si = shapes.write[p_idx];
	si.id = b3_nullShapeId;
	if (!in_world() || si.disabled || !si.shape) {
		return;
	}

	b3ShapeDef def = b3DefaultShapeDef();
	def.isSensor = true;
	def.enableSensorEvents = true;
	def.updateBodyMass = false;
	def.filter.categoryBits = (uint64_t)collision_layer | BOX3D_SENSOR_BIT;
	def.filter.maskBits = (uint64_t)collision_mask | BOX3D_QUERY_BIT;
	def.userData = (void *)(intptr_t)p_idx;

	// Areas used to duplicate the body path and had drifted from it: no scale,
	// no cylinders, a sphere that ignored its local offset, and no thickening
	// for flat convex plates. One builder, one behaviour.
	si.id = b3_create_godot_shape(id, def, si.shape, b3_shape_transform(transform, si.xform));
	if (B3_IS_NULL(si.id)) {
		ERR_PRINT("Box3D: shape type " + itos(si.shape->type) + " is not supported on areas, ignoring it.");
	}

}

void Box3DArea::rebuild_shapes() {
	if (!in_world()) {
		return;
	}
	for (int i = 0; i < shapes.size(); i++) {
		if (B3_IS_NON_NULL(shapes[i].id)) {
			b3DestroyShape(shapes[i].id, false);
			shapes.write[i].id = b3_nullShapeId;
		}
	}
	for (int i = 0; i < shapes.size(); i++) {
		_create_shape(i);
	}
	// Sensor shapes are created with updateBodyMass = false; Box3D asserts on
	// step while the body still carries that flag, statics included.
	b3Body_ApplyMassFromShapes(id);
}

void Box3DArea::apply_filter() {
	if (!in_world()) {
		return;
	}
	b3Filter filter = b3DefaultFilter();
	filter.categoryBits = (uint64_t)collision_layer | BOX3D_SENSOR_BIT;
	filter.maskBits = (uint64_t)collision_mask | BOX3D_QUERY_BIT;
	for (int i = 0; i < shapes.size(); i++) {
		if (B3_IS_NON_NULL(shapes[i].id)) {
			b3Shape_SetFilter(shapes[i].id, filter, true);
		}
	}
}

void Box3DArea::set_monitor_callback(Object *p_receiver, const StringName &p_method) {
	monitor_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	monitor_callback_method = p_method;
}

void Box3DArea::set_area_monitor_callback(Object *p_receiver, const StringName &p_method) {
	area_monitor_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	area_monitor_callback_method = p_method;
}

void Box3DArea::report_body(uint32_t p_status, Box3DBody *p_body, int p_body_shape, int p_area_shape) {
	if (monitor_callback_id == 0) {
		return;
	}
	Object *obj = ObjectDB::get_instance(monitor_callback_id);
	if (!obj) {
		monitor_callback_id = 0;
		return;
	}
	Variant v_status = (int)p_status;
	Variant v_rid = p_body->self;
	Variant v_instance = (int)p_body->instance_id;
	Variant v_body_shape = p_body_shape;
	Variant v_area_shape = p_area_shape;
	const Variant *argv[5] = { &v_status, &v_rid, &v_instance, &v_body_shape, &v_area_shape };
	Variant::CallError err;
	obj->call(monitor_callback_method, argv, 5, err);
}

void Box3DArea::report_area(uint32_t p_status, Box3DArea *p_area, int p_area_shape, int p_self_shape) {
	if (area_monitor_callback_id == 0) {
		return;
	}
	Object *obj = ObjectDB::get_instance(area_monitor_callback_id);
	if (!obj) {
		area_monitor_callback_id = 0;
		return;
	}
	Variant v_status = (int)p_status;
	Variant v_rid = p_area->self;
	Variant v_instance = (int)p_area->instance_id;
	Variant v_area_shape = p_area_shape;
	Variant v_self_shape = p_self_shape;
	const Variant *argv[5] = { &v_status, &v_rid, &v_instance, &v_area_shape, &v_self_shape };
	Variant::CallError err;
	obj->call(area_monitor_callback_method, argv, 5, err);
}

/* Box3DSpace */

Box3DSpace::Box3DSpace() {
	b3WorldDef def = b3DefaultWorldDef();
	// ponytail: single threaded. Raise once M7 shows threading keeps determinism.
	def.workerCount = 1;
	world = b3CreateWorld(&def);
	b3World_SetCustomFilterCallback(world, godot_collision_filter, nullptr);
	direct_state = memnew(Box3DDirectSpaceState);
	direct_state->space = this;
	apply_gravity();

	if (ProjectSettings::get_singleton()->has_setting("physics/3d/box3d_substeps")) {
		sub_steps = CLAMP((int)ProjectSettings::get_singleton()->get_setting("physics/3d/box3d_substeps"), 1, 8);
	}
}

Box3DSpace::~Box3DSpace() {
	// Bodies outlive the space in Godot, so detach them before the world dies.
	while (!bodies.empty()) {
		bodies.front()->get()->set_space(nullptr);
	}
	while (!areas.empty()) {
		areas.front()->get()->set_space(nullptr);
	}
	if (B3_IS_NON_NULL(world)) {
		b3DestroyWorld(world);
		world = b3_nullWorldId;
	}
	if (direct_state) {
		memdelete(direct_state);
		direct_state = nullptr;
	}
}

Box3DBody *Box3DSpace::owner_body(RID p_rid) const {
	for (const List<Box3DBody *>::Element *E = bodies.front(); E; E = E->next()) {
		if (E->get()->self == p_rid) {
			return E->get();
		}
	}
	return nullptr;
}

void Box3DSpace::apply_gravity() {
	if (B3_IS_NULL(world)) {
		return;
	}
	b3World_SetGravity(world, b3_vec(gravity_vector * gravity_magnitude));
}

void Box3DSpace::step(real_t p_delta) {
	last_step = p_delta;
	apply_area_overrides();
	b3World_Step(world, p_delta, sub_steps);
	pump_events(p_delta);
	for (List<Box3DBody *>::Element *E = bodies.front(); E; E = E->next()) {
		E->get()->dispatch_force_integration(p_delta);
	}
}

/* Box3DDirectBodyState */

Box3DDirectBodyState *Box3DDirectBodyState::singleton = nullptr;

Box3DDirectBodyState *Box3DDirectBodyState::get_singleton() {
	if (!singleton) {
		singleton = memnew(Box3DDirectBodyState);
	}
	return singleton;
}

void Box3DDirectBodyState::free_singleton() {
	if (singleton) {
		memdelete(singleton);
		singleton = nullptr;
	}
}

Vector3 Box3DDirectBodyState::get_total_gravity() const {
	ERR_FAIL_COND_V(!body, Vector3());
	return body->total_gravity;
}

float Box3DDirectBodyState::get_total_angular_damp() const {
	ERR_FAIL_COND_V(!body, 0);
	return body->total_angular_damp;
}

float Box3DDirectBodyState::get_total_linear_damp() const {
	ERR_FAIL_COND_V(!body, 0);
	return body->total_linear_damp;
}

Vector3 Box3DDirectBodyState::get_center_of_mass() const {
	ERR_FAIL_COND_V(!body || !body->in_world(), Vector3());
	return g_vec(b3Body_GetLocalCenter(body->id));
}

Basis Box3DDirectBodyState::get_principal_inertia_axes() const {
	return Basis();
}

float Box3DDirectBodyState::get_inverse_mass() const {
	ERR_FAIL_COND_V(!body || !body->in_world(), 0);
	return b3Body_GetInverseMass(body->id);
}

Vector3 Box3DDirectBodyState::get_inverse_inertia() const {
	ERR_FAIL_COND_V(!body || !body->in_world(), Vector3());
	b3Matrix3 i = b3Body_GetLocalRotationalInertia(body->id);
	return Vector3(i.cx.x != 0 ? 1.0 / i.cx.x : 0.0, i.cy.y != 0 ? 1.0 / i.cy.y : 0.0, i.cz.z != 0 ? 1.0 / i.cz.z : 0.0);
}

Basis Box3DDirectBodyState::get_inverse_inertia_tensor() const {
	ERR_FAIL_COND_V(!body || !body->in_world(), Basis());
	return g_basis(b3Body_GetWorldInverseRotationalInertia(body->id));
}

void Box3DDirectBodyState::set_linear_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_SetLinearVelocity(body->id, b3_vec(p_velocity));
}

Vector3 Box3DDirectBodyState::get_linear_velocity() const {
	ERR_FAIL_COND_V(!body, Vector3());
	return body->get_linear_velocity();
}

void Box3DDirectBodyState::set_angular_velocity(const Vector3 &p_velocity) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_SetAngularVelocity(body->id, b3_vec(p_velocity));
}

Vector3 Box3DDirectBodyState::get_angular_velocity() const {
	ERR_FAIL_COND_V(!body, Vector3());
	return body->get_angular_velocity();
}

void Box3DDirectBodyState::set_transform(const Transform &p_transform) {
	ERR_FAIL_COND(!body);
	body->transform = p_transform;
	if (body->in_world()) {
		b3Body_SetTransform(body->id, b3_pos(p_transform.origin), b3_quat(p_transform.basis));
	}
}

Transform Box3DDirectBodyState::get_transform() const {
	ERR_FAIL_COND_V(!body, Transform());
	return body->get_transform();
}

Vector3 Box3DDirectBodyState::get_velocity_at_local_position(const Vector3 &p_position) const {
	ERR_FAIL_COND_V(!body || !body->in_world(), Vector3());
	return g_vec(b3Body_GetLocalPointVelocity(body->id, b3_vec(p_position)));
}

void Box3DDirectBodyState::add_central_force(const Vector3 &p_force) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_ApplyForceToCenter(body->id, b3_vec(p_force), true);
}

void Box3DDirectBodyState::add_force(const Vector3 &p_force, const Vector3 &p_pos) {
	ERR_FAIL_COND(!body || !body->in_world());
	// Godot passes an offset from the body origin, Box3D wants a world point.
	b3Body_ApplyForce(body->id, b3_vec(p_force), b3_pos(body->get_transform().origin + p_pos), true);
}

void Box3DDirectBodyState::add_torque(const Vector3 &p_torque) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_ApplyTorque(body->id, b3_vec(p_torque), true);
}

void Box3DDirectBodyState::apply_central_impulse(const Vector3 &p_j) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_ApplyLinearImpulseToCenter(body->id, b3_vec(p_j), true);
}

void Box3DDirectBodyState::apply_impulse(const Vector3 &p_pos, const Vector3 &p_j) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_ApplyLinearImpulse(body->id, b3_vec(p_j), b3_pos(body->get_transform().origin + p_pos), true);
}

void Box3DDirectBodyState::apply_torque_impulse(const Vector3 &p_j) {
	ERR_FAIL_COND(!body || !body->in_world());
	b3Body_ApplyAngularImpulse(body->id, b3_vec(p_j), true);
}

void Box3DDirectBodyState::set_sleep_state(bool p_enable) {
	ERR_FAIL_COND(!body);
	body->sleeping = p_enable;
	if (body->in_world()) {
		b3Body_SetAwake(body->id, !p_enable);
	}
}

bool Box3DDirectBodyState::is_sleeping() const {
	ERR_FAIL_COND_V(!body, false);
	return body->is_sleeping();
}

int Box3DDirectBodyState::get_contact_count() const {
	ERR_FAIL_COND_V(!body, 0);
	return body->contacts.size();
}

Vector3 Box3DDirectBodyState::get_contact_local_position(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), Vector3());
	return body->contacts[p_contact_idx].local_position;
}

Vector3 Box3DDirectBodyState::get_contact_local_normal(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), Vector3());
	return body->contacts[p_contact_idx].normal;
}

float Box3DDirectBodyState::get_contact_impulse(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), 0);
	return body->contacts[p_contact_idx].impulse;
}

int Box3DDirectBodyState::get_contact_local_shape(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), 0);
	return body->contacts[p_contact_idx].local_shape;
}

RID Box3DDirectBodyState::get_contact_collider(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), RID());
	return body->contacts[p_contact_idx].collider;
}

Vector3 Box3DDirectBodyState::get_contact_collider_position(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), Vector3());
	return body->contacts[p_contact_idx].world_position;
}

ObjectID Box3DDirectBodyState::get_contact_collider_id(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), 0);
	return body->contacts[p_contact_idx].collider_id;
}

int Box3DDirectBodyState::get_contact_collider_shape(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), 0);
	return body->contacts[p_contact_idx].collider_shape;
}

Vector3 Box3DDirectBodyState::get_contact_collider_velocity_at_position(int p_contact_idx) const {
	ERR_FAIL_COND_V(!body || !body->space || p_contact_idx < 0 || p_contact_idx >= body->contacts.size(), Vector3());
	const Box3DContact &c = body->contacts[p_contact_idx];
	// The collider RID names a body in this space; find its b3 id there.
	for (List<Box3DBody *>::Element *E = body->space->bodies.front(); E; E = E->next()) {
		if (E->get()->self == c.collider && E->get()->in_world()) {
			return g_vec(b3Body_GetWorldPointVelocity(E->get()->id, b3_pos(c.world_position)));
		}
	}
	return Vector3();
}

PhysicsDirectSpaceState *Box3DDirectBodyState::get_space_state() {
	ERR_FAIL_COND_V(!body || !body->space, nullptr);
	return body->space->direct_state;
}
