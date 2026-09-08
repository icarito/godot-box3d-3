/**************************************************************************/
/*  box3d_physics_server.cpp                                              */
/**************************************************************************/

#include "box3d_physics_server.h"

#define GET_OR_FAIL(m_type, m_var, m_owner, m_rid)   \
	m_type *m_var = m_owner.getornull(m_rid);        \
	ERR_FAIL_COND(!m_var)

#define GET_OR_FAIL_V(m_type, m_var, m_owner, m_rid, m_ret) \
	m_type *m_var = m_owner.getornull(m_rid);               \
	ERR_FAIL_COND_V(!m_var, m_ret)

/* Shapes */

RID Box3DPhysicsServer::shape_create(PhysicsServer::ShapeType p_shape) {
	Box3DShape *shape = memnew(Box3DShape);
	shape->type = p_shape;
	RID rid = shape_owner.make_rid(shape);
	shape->self = rid;
	return rid;
}

void Box3DPhysicsServer::shape_set_data(RID p_shape, const Variant &p_data) {
	GET_OR_FAIL(Box3DShape, shape, shape_owner, p_shape);
	shape->data = p_data;
	for (Set<Box3DBody *>::Element *E = shape->owners.front(); E; E = E->next()) {
		E->get()->rebuild_shapes();
	}
}

void Box3DPhysicsServer::shape_set_custom_solver_bias(RID p_shape, real_t p_bias) {
	GET_OR_FAIL(Box3DShape, shape, shape_owner, p_shape);
	shape->custom_bias = p_bias;
}

PhysicsServer::ShapeType Box3DPhysicsServer::shape_get_type(RID p_shape) const {
	GET_OR_FAIL_V(Box3DShape, shape, shape_owner, p_shape, PhysicsServer::SHAPE_CUSTOM);
	return shape->type;
}

Variant Box3DPhysicsServer::shape_get_data(RID p_shape) const {
	GET_OR_FAIL_V(Box3DShape, shape, shape_owner, p_shape, Variant());
	return shape->data;
}

void Box3DPhysicsServer::shape_set_margin(RID p_shape, real_t p_margin) {
	GET_OR_FAIL(Box3DShape, shape, shape_owner, p_shape);
	shape->margin = p_margin;
}

real_t Box3DPhysicsServer::shape_get_margin(RID p_shape) const {
	GET_OR_FAIL_V(Box3DShape, shape, shape_owner, p_shape, 0);
	return shape->margin;
}

real_t Box3DPhysicsServer::shape_get_custom_solver_bias(RID p_shape) const {
	GET_OR_FAIL_V(Box3DShape, shape, shape_owner, p_shape, 0);
	return shape->custom_bias;
}

/* Spaces */

RID Box3DPhysicsServer::space_create() {
	Box3DSpace *space = memnew(Box3DSpace);
	RID rid = space_owner.make_rid(space);
	space->self = rid;
	return rid;
}

void Box3DPhysicsServer::space_set_active(RID p_space, bool p_active) {
	GET_OR_FAIL(Box3DSpace, space, space_owner, p_space);
	if (space->active == p_active) {
		return;
	}
	space->active = p_active;
	if (p_active) {
		active_spaces.push_back(space);
	} else {
		active_spaces.erase(space);
	}
}

bool Box3DPhysicsServer::space_is_active(RID p_space) const {
	GET_OR_FAIL_V(Box3DSpace, space, space_owner, p_space, false);
	return space->active;
}

void Box3DPhysicsServer::space_set_param(RID p_space, PhysicsServer::SpaceParameter p_param, real_t p_value) {
	// Box3D tunes contacts through the world def, not per-parameter. M2 ignores these.
}

real_t Box3DPhysicsServer::space_get_param(RID p_space, PhysicsServer::SpaceParameter p_param) const {
	return 0;
}

PhysicsDirectSpaceState *Box3DPhysicsServer::space_get_direct_state(RID p_space) {
	GET_OR_FAIL_V(Box3DSpace, space, space_owner, p_space, nullptr);
	return space->direct_state;
}

void Box3DPhysicsServer::space_set_debug_contacts(RID p_space, int p_max_contacts) {
}

Vector<Vector3> Box3DPhysicsServer::space_get_contacts(RID p_space) const {
	return Vector<Vector3>();
}

int Box3DPhysicsServer::space_get_contact_count(RID p_space) const {
	return 0;
}

/* Areas */

RID Box3DPhysicsServer::area_create() {
	Box3DArea *area = memnew(Box3DArea);
	return area_owner.make_rid(area);
}

