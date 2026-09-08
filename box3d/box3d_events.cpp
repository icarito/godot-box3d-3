/**************************************************************************/
/*  box3d_events.cpp                                                      */
/*  Per-step bookkeeping: contact reporting, area monitoring via Box3D    */
/*  sensor events, and Godot space-override emulation.                    */
/**************************************************************************/

#include "box3d_objects.h"

#include "box3d_proxies.h"

#include "core/object.h"

/* Contact reporting */

static void collect_debug_contacts(Box3DSpace *p_space) {
	p_space->debug_contacts.clear();
	if (p_space->debug_contact_max <= 0) {
		return;
	}
	for (List<Box3DBody *>::Element *E = p_space->bodies.front(); E; E = E->next()) {
		Box3DBody *body = E->get();
		if (!body->in_world() || body->contacts.empty()) {
			continue;
		}
		for (int i = 0; i < body->contacts.size(); i++) {
			if (p_space->debug_contacts.size() >= p_space->debug_contact_max) {
				return;
			}
			p_space->debug_contacts.push_back(body->contacts[i].world_position);
		}
	}
}

/* Area monitoring through sensor events */

struct SensorLookup {
	Box3DBody *body = nullptr;
	Box3DArea *area = nullptr;
	int shape_index = 0;
};

static SensorLookup sensor_lookup(b3ShapeId p_shape) {
	SensorLookup out;
	b3BodyId bid = b3Shape_GetBody(p_shape);
	if (B3_IS_NULL(bid)) {
		return out;
	}
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(bid);
	if (!entity) {
		return out;
	}
	out.shape_index = (int)(intptr_t)b3Shape_GetUserData(p_shape);
	if (entity->is_area) {
		out.area = (Box3DArea *)entity;
	} else {
		out.body = (Box3DBody *)entity;
	}
	return out;
}

static void pump_sensor_events(Box3DSpace *p_space) {
	b3SensorEvents events = b3World_GetSensorEvents(p_space->world);

	// The event arrays live in world memory and stay valid until the next
	// step; monitoring callbacks may add or free bodies, so copy first and
	// dispatch from the copies.
	LocalVector<b3SensorBeginTouchEvent> begins;
	begins.resize(events.beginCount);
	for (int i = 0; i < events.beginCount; i++) {
		begins[i] = events.beginEvents[i];
	}
	LocalVector<b3SensorEndTouchEvent> ends;
	ends.resize(events.endCount);
	for (int i = 0; i < events.endCount; i++) {
		ends[i] = events.endEvents[i];
	}

	for (int i = 0; i < (int)begins.size(); i++) {
		const b3SensorBeginTouchEvent &e = begins[i];
		SensorLookup sensor = sensor_lookup(e.sensorShapeId);
		SensorLookup visitor = sensor_lookup(e.visitorShapeId);
		if (!sensor.area) {
			continue;
		}
		if (visitor.body) {
			sensor.area->report_body(PhysicsServer::AREA_BODY_ADDED, visitor.body,
					visitor.shape_index, sensor.shape_index);
		} else if (visitor.area && visitor.area != sensor.area && visitor.area->monitorable) {
			sensor.area->report_area(PhysicsServer::AREA_BODY_ADDED, visitor.area,
					visitor.shape_index, sensor.shape_index);
		}
	}

	for (int i = 0; i < (int)ends.size(); i++) {
		const b3SensorEndTouchEvent &e = ends[i];
		// Either shape may have been destroyed during the step or by a
		// callback that already ran.
		if (!b3Shape_IsValid(e.sensorShapeId) || !b3Shape_IsValid(e.visitorShapeId)) {
			continue;
		}
		SensorLookup sensor = sensor_lookup(e.sensorShapeId);
		SensorLookup visitor = sensor_lookup(e.visitorShapeId);
		if (!sensor.area) {
			continue;
		}
		if (visitor.body) {
			sensor.area->report_body(PhysicsServer::AREA_BODY_REMOVED, visitor.body,
					visitor.shape_index, sensor.shape_index);
		} else if (visitor.area && visitor.area != sensor.area && visitor.area->monitorable) {
			sensor.area->report_area(PhysicsServer::AREA_BODY_REMOVED, visitor.area,
					visitor.shape_index, sensor.shape_index);
		}
	}
}

