/**************************************************************************/
/*  box3d_motion.cpp                                                      */
/*  body_test_motion(), the query behind move_and_collide/move_and_slide. */
/**************************************************************************/

#include "box3d_objects.h"

// Three phases, the same shape as the Bullet backend's:
//   1. push the body out of anything it already overlaps,
//   2. sweep its shapes along the motion and clamp to the first hit,
//   3. look for a resting contact at the end position, which is what tells
//      Godot there was a collision and gives move_and_slide its normal.
//
// Everything runs in world space with a zero query origin. That is exact for a
// float build; a BOX3D_DOUBLE_PRECISION large world would want a local origin.

namespace {

// Recovery pushes the body this far out of its neighbours. It has to stay above
// the distance where b3ShapeCast reports "initial overlap", because that result
// carries no normal and would clamp every motion to zero.
const float MIN_MARGIN = 2.0f * B3_LINEAR_SLOP;
const int RECOVER_CYCLES = 4;
const float RECOVER_SCALE = 0.4f;

struct WorldProxy {
	b3Vec3 points[B3_MAX_SHAPE_CAST_POINTS];
	b3ShapeProxy proxy;
};

Box3DBody *body_of(b3ShapeId p_shape) {
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(b3Shape_GetBody(p_shape));
	return (entity && !entity->is_area) ? (Box3DBody *)entity : nullptr;
}

int shape_index_of(b3ShapeId p_shape) {
	return (int)(intptr_t)b3Shape_GetUserData(p_shape);
}

// A Godot shape definition, as a world space point cloud.
bool build_godot_proxy(const Box3DShape *p_shape, const Transform &p_xform, WorldProxy &r_proxy) {
	switch (p_shape->type) {
		case PhysicsServer::SHAPE_BOX: {
			Vector3 he = p_shape->data;
			for (int i = 0; i < 8; i++) {
				Vector3 corner((i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z);
				r_proxy.points[i] = b3_vec(p_xform.xform(corner));
			}
			r_proxy.proxy.points = r_proxy.points;
			r_proxy.proxy.count = 8;
			r_proxy.proxy.radius = 0.0f;
			return true;
		}
		default:
			return false;
	}
}

// A live Box3D shape, as a world space point cloud. Meshes and height fields
// have no point cloud form; they arrive with M4.
bool build_b3_proxy(b3ShapeId p_shape, WorldProxy &r_proxy) {
	b3WorldTransform wt = b3Body_GetTransform(b3Shape_GetBody(p_shape));
	Transform xform = g_transform(wt.p, wt.q);

	switch (b3Shape_GetType(p_shape)) {
		case b3_sphereShape: {
			b3Sphere sphere = b3Shape_GetSphere(p_shape);
			r_proxy.points[0] = b3_vec(xform.xform(g_vec(sphere.center)));
			r_proxy.proxy.count = 1;
			r_proxy.proxy.radius = sphere.radius;
		} break;
		case b3_capsuleShape: {
			b3Capsule capsule = b3Shape_GetCapsule(p_shape);
			r_proxy.points[0] = b3_vec(xform.xform(g_vec(capsule.center1)));
			r_proxy.points[1] = b3_vec(xform.xform(g_vec(capsule.center2)));
			r_proxy.proxy.count = 2;
			r_proxy.proxy.radius = capsule.radius;
		} break;
		case b3_hullShape: {
			const b3HullData *hull = b3Shape_GetHull(p_shape);
			const b3Vec3 *points = hull ? b3GetHullPoints(hull) : nullptr;
			if (!points) {
				return false;
			}
			int count = MIN(hull->vertexCount, (int)B3_MAX_SHAPE_CAST_POINTS);
			for (int i = 0; i < count; i++) {
				r_proxy.points[i] = b3_vec(xform.xform(g_vec(points[i])));
			}
			r_proxy.proxy.count = count;
			r_proxy.proxy.radius = 0.0f;
		} break;
		default:
			return false;
	}

	r_proxy.proxy.points = r_proxy.points;
	return true;
}

b3AABB proxy_aabb(const WorldProxy &p_proxy, float p_inflate) {
	Vector3 lower = g_vec(p_proxy.points[0]);
	Vector3 upper = lower;
	for (int i = 1; i < p_proxy.proxy.count; i++) {
		Vector3 p = g_vec(p_proxy.points[i]);
		lower.x = MIN(lower.x, p.x);
		lower.y = MIN(lower.y, p.y);
		lower.z = MIN(lower.z, p.z);
		upper.x = MAX(upper.x, p.x);
		upper.y = MAX(upper.y, p.y);
		upper.z = MAX(upper.z, p.z);
	}
	float pad = p_proxy.proxy.radius + p_inflate;
	b3AABB aabb;
	aabb.lowerBound = b3_vec(lower - Vector3(pad, pad, pad));
	aabb.upperBound = b3_vec(upper + Vector3(pad, pad, pad));
	return aabb;
}

struct Candidates {
	Vector<b3ShapeId> shapes;
	b3BodyId skip;
	const Set<RID> *exclude;
};

bool accept(const Candidates &p_ctx, b3ShapeId p_shape) {
	b3BodyId owner = b3Shape_GetBody(p_shape);
	if (B3_ID_EQUALS(owner, p_ctx.skip)) {
		return false;
	}
	// Area sensors ride on their own kinematic proxy bodies; they never block
	// motion, just like the Bullet backend's ghost objects.
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(owner);
	if (entity && entity->is_area) {
		return false;
	}
	if (p_ctx.exclude && !p_ctx.exclude->empty()) {
		Box3DBody *other = (Box3DBody *)entity;
		if (other && p_ctx.exclude->has(other->self)) {
			return false;
		}
	}
	return true;
}

bool collect_candidate(b3ShapeId p_shape, void *p_context) {
	Candidates *ctx = (Candidates *)p_context;
	if (accept(*ctx, p_shape)) {
		ctx->shapes.push_back(p_shape);
	}
	return true; // keep searching
}

struct Contact {
	Vector3 normal; // Points from the other shape back toward our body.
	Vector3 point;
	real_t depth = 0.0;
	b3ShapeId shape = b3_nullShapeId;
	int local_shape = 0;
	bool valid = false;
};

// Closest contact between one of our shapes and everything within p_margin.
// Returns the deepest one, which is what Godot reports as "the" collision.
bool query_contacts(Box3DBody *p_body, const Transform &p_xform, real_t p_margin, const Set<RID> &p_exclude,
		Contact *r_deepest, Vector3 *r_recover) {
	bool any = false;
	if (r_recover) {
		*r_recover = Vector3();
	}

	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = p_body->collision_layer;
	filter.maskBits = p_body->collision_mask;

	for (int i = 0; i < p_body->shapes.size(); i++) {
		const Box3DBody::ShapeInstance &si = p_body->shapes[i];
		if (si.disabled || !si.shape) {
			continue;
		}

		WorldProxy ours;
		if (!build_godot_proxy(si.shape, p_xform * si.xform, ours)) {
			continue;
		}

		Candidates candidates;
		candidates.skip = p_body->id;
		candidates.exclude = &p_exclude;
		b3World_OverlapAABB(p_body->space->world, proxy_aabb(ours, p_margin), filter, collect_candidate, &candidates);

		for (int c = 0; c < candidates.shapes.size(); c++) {
			WorldProxy theirs;
			if (!build_b3_proxy(candidates.shapes[c], theirs)) {
				continue;
			}

			// Both clouds are already in world space, so B's pose in A's frame is identity.
			b3DistanceInput input = { 0 };
			input.proxyA = ours.proxy;
			input.proxyB = theirs.proxy;
			input.transform = b3Transform_identity;
			input.useRadii = true;

			b3SimplexCache cache = { 0 };
			b3DistanceOutput out = b3ShapeDistance(&input, &cache, nullptr, 0);

			// GJK cannot name a direction once the shapes interpenetrate.
			// ponytail: deep overlap is left to the caller to avoid; add EPA only
			// if spawning inside geometry turns out to matter.
			if (out.distance <= 0.0f || out.distance >= p_margin) {
				continue;
			}

			Vector3 normal = -g_vec(out.normal); // A to B, so pushing us out runs the other way.
			real_t depth = p_margin - out.distance;
			any = true;

			if (r_recover) {
				*r_recover += normal * depth * RECOVER_SCALE;
			}
			if (r_deepest && depth > r_deepest->depth) {
				r_deepest->normal = normal;
				r_deepest->point = g_vec(out.pointB);
				r_deepest->depth = depth;
				r_deepest->shape = candidates.shapes[c];
				r_deepest->local_shape = i;
				r_deepest->valid = true;
			}
		}
	}

	return any;
}

struct CastHit {
	float fraction = 1.0f;
	bool hit = false;
	b3BodyId skip;
	const Set<RID> *exclude;
};

float cast_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t, int, int, void *p_context) {
	CastHit *ctx = (CastHit *)p_context;

	Candidates probe;
	probe.skip = ctx->skip;
	probe.exclude = ctx->exclude;
	if (!accept(probe, p_shape)) {
		return -1.0f; // ignore this shape and keep going
	}

	if (p_fraction < ctx->fraction) {
		ctx->fraction = p_fraction;
		ctx->hit = true;
	}
	return p_fraction; // clip the sweep here
}

} // namespace