void Box3DPhysicsServer::area_set_space(RID p_area, RID p_space) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->space = p_space;
}

RID Box3DPhysicsServer::area_get_space(RID p_area) const {
	GET_OR_FAIL_V(Box3DArea, area, area_owner, p_area, RID());
	return area->space;
}

void Box3DPhysicsServer::area_set_space_override_mode(RID p_area, PhysicsServer::AreaSpaceOverrideMode p_mode) {
}

PhysicsServer::AreaSpaceOverrideMode Box3DPhysicsServer::area_get_space_override_mode(RID p_area) const {
	return PhysicsServer::AREA_SPACE_OVERRIDE_DISABLED;
}

void Box3DPhysicsServer::area_add_shape(RID p_area, RID p_shape, const Transform &p_transform, bool p_disabled) {
}

void Box3DPhysicsServer::area_set_shape(RID p_area, int p_shape_idx, RID p_shape) {
}

void Box3DPhysicsServer::area_set_shape_transform(RID p_area, int p_shape_idx, const Transform &p_transform) {
}

int Box3DPhysicsServer::area_get_shape_count(RID p_area) const {
	return 0;
}

RID Box3DPhysicsServer::area_get_shape(RID p_area, int p_shape_idx) const {
	return RID();
}

Transform Box3DPhysicsServer::area_get_shape_transform(RID p_area, int p_shape_idx) const {
	return Transform();
}

void Box3DPhysicsServer::area_remove_shape(RID p_area, int p_shape_idx) {
}

void Box3DPhysicsServer::area_clear_shapes(RID p_area) {
}

void Box3DPhysicsServer::area_set_shape_disabled(RID p_area, int p_shape_idx, bool p_disabled) {
}

void Box3DPhysicsServer::area_attach_object_instance_id(RID p_area, ObjectID p_id) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->instance_id = p_id;
}

ObjectID Box3DPhysicsServer::area_get_object_instance_id(RID p_area) const {
	GET_OR_FAIL_V(Box3DArea, area, area_owner, p_area, 0);
	return area->instance_id;
}

void Box3DPhysicsServer::area_set_param(RID p_area, PhysicsServer::AreaParameter p_param, const Variant &p_value) {
	// Godot's World configures the space's default area through the space RID.
	if (space_owner.owns(p_area)) {
		Box3DSpace *space = space_owner.get(p_area);
		switch (p_param) {
			case AREA_PARAM_GRAVITY:
				space->gravity_magnitude = p_value;
				space->apply_gravity();
				break;
			case AREA_PARAM_GRAVITY_VECTOR:
				space->gravity_vector = p_value;
				space->apply_gravity();
				break;
			case AREA_PARAM_LINEAR_DAMP:
				space->area_linear_damp = p_value;
				for (List<Box3DBody *>::Element *E = space->bodies.front(); E; E = E->next()) {
					E->get()->apply_damping();
				}
				break;
			case AREA_PARAM_ANGULAR_DAMP:
				space->area_angular_damp = p_value;
				for (List<Box3DBody *>::Element *E = space->bodies.front(); E; E = E->next()) {
					E->get()->apply_damping();
				}
				break;
			default:
				break;
		}
		return;
	}

	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->params[p_param] = p_value;
}

void Box3DPhysicsServer::area_set_transform(RID p_area, const Transform &p_transform) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->transform = p_transform;
}

Variant Box3DPhysicsServer::area_get_param(RID p_area, PhysicsServer::AreaParameter p_param) const {
	if (space_owner.owns(p_area)) {
		Box3DSpace *space = space_owner.get(p_area);
		switch (p_param) {
			case AREA_PARAM_GRAVITY:
				return space->gravity_magnitude;
			case AREA_PARAM_GRAVITY_VECTOR:
				return space->gravity_vector;
			case AREA_PARAM_LINEAR_DAMP:
				return space->area_linear_damp;
			case AREA_PARAM_ANGULAR_DAMP:
				return space->area_angular_damp;
			default:
				return Variant();
		}
	}

	GET_OR_FAIL_V(Box3DArea, area, area_owner, p_area, Variant());
	const Map<PhysicsServer::AreaParameter, Variant>::Element *E = area->params.find(p_param);
	return E ? E->get() : Variant();
}

Transform Box3DPhysicsServer::area_get_transform(RID p_area) const {
	GET_OR_FAIL_V(Box3DArea, area, area_owner, p_area, Transform());
	return area->transform;
}

void Box3DPhysicsServer::area_set_collision_mask(RID p_area, uint32_t p_mask) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->collision_mask = p_mask;
}

