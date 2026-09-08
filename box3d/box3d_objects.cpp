/**************************************************************************/
/*  box3d_objects.cpp                                                     */
/**************************************************************************/

#include "box3d_objects.h"

#include "core/object.h"

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

/* Box3DBody */

Box3DBody::~Box3DBody() {
	_destroy_in_world();
}

void Box3DBody::set_space(Box3DSpace *p_space) {
	if (space == p_space) {
		return;
	}
	if (space) {
		_destroy_in_world();
		space->bodies.erase(this);
	}
	space = p_space;
	if (space) {
		space->bodies.push_back(this);
		_create_in_world();
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
	def.gravityScale = gravity_scale;
	def.enableSleep = can_sleep;
	def.isAwake = !sleeping;
	def.isBullet = ccd;

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

	b3DestroyBody(id); // Destroys the body's shapes too.
	id = b3_nullBodyId;
	for (int i = 0; i < shapes.size(); i++) {
		shapes.write[i].id = b3_nullShapeId;
	}
}

void Box3DBody::_create_shape(int p_idx) {
	ShapeInstance &si = shapes.write[p_idx];
	si.id = b3_nullShapeId;
	if (!in_world() || si.disabled || !si.shape) {
		return;
	}

	b3ShapeDef def = b3DefaultShapeDef();
	def.baseMaterial.friction = friction;
	def.baseMaterial.restitution = bounce;
	def.filter.categoryBits = collision_layer;
	def.filter.maskBits = collision_mask;
	// Mass is applied once, after every shape exists, by apply_mass().
	def.updateBodyMass = false;

	switch (si.shape->type) {
		case PhysicsServer::SHAPE_BOX: {
			Vector3 he = si.shape->data;
			b3BoxHull hull = b3MakeBoxHull(MAX((float)he.x, B3_LINEAR_SLOP), MAX((float)he.y, B3_LINEAR_SLOP), MAX((float)he.z, B3_LINEAR_SLOP));
			// Box3D bakes the local transform into a world-owned clone of the hull,
			// so the stack copy above does not need to outlive this call.
			si.id = b3CreateTransformedHullShape(id, &def, &hull.base, b3_transform(si.xform), b3Vec3_one);
		} break;
		default: {
			ERR_PRINT("Box3D: shape type " + itos(si.shape->type) + " is not supported yet, ignoring it.");
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
	}
	for (int i = 0; i < shapes.size(); i++) {
		_create_shape(i);
	}
	apply_mass();
}

void Box3DBody::apply_mass() {
	if (!in_world() || b3Body_GetType(id) != b3_dynamicBody) {
		return;
	}

	b3Body_ApplyMassFromShapes(id);

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

void Box3DBody::apply_damping() {
	if (!in_world()) {
		return;
	}
	// A negative body damp means "use the space default", which Godot's World
	// pushes in as an area parameter.
	real_t ld = linear_damp >= 0 ? linear_damp : (space ? space->area_linear_damp : 0.0);
	real_t ad = angular_damp >= 0 ? angular_damp : (space ? space->area_angular_damp : 0.0);
	b3Body_SetLinearDamping(id, MAX((float)ld, 0.0f));
	b3Body_SetAngularDamping(id, MAX((float)ad, 0.0f));
}

void Box3DBody::apply_filter() {
	if (!in_world()) {
		return;
	}
	b3Filter filter = b3DefaultFilter();
	filter.categoryBits = collision_layer;
	filter.maskBits = collision_mask;
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

Transform Box3DBody::get_transform() const {
	if (!in_world()) {
		return transform;
	}
	return g_transform(b3Body_GetPosition(id), b3Body_GetRotation(id));
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

/* Box3DSpace */

Box3DSpace::Box3DSpace() {
	b3WorldDef def = b3DefaultWorldDef();
	// ponytail: single threaded. Raise once M7 shows threading keeps determinism.
	def.workerCount = 1;
	world = b3CreateWorld(&def);
	direct_state = memnew(Box3DDirectSpaceState);
	direct_state->space = this;
	apply_gravity();
}

Box3DSpace::~Box3DSpace() {
	// Bodies outlive the space in Godot, so detach them before the world dies.
	while (!bodies.empty()) {
		bodies.front()->get()->set_space(nullptr);
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

void Box3DSpace::apply_gravity() {
	if (B3_IS_NULL(world)) {
		return;
	}
	b3World_SetGravity(world, b3_vec(gravity_vector * gravity_magnitude));
}

void Box3DSpace::step(real_t p_delta) {
	last_step = p_delta;
	b3World_Step(world, p_delta, 4);
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
	if (!body->space) {
		return Vector3();
	}
	return body->space->gravity_vector * body->space->gravity_magnitude * body->gravity_scale;
}

float Box3DDirectBodyState::get_total_angular_damp() const {
	ERR_FAIL_COND_V(!body, 0);
	return body->in_world() ? b3Body_GetAngularDamping(body->id) : 0;
}

float Box3DDirectBodyState::get_total_linear_damp() const {
	ERR_FAIL_COND_V(!body, 0);
	return body->in_world() ? b3Body_GetLinearDamping(body->id) : 0;
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

PhysicsDirectSpaceState *Box3DDirectBodyState::get_space_state() {
	ERR_FAIL_COND_V(!body || !body->space, nullptr);
	return body->space->direct_state;
}