bool Box3DSpace::test_motion(Box3DBody *p_body, const Transform &p_from, const Vector3 &p_motion, real_t p_margin,
		PhysicsServer::MotionResult *r_result, const Set<RID> &p_exclude) {
	ERR_FAIL_COND_V(!p_body || !p_body->in_world(), false);

	const real_t margin = MAX(p_margin, (real_t)MIN_MARGIN);

	// Box3D has no body scale, so the query runs on the rotation only.
	Transform xform = p_from;
	xform.basis.orthonormalize();

	// Phase 1: depenetrate.
	Vector3 recovered;
	for (int i = 0; i < RECOVER_CYCLES; i++) {
		Vector3 step;
		if (!query_contacts(p_body, xform, margin, p_exclude, nullptr, &step)) {
			break;
		}
		xform.origin += step;
		recovered += step;
	}

	// Phase 2: sweep, clamping the motion at the first thing each shape hits.
	Vector3 motion = p_motion;
	const real_t total_length = p_motion.length();
	real_t safe_fraction = 1.0;
	real_t unsafe_fraction = 1.0;

	if (total_length > CMP_EPSILON) {
		b3QueryFilter filter = b3DefaultQueryFilter();
		filter.categoryBits = p_body->collision_layer;
		filter.maskBits = p_body->collision_mask;

		for (int i = 0; i < p_body->shapes.size(); i++) {
			const Box3DBody::ShapeInstance &si = p_body->shapes[i];
			if (si.disabled || !si.shape) {
				continue;
			}
			if (motion.length_squared() <= CMP_EPSILON * CMP_EPSILON) {
				break;
			}

			WorldProxy ours;
			if (!build_godot_proxy(si.shape, xform * si.xform, ours)) {
				continue;
			}

			CastHit hit;
			hit.skip = p_body->id;
			hit.exclude = &p_exclude;
			b3World_CastShape(world, b3_pos(Vector3()), &ours.proxy, b3_vec(motion), filter, cast_callback, &hit);

			if (!hit.hit) {
				continue;
			}

			// Fractions are reported against the already clipped motion, so scale
			// them back onto the caller's motion before comparing shapes.
			real_t hit_fraction = hit.fraction * motion.length() / total_length;
			if (hit_fraction < unsafe_fraction) {
				unsafe_fraction = hit_fraction;
				safe_fraction = MAX(hit_fraction - margin / total_length, (real_t)0.0);
			}
			motion *= hit.fraction;
		}
	}

	xform.origin += motion;

	// Phase 3: what are we resting against now?
	Contact contact;
	const bool colliding = query_contacts(p_body, xform, margin, p_exclude, &contact, nullptr) && contact.valid;

	if (!r_result) {
		return colliding;
	}

	r_result->motion = recovered + motion;
	if (!colliding) {
		r_result->remainder = Vector3();
		return false;
	}

	r_result->remainder = p_motion - motion;
	r_result->collision_point = contact.point;
	r_result->collision_normal = contact.normal;
	r_result->collision_depth = contact.depth;
	r_result->collision_safe_fraction = safe_fraction;
	r_result->collision_unsafe_fraction = unsafe_fraction;
	r_result->collision_local_shape = contact.local_shape;
	r_result->collider_shape = shape_index_of(contact.shape);

	Box3DBody *other = body_of(contact.shape);
	if (other) {
		r_result->collider = other->self;
		r_result->collider_id = other->instance_id;
		b3BodyId other_id = b3Shape_GetBody(contact.shape);
		r_result->collider_velocity = g_vec(b3Body_GetWorldPointVelocity(other_id, b3_pos(contact.point)));
	}

	return true;
}