void Box3DPhysicsServer::area_set_collision_layer(RID p_area, uint32_t p_layer) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->collision_layer = p_layer;
}

void Box3DPhysicsServer::area_set_monitorable(RID p_area, bool p_monitorable) {
}

void Box3DPhysicsServer::area_set_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method) {
}

void Box3DPhysicsServer::area_set_area_monitor_callback(RID p_area, Object *p_receiver, const StringName &p_method) {
}

void Box3DPhysicsServer::area_set_ray_pickable(RID p_area, bool p_enable) {
	GET_OR_FAIL(Box3DArea, area, area_owner, p_area);
	area->ray_pickable = p_enable;
}

bool Box3DPhysicsServer::area_is_ray_pickable(RID p_area) const {
	GET_OR_FAIL_V(Box3DArea, area, area_owner, p_area, false);
	return area->ray_pickable;
}

/* Bodies */

RID Box3DPhysicsServer::body_create(PhysicsServer::BodyMode p_mode, bool p_init_sleeping) {
	Box3DBody *body = memnew(Box3DBody);
	body->mode = p_mode;
	body->sleeping = p_init_sleeping;
	RID rid = body_owner.make_rid(body);
	body->self = rid;
	return rid;
}

void Box3DPhysicsServer::body_set_space(RID p_body, RID p_space) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	if (p_space.is_valid()) {
		GET_OR_FAIL(Box3DSpace, space, space_owner, p_space);
		body->set_space(space);
	} else {
		body->set_space(nullptr);
	}
}

RID Box3DPhysicsServer::body_get_space(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, RID());
	return body->space ? body->space->self : RID();
}

void Box3DPhysicsServer::body_set_mode(RID p_body, PhysicsServer::BodyMode p_mode) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	if (body->mode == p_mode) {
		return;
	}
	body->mode = p_mode;
	if (body->in_world()) {
		b3Body_SetType(body->id, p_mode == BODY_MODE_STATIC ? b3_staticBody : (p_mode == BODY_MODE_KINEMATIC ? b3_kinematicBody : b3_dynamicBody));
		body->apply_mass();
		body->apply_motion_locks();
	}
}

PhysicsServer::BodyMode Box3DPhysicsServer::body_get_mode(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, BODY_MODE_STATIC);
	return body->mode;
}

void Box3DPhysicsServer::body_add_shape(RID p_body, RID p_shape, const Transform &p_transform, bool p_disabled) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	GET_OR_FAIL(Box3DShape, shape, shape_owner, p_shape);

	Box3DBody::ShapeInstance si;
	si.shape = shape;
	si.xform = p_transform;
	si.disabled = p_disabled;
	body->shapes.push_back(si);
	shape->owners.insert(body);
	body->rebuild_shapes();
}

void Box3DPhysicsServer::body_set_shape(RID p_body, int p_shape_idx, RID p_shape) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	GET_OR_FAIL(Box3DShape, shape, shape_owner, p_shape);

	body->shapes.write[p_shape_idx].shape->owners.erase(body);
	body->shapes.write[p_shape_idx].shape = shape;
	shape->owners.insert(body);
	body->rebuild_shapes();
}

void Box3DPhysicsServer::body_set_shape_transform(RID p_body, int p_shape_idx, const Transform &p_transform) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	body->shapes.write[p_shape_idx].xform = p_transform;
	body->rebuild_shapes();
}

int Box3DPhysicsServer::body_get_shape_count(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->shapes.size();
}

RID Box3DPhysicsServer::body_get_shape(RID p_body, int p_shape_idx) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, RID());
	ERR_FAIL_INDEX_V(p_shape_idx, body->shapes.size(), RID());
	Box3DShape *shape = body->shapes[p_shape_idx].shape;
	return shape ? shape->self : RID();
}

Transform Box3DPhysicsServer::body_get_shape_transform(RID p_body, int p_shape_idx) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, Transform());
	ERR_FAIL_INDEX_V(p_shape_idx, body->shapes.size(), Transform());
	return body->shapes[p_shape_idx].xform;
}

void Box3DPhysicsServer::body_remove_shape(RID p_body, int p_shape_idx) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	Box3DShape *shape = body->shapes[p_shape_idx].shape;
	body->shapes.remove(p_shape_idx);
	if (shape) {
		bool still_used = false;
		for (int i = 0; i < body->shapes.size(); i++) {
			still_used = still_used || body->shapes[i].shape == shape;
		}
		if (!still_used) {
			shape->owners.erase(body);
		}
	}
	body->rebuild_shapes();
}