/* Space overrides */

static Vector3 area_gravity(Box3DArea *p_area, const Vector3 &p_body_origin) {
	const Map<PhysicsServer::AreaParameter, Variant>::Element *mag = p_area->params.find(PhysicsServer::AREA_PARAM_GRAVITY);
	const Map<PhysicsServer::AreaParameter, Variant>::Element *vec = p_area->params.find(PhysicsServer::AREA_PARAM_GRAVITY_VECTOR);
	const Map<PhysicsServer::AreaParameter, Variant>::Element *point = p_area->params.find(PhysicsServer::AREA_PARAM_GRAVITY_IS_POINT);
	const Map<PhysicsServer::AreaParameter, Variant>::Element *attenuation = p_area->params.find(PhysicsServer::AREA_PARAM_GRAVITY_POINT_ATTENUATION);
	real_t magnitude = mag ? (real_t)mag->get() : 9.8;
	Vector3 direction = vec ? (Vector3)vec->get() : Vector3(0, -1, 0);

	if (point && ((bool)point->get())) {
		// The gravity vector names a local point; pull the body toward it.
		Vector3 target = p_area->transform.xform(direction);
		Vector3 delta = target - p_body_origin;
		real_t distance = delta.length();
		if (distance < CMP_EPSILON) {
			return Vector3();
		}
		Vector3 gravity = delta / distance;
		real_t distance_scale = attenuation ? (real_t)attenuation->get() : 1.0;
		if (distance_scale > 0) {
			gravity *= magnitude / Math::pow(distance * distance_scale + 1.0, 2.0);
		} else {
			gravity *= magnitude;
		}
		return gravity;
	}

	if (direction.length_squared() > 0) {
		return direction.normalized() * magnitude;
	}
	return Vector3();
}

// Area/shape overlap test between an override area and a body. One exact
// b3Body_OverlapShape call per area shape covers every shape on the body.
static bool area_overlaps_body(Box3DArea *p_area, Box3DBody *p_body) {
	for (int s = 0; s < p_area->shapes.size(); s++) {
		const Box3DArea::ShapeInstance &si = p_area->shapes[s];
		if (si.disabled || !si.shape || B3_IS_NULL(si.id)) {
			continue;
		}
		Box3DWorldProxy area_proxy;
		if (!box3d_build_b3_proxy(si.id, area_proxy)) {
			continue;
		}
		b3QueryFilter filter = b3DefaultQueryFilter();
		filter.categoryBits = p_area->collision_layer;
		filter.maskBits = p_area->collision_mask;
		if (b3Body_OverlapShape(p_body->id, b3_pos(Vector3()), &area_proxy.proxy, filter,
					b3Body_GetTransform(p_body->id))) {
			return true;
		}
	}
	return false;
}