void Box3DPhysicsServer::body_clear_shapes(RID p_body) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	for (int i = 0; i < body->shapes.size(); i++) {
		if (body->shapes[i].shape) {
			body->shapes[i].shape->owners.erase(body);
		}
	}
	body->shapes.clear();
	body->rebuild_shapes();
}

void Box3DPhysicsServer::body_set_shape_disabled(RID p_body, int p_shape_idx, bool p_disabled) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_INDEX(p_shape_idx, body->shapes.size());
	body->shapes.write[p_shape_idx].disabled = p_disabled;
	body->rebuild_shapes();
}

void Box3DPhysicsServer::body_attach_object_instance_id(RID p_body, uint32_t p_id) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->instance_id = p_id;
}

uint32_t Box3DPhysicsServer::body_get_object_instance_id(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->instance_id;
}

void Box3DPhysicsServer::body_set_enable_continuous_collision_detection(RID p_body, bool p_enable) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->ccd = p_enable;
	if (body->in_world()) {
		b3Body_SetBullet(body->id, p_enable);
	}
}

bool Box3DPhysicsServer::body_is_continuous_collision_detection_enabled(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, false);
	return body->ccd;
}

void Box3DPhysicsServer::body_set_collision_layer(RID p_body, uint32_t p_layer) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->collision_layer = p_layer;
	body->apply_filter();
}

uint32_t Box3DPhysicsServer::body_get_collision_layer(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->collision_layer;
}

void Box3DPhysicsServer::body_set_collision_mask(RID p_body, uint32_t p_mask) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->collision_mask = p_mask;
	body->apply_filter();
}

uint32_t Box3DPhysicsServer::body_get_collision_mask(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->collision_mask;
}

void Box3DPhysicsServer::body_set_user_flags(RID p_body, uint32_t p_flags) {
}

uint32_t Box3DPhysicsServer::body_get_user_flags(RID p_body) const {
	return 0;
}

void Box3DPhysicsServer::body_set_param(RID p_body, PhysicsServer::BodyParameter p_param, float p_value) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	switch (p_param) {
		case BODY_PARAM_BOUNCE:
			body->bounce = p_value;
			body->apply_material();
			break;
		case BODY_PARAM_FRICTION:
			body->friction = p_value;
			body->apply_material();
			break;
		case BODY_PARAM_MASS:
			body->mass = p_value;
			body->apply_mass();
			break;
		case BODY_PARAM_GRAVITY_SCALE:
			body->gravity_scale = p_value;
			if (body->in_world()) {
				b3Body_SetGravityScale(body->id, p_value);
			}
			break;
		case BODY_PARAM_LINEAR_DAMP:
			body->linear_damp = p_value;
			body->apply_damping();
			break;
		case BODY_PARAM_ANGULAR_DAMP:
			body->angular_damp = p_value;
			body->apply_damping();
			break;
		default:
			break;
	}
}

float Box3DPhysicsServer::body_get_param(RID p_body, PhysicsServer::BodyParameter p_param) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	switch (p_param) {
		case BODY_PARAM_BOUNCE:
			return body->bounce;
		case BODY_PARAM_FRICTION:
			return body->friction;
		case BODY_PARAM_MASS:
			return body->mass;
		case BODY_PARAM_GRAVITY_SCALE:
			return body->gravity_scale;
		case BODY_PARAM_LINEAR_DAMP:
			return body->linear_damp;
		case BODY_PARAM_ANGULAR_DAMP:
			return body->angular_damp;
		default:
			return 0;
	}
}

void Box3DPhysicsServer::body_set_kinematic_safe_margin(RID p_body, real_t p_margin) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->kinematic_safe_margin = p_margin;
}

real_t Box3DPhysicsServer::body_get_kinematic_safe_margin(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->kinematic_safe_margin;
}

void Box3DPhysicsServer::body_set_state(RID p_body, PhysicsServer::BodyState p_state, const Variant &p_variant) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	switch (p_state) {
		case BODY_STATE_TRANSFORM: {
			body->transform = p_variant;
			if (body->in_world()) {
				b3Body_SetTransform(body->id, b3_pos(body->transform.origin), b3_quat(body->transform.basis));
			}
		} break;
		case BODY_STATE_LINEAR_VELOCITY: {
			body->linear_velocity = p_variant;
			if (body->in_world()) {
				b3Body_SetLinearVelocity(body->id, b3_vec(body->linear_velocity));
			}
		} break;
		case BODY_STATE_ANGULAR_VELOCITY: {
			body->angular_velocity = p_variant;
			if (body->in_world()) {
				b3Body_SetAngularVelocity(body->id, b3_vec(body->angular_velocity));
			}
		} break;
		case BODY_STATE_SLEEPING: {
			body->sleeping = p_variant;
			if (body->in_world()) {
				b3Body_SetAwake(body->id, !body->sleeping);
			}
		} break;
		case BODY_STATE_CAN_SLEEP: {
			body->can_sleep = p_variant;
			if (body->in_world()) {
				b3Body_EnableSleep(body->id, body->can_sleep);
			}
		} break;
	}
}

Variant Box3DPhysicsServer::body_get_state(RID p_body, PhysicsServer::BodyState p_state) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, Variant());
	switch (p_state) {
		case BODY_STATE_TRANSFORM:
			return body->get_transform();
		case BODY_STATE_LINEAR_VELOCITY:
			return body->get_linear_velocity();
		case BODY_STATE_ANGULAR_VELOCITY:
			return body->get_angular_velocity();
		case BODY_STATE_SLEEPING:
			return body->is_sleeping();
		case BODY_STATE_CAN_SLEEP:
			return body->can_sleep;
	}
	return Variant();
}

void Box3DPhysicsServer::body_set_applied_force(RID p_body, const Vector3 &p_force) {
	body_add_central_force(p_body, p_force);
}

Vector3 Box3DPhysicsServer::body_get_applied_force(RID p_body) const {
	return Vector3();
}

void Box3DPhysicsServer::body_set_applied_torque(RID p_body, const Vector3 &p_torque) {
	body_add_torque(p_body, p_torque);
}

Vector3 Box3DPhysicsServer::body_get_applied_torque(RID p_body) const {
	return Vector3();
}

void Box3DPhysicsServer::body_add_central_force(RID p_body, const Vector3 &p_force) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyForceToCenter(body->id, b3_vec(p_force), true);
}

void Box3DPhysicsServer::body_add_force(RID p_body, const Vector3 &p_force, const Vector3 &p_pos) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyForce(body->id, b3_vec(p_force), b3_pos(body->get_transform().origin + p_pos), true);
}

void Box3DPhysicsServer::body_add_torque(RID p_body, const Vector3 &p_torque) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyTorque(body->id, b3_vec(p_torque), true);
}

void Box3DPhysicsServer::body_apply_central_impulse(RID p_body, const Vector3 &p_impulse) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyLinearImpulseToCenter(body->id, b3_vec(p_impulse), true);
}

void Box3DPhysicsServer::body_apply_impulse(RID p_body, const Vector3 &p_pos, const Vector3 &p_impulse) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyLinearImpulse(body->id, b3_vec(p_impulse), b3_pos(body->get_transform().origin + p_pos), true);
}

void Box3DPhysicsServer::body_apply_torque_impulse(RID p_body, const Vector3 &p_impulse) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	ERR_FAIL_COND(!body->in_world());
	b3Body_ApplyAngularImpulse(body->id, b3_vec(p_impulse), true);
}

void Box3DPhysicsServer::body_set_axis_velocity(RID p_body, const Vector3 &p_axis_velocity) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	Vector3 axis = p_axis_velocity.normalized();
	Vector3 v = body->get_linear_velocity();
	v -= axis * axis.dot(v);
	v += p_axis_velocity;
	body_set_state(p_body, BODY_STATE_LINEAR_VELOCITY, v);
}

void Box3DPhysicsServer::body_set_axis_lock(RID p_body, PhysicsServer::BodyAxis p_axis, bool p_lock) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	if (p_lock) {
		body->locked_axes |= p_axis;
	} else {
		body->locked_axes &= ~(uint32_t)p_axis;
	}
	body->apply_motion_locks();
}

bool Box3DPhysicsServer::body_is_axis_locked(RID p_body, PhysicsServer::BodyAxis p_axis) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, false);
	return body->locked_axes & p_axis;
}

void Box3DPhysicsServer::body_add_collision_exception(RID p_body, RID p_body_b) {
	// Needs Box3D group indices or a custom filter callback. M4.
}

void Box3DPhysicsServer::body_remove_collision_exception(RID p_body, RID p_body_b) {
}

void Box3DPhysicsServer::body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
}

void Box3DPhysicsServer::body_set_max_contacts_reported(RID p_body, int p_contacts) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->max_contacts_reported = p_contacts;
}

int Box3DPhysicsServer::body_get_max_contacts_reported(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, 0);
	return body->max_contacts_reported;
}

void Box3DPhysicsServer::body_set_contacts_reported_depth_threshold(RID p_body, float p_threshold) {
}