void Box3DSpace::apply_area_overrides() {
	if (B3_IS_NULL(world)) {
		return;
	}

	for (List<Box3DBody *>::Element *E = bodies.front(); E; E = E->next()) {
		Box3DBody *body = E->get();
		if (body->mode != PhysicsServer::BODY_MODE_RIGID && body->mode != PhysicsServer::BODY_MODE_CHARACTER) {
			continue;
		}

		// Defaults, Bullet-style: the totals the body would get with no areas.
		Vector3 total_gravity = gravity_vector * gravity_magnitude * body->gravity_scale;
		real_t total_linear_damp = body->linear_damp >= 0 ? body->linear_damp : area_linear_damp;
		real_t total_angular_damp = body->angular_damp >= 0 ? body->angular_damp : area_angular_damp;

		bool stopped = false;
		for (List<Box3DArea *>::Element *A = areas.front(); A && !stopped; A = A->next()) {
			Box3DArea *area = A->get();
			PhysicsServer::AreaSpaceOverrideMode mode = area->override_mode;
			if (mode == PhysicsServer::AREA_SPACE_OVERRIDE_DISABLED || !area->in_world()) {
				continue;
			}
			if (!area_overlaps_body(area, body)) {
				continue;
			}

			Vector3 support_gravity = area_gravity(area, body->get_transform().origin);
			const Map<PhysicsServer::AreaParameter, Variant>::Element *ld =
					area->params.find(PhysicsServer::AREA_PARAM_LINEAR_DAMP);
			const Map<PhysicsServer::AreaParameter, Variant>::Element *ad =
					area->params.find(PhysicsServer::AREA_PARAM_ANGULAR_DAMP);
			real_t area_linear_damp = ld ? (real_t)ld->get() : 0.1;
			real_t area_angular_damp = ad ? (real_t)ad->get() : 0.1;

			switch (mode) {
				case PhysicsServer::AREA_SPACE_OVERRIDE_COMBINE:
					total_gravity += support_gravity;
					total_linear_damp += area_linear_damp;
					total_angular_damp += area_angular_damp;
					break;
				case PhysicsServer::AREA_SPACE_OVERRIDE_COMBINE_REPLACE:
					total_gravity += support_gravity;
					total_linear_damp += area_linear_damp;
					total_angular_damp += area_angular_damp;
					stopped = true;
					break;
				case PhysicsServer::AREA_SPACE_OVERRIDE_REPLACE:
					total_gravity = support_gravity;
					total_linear_damp = area_linear_damp;
					total_angular_damp = area_angular_damp;
					stopped = true;
					break;
				case PhysicsServer::AREA_SPACE_OVERRIDE_REPLACE_COMBINE:
					total_gravity = support_gravity;
					total_linear_damp = area_linear_damp;
					total_angular_damp = area_angular_damp;
					break;
				default:
					break;
			}
		}

		body->total_gravity = total_gravity;
		body->total_linear_damp = total_linear_damp;
		body->total_angular_damp = total_angular_damp;

		if (!body->in_world()) {
			continue;
		}

		Vector3 default_gravity = gravity_vector * gravity_magnitude * body->gravity_scale;
		real_t default_linear_damp = body->linear_damp >= 0 ? body->linear_damp : area_linear_damp;
		real_t default_angular_damp = body->angular_damp >= 0 ? body->angular_damp : area_angular_damp;

		const bool overridden = total_gravity != default_gravity ||
				total_linear_damp != default_linear_damp ||
				total_angular_damp != default_angular_damp;

		if (!overridden) {
			if (body->gravity_from_area) {
				// Left the override area: restore the plain Godot gravity path.
				body->gravity_from_area = false;
				b3Body_SetGravityScale(body->id, body->gravity_scale);
				body->apply_damping();
			}
			continue;
		}

		body->gravity_from_area = true;
		b3Body_SetGravityScale(body->id, 0.0f);

		// Box3D applies internal gravity every sub-step but consumes applied
		// forces once, so scale the override force to land the same delta-v.
		const float substeps = 4.0f;
		float mass = MAX((float)body->mass, CMP_EPSILON);
		b3Body_ApplyForceToCenter(body->id, b3_vec(total_gravity * mass * substeps), false);

		body->apply_damping(total_linear_damp, total_angular_damp);
	}
}

/* Event pump */

void Box3DSpace::pump_events(real_t p_delta) {
	(void)p_delta;

	for (List<Box3DBody *>::Element *E = bodies.front(); E; E = E->next()) {
		E->get()->collect_contacts();
	}
	collect_debug_contacts(this);
	pump_sensor_events(this);
}