float Box3DPhysicsServer::body_get_contacts_reported_depth_threshold(RID p_body) const {
	return 0;
}

void Box3DPhysicsServer::body_set_omit_force_integration(RID p_body, bool p_omit) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->omit_force_integration = p_omit;
}

bool Box3DPhysicsServer::body_is_omitting_force_integration(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, false);
	return body->omit_force_integration;
}

void Box3DPhysicsServer::body_set_force_integration_callback(RID p_body, Object *p_receiver, const StringName &p_method, const Variant &p_udata) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->fi_callback_id = p_receiver ? p_receiver->get_instance_id() : 0;
	body->fi_callback_method = p_method;
	body->fi_callback_udata = p_udata;
}

void Box3DPhysicsServer::body_set_ray_pickable(RID p_body, bool p_enable) {
	GET_OR_FAIL(Box3DBody, body, body_owner, p_body);
	body->ray_pickable = p_enable;
}

bool Box3DPhysicsServer::body_is_ray_pickable(RID p_body) const {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, false);
	return body->ray_pickable;
}

PhysicsDirectBodyState *Box3DPhysicsServer::body_get_direct_state(RID p_body) {
	GET_OR_FAIL_V(Box3DBody, body, body_owner, p_body, nullptr);
	Box3DDirectBodyState *state = Box3DDirectBodyState::get_singleton();
	state->body = body;
	state->delta = body->space ? body->space->last_step : 0;
	return state;
}

bool Box3DPhysicsServer::body_test_motion(RID p_body, const Transform &p_from, const Vector3 &p_motion, bool p_infinite_inertia, MotionResult *r_result, bool p_exclude_raycast_shapes, const Set<RID> &p_exclude) {
	// M3.
	if (r_result) {
		r_result->motion = p_motion;
		r_result->remainder = Vector3();
	}
	return false;
}

int Box3DPhysicsServer::body_test_ray_separation(RID p_body, const Transform &p_transform, bool p_infinite_inertia, Vector3 &r_recover_motion, SeparationResult *r_results, int p_result_max, float p_margin) {
	// M3.
	r_recover_motion = Vector3();
	return 0;
}

RID Box3DPhysicsServer::soft_body_create(bool p_init_sleeping) {
	return RID();
}

void Box3DPhysicsServer::soft_body_update_visual_server(RID p_body, class SoftBodyVisualServerHandler *p_visual_server_handler) {
}

void Box3DPhysicsServer::soft_body_set_space(RID p_body, RID p_space) {
}

RID Box3DPhysicsServer::soft_body_get_space(RID p_body) const {
	return RID();
}

void Box3DPhysicsServer::soft_body_set_mesh(RID p_body, const REF &p_mesh) {
}

void Box3DPhysicsServer::soft_body_set_collision_layer(RID p_body, uint32_t p_layer) {
}

uint32_t Box3DPhysicsServer::soft_body_get_collision_layer(RID p_body) const {
	return uint32_t();
}

void Box3DPhysicsServer::soft_body_set_collision_mask(RID p_body, uint32_t p_mask) {
}

uint32_t Box3DPhysicsServer::soft_body_get_collision_mask(RID p_body) const {
	return uint32_t();
}

void Box3DPhysicsServer::soft_body_add_collision_exception(RID p_body, RID p_body_b) {
}

void Box3DPhysicsServer::soft_body_remove_collision_exception(RID p_body, RID p_body_b) {
}

void Box3DPhysicsServer::soft_body_get_collision_exceptions(RID p_body, List<RID> *p_exceptions) {
}

void Box3DPhysicsServer::soft_body_set_state(RID p_body, PhysicsServer::BodyState p_state, const Variant &p_variant) {
}

Variant Box3DPhysicsServer::soft_body_get_state(RID p_body, PhysicsServer::BodyState p_state) const {
	return Variant();
}

void Box3DPhysicsServer::soft_body_set_transform(RID p_body, const Transform &p_transform) {
}

Vector3 Box3DPhysicsServer::soft_body_get_vertex_position(RID p_body, int vertex_index) const {
	return Vector3();
}

void Box3DPhysicsServer::soft_body_set_ray_pickable(RID p_body, bool p_enable) {
}

bool Box3DPhysicsServer::soft_body_is_ray_pickable(RID p_body) const {
	return bool();
}

void Box3DPhysicsServer::soft_body_set_simulation_precision(RID p_body, int p_simulation_precision) {
}

int Box3DPhysicsServer::soft_body_get_simulation_precision(RID p_body) {
	return int();
}

void Box3DPhysicsServer::soft_body_set_total_mass(RID p_body, real_t p_total_mass) {
}

real_t Box3DPhysicsServer::soft_body_get_total_mass(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_linear_stiffness(RID p_body, real_t p_stiffness) {
}

real_t Box3DPhysicsServer::soft_body_get_linear_stiffness(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_areaAngular_stiffness(RID p_body, real_t p_stiffness) {
}

real_t Box3DPhysicsServer::soft_body_get_areaAngular_stiffness(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_volume_stiffness(RID p_body, real_t p_stiffness) {
}

real_t Box3DPhysicsServer::soft_body_get_volume_stiffness(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_pressure_coefficient(RID p_body, real_t p_pressure_coefficient) {
}

real_t Box3DPhysicsServer::soft_body_get_pressure_coefficient(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_pose_matching_coefficient(RID p_body, real_t p_pose_matching_coefficient) {
}

real_t Box3DPhysicsServer::soft_body_get_pose_matching_coefficient(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_damping_coefficient(RID p_body, real_t p_damping_coefficient) {
}

real_t Box3DPhysicsServer::soft_body_get_damping_coefficient(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_set_drag_coefficient(RID p_body, real_t p_drag_coefficient) {
}

real_t Box3DPhysicsServer::soft_body_get_drag_coefficient(RID p_body) {
	return real_t();
}

void Box3DPhysicsServer::soft_body_move_point(RID p_body, int p_point_index, const Vector3 &p_global_position) {
}

Vector3 Box3DPhysicsServer::soft_body_get_point_global_position(RID p_body, int p_point_index) {
	return Vector3();
}

Vector3 Box3DPhysicsServer::soft_body_get_point_offset(RID p_body, int p_point_index) const {
	return Vector3();
}

void Box3DPhysicsServer::soft_body_remove_all_pinned_points(RID p_body) {
}

void Box3DPhysicsServer::soft_body_pin_point(RID p_body, int p_point_index, bool p_pin) {
}

bool Box3DPhysicsServer::soft_body_is_point_pinned(RID p_body, int p_point_index) {
	return bool();
}

PhysicsServer::JointType Box3DPhysicsServer::joint_get_type(RID p_joint) const {
	return PhysicsServer::JointType();
}

void Box3DPhysicsServer::joint_set_solver_priority(RID p_joint, int p_priority) {
}

int Box3DPhysicsServer::joint_get_solver_priority(RID p_joint) const {
	return int();
}

void Box3DPhysicsServer::joint_disable_collisions_between_bodies(RID p_joint, const bool p_disable) {
}

bool Box3DPhysicsServer::joint_is_disabled_collisions_between_bodies(RID p_joint) const {
	return bool();
}

RID Box3DPhysicsServer::joint_create_pin(RID p_body_A, const Vector3 &p_local_A, RID p_body_B, const Vector3 &p_local_B) {
	return RID();
}

void Box3DPhysicsServer::pin_joint_set_param(RID p_joint, PhysicsServer::PinJointParam p_param, float p_value) {
}

float Box3DPhysicsServer::pin_joint_get_param(RID p_joint, PhysicsServer::PinJointParam p_param) const {
	return float();
}

void Box3DPhysicsServer::pin_joint_set_local_a(RID p_joint, const Vector3 &p_A) {
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_a(RID p_joint) const {
	return Vector3();
}

void Box3DPhysicsServer::pin_joint_set_local_b(RID p_joint, const Vector3 &p_B) {
}

Vector3 Box3DPhysicsServer::pin_joint_get_local_b(RID p_joint) const {
	return Vector3();
}

RID Box3DPhysicsServer::joint_create_hinge(RID p_body_A, const Transform &p_hinge_A, RID p_body_B, const Transform &p_hinge_B) {
	return RID();
}

RID Box3DPhysicsServer::joint_create_hinge_simple(RID p_body_A, const Vector3 &p_pivot_A, const Vector3 &p_axis_A, RID p_body_B, const Vector3 &p_pivot_B, const Vector3 &p_axis_B) {
	return RID();
}

void Box3DPhysicsServer::hinge_joint_set_param(RID p_joint, PhysicsServer::HingeJointParam p_param, float p_value) {
}

float Box3DPhysicsServer::hinge_joint_get_param(RID p_joint, PhysicsServer::HingeJointParam p_param) const {
	return float();
}

void Box3DPhysicsServer::hinge_joint_set_flag(RID p_joint, PhysicsServer::HingeJointFlag p_flag, bool p_value) {
}

bool Box3DPhysicsServer::hinge_joint_get_flag(RID p_joint, PhysicsServer::HingeJointFlag p_flag) const {
	return bool();
}

RID Box3DPhysicsServer::joint_create_slider(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return RID();
}

void Box3DPhysicsServer::slider_joint_set_param(RID p_joint, PhysicsServer::SliderJointParam p_param, float p_value) {
}

float Box3DPhysicsServer::slider_joint_get_param(RID p_joint, PhysicsServer::SliderJointParam p_param) const {
	return float();
}

RID Box3DPhysicsServer::joint_create_cone_twist(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return RID();
}

void Box3DPhysicsServer::cone_twist_joint_set_param(RID p_joint, PhysicsServer::ConeTwistJointParam p_param, float p_value) {
}

float Box3DPhysicsServer::cone_twist_joint_get_param(RID p_joint, PhysicsServer::ConeTwistJointParam p_param) const {
	return float();
}

RID Box3DPhysicsServer::joint_create_generic_6dof(RID p_body_A, const Transform &p_local_frame_A, RID p_body_B, const Transform &p_local_frame_B) {
	return RID();
}

void Box3DPhysicsServer::generic_6dof_joint_set_param(RID p_joint, Vector3::Axis, PhysicsServer::G6DOFJointAxisParam p_param, float p_value) {
}

float Box3DPhysicsServer::generic_6dof_joint_get_param(RID p_joint, Vector3::Axis, PhysicsServer::G6DOFJointAxisParam p_param) {
	return float();
}

void Box3DPhysicsServer::generic_6dof_joint_set_flag(RID p_joint, Vector3::Axis, PhysicsServer::G6DOFJointAxisFlag p_flag, bool p_enable) {
}

bool Box3DPhysicsServer::generic_6dof_joint_get_flag(RID p_joint, Vector3::Axis, PhysicsServer::G6DOFJointAxisFlag p_flag) {
	return bool();
}

void Box3DPhysicsServer::free(RID p_rid) {
	// getornull() would both complain and, in release builds, hand back a
	// wrongly typed pointer for a RID owned by another owner. owns() is the test.
	if (shape_owner.owns(p_rid)) {
		Box3DShape *shape = shape_owner.get(p_rid);
		while (Set<Box3DBody *>::Element *E = shape->owners.front()) {
			Box3DBody *body = E->get();
			for (int i = body->shapes.size() - 1; i >= 0; i--) {
				if (body->shapes[i].shape == shape) {
					body->shapes.remove(i);
				}
			}
			shape->owners.erase(body);
			body->rebuild_shapes();
		}
		shape_owner.free(p_rid);
		memdelete(shape);
		return;
	}

	if (body_owner.owns(p_rid)) {
		Box3DBody *body = body_owner.get(p_rid);
		body->set_space(nullptr);
		for (int i = 0; i < body->shapes.size(); i++) {
			if (body->shapes[i].shape) {
				body->shapes[i].shape->owners.erase(body);
			}
		}
		body_owner.free(p_rid);
		memdelete(body);
		return;
	}

	if (area_owner.owns(p_rid)) {
		Box3DArea *area = area_owner.get(p_rid);
		area_owner.free(p_rid);
		memdelete(area);
		return;
	}

	if (space_owner.owns(p_rid)) {
		Box3DSpace *space = space_owner.get(p_rid);
		active_spaces.erase(space);
		space_owner.free(p_rid);
		memdelete(space);
		return;
	}

	ERR_PRINT("Box3D: attempted to free an invalid RID.");
}

void Box3DPhysicsServer::set_active(bool p_active) {
	active = p_active;
}

void Box3DPhysicsServer::init() {
}

void Box3DPhysicsServer::step(float p_step) {
	if (!active) {
		return;
	}
	for (int i = 0; i < active_spaces.size(); i++) {
		active_spaces[i]->step(p_step);
	}
}

void Box3DPhysicsServer::flush_queries() {
	// Godot 3 steps after the scene tree iteration, so there is nothing to flush
	// before it: Box3DSpace::step() both simulates and reports.
}

void Box3DPhysicsServer::finish() {
	Box3DDirectBodyState::free_singleton();
}

bool Box3DPhysicsServer::is_flushing_queries() const {
	return false;
}

void Box3DPhysicsServer::set_collision_iterations(int p_iterations) {
}

int Box3DPhysicsServer::get_process_info(PhysicsServer::ProcessInfo p_info) {
	return 0;
}

Box3DPhysicsServer::Box3DPhysicsServer() {
}

Box3DPhysicsServer::~Box3DPhysicsServer() {
}
