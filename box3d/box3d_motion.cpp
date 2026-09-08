/**************************************************************************/
/*  box3d_motion.cpp                                                      */
/*  body_test_motion(), the query behind move_and_collide/move_and_slide. */
/**************************************************************************/

#include "box3d_objects.h"

#include "box3d_proxies.h"

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

Box3DBody *body_of(b3ShapeId p_shape) {
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(b3Shape_GetBody(p_shape));
	return (entity && !entity->is_area) ? (Box3DBody *)entity : nullptr;
}

int shape_index_of(b3ShapeId p_shape) {
	return (int)(intptr_t)b3Shape_GetUserData(p_shape);
}

// A ray shape takes part in sweeps only (and only when not excluded), never
// in the contact phases: it has no surface to rest on.
bool sweep_shape(const Box3DShape *p_shape) {
	return p_shape->type != PhysicsServer::SHAPE_RAY;
}

bool ray_shape(const Box3DShape *p_shape) {
	return p_shape->type == PhysicsServer::SHAPE_RAY;
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



// GJK reports zero distance and no direction once two shapes interpenetrate, so
// a body that ends up inside geometry could never be pushed out: the recovery
// found nothing, the sweep saw initial overlap and clamped the motion to zero,
// and the body froze in every direction at once. Shrinking our own cloud toward
// its centroid separates the pair again and hands back the axis; the real
// penetration is then the shrink minus whatever separation is left.
bool proxy_contact(const Box3DWorldProxy &p_ours, const b3ShapeProxy &p_theirs, real_t p_margin,
		Vector3 &r_normal, Vector3 &r_point, real_t &r_depth) {
	b3DistanceInput input = { 0 };
	input.proxyA = p_ours.proxy;
	input.proxyB = p_theirs;
	input.transform = b3Transform_identity;
	input.useRadii = true;

	b3SimplexCache cache = { 0 };
	b3DistanceOutput out = b3ShapeDistance(&input, &cache, nullptr, 0);

	if (out.distance > 0.0f) {
		if (out.distance >= p_margin) {
			return false;
		}
		r_normal = -g_vec(out.normal); // A to B, so pushing us out runs the other way
		r_point = g_vec(out.pointB);
		r_depth = p_margin - out.distance;
		return true;
	}

	// Interpenetrating. GJK gives no direction here, and shrinking our cloud
	// toward its centroid cannot help when we are entirely inside the other
	// shape, which is exactly the case that matters: a character waking up
	// inside a cryo pod. Projection needs neither. Score candidate axes and
	// keep the shallowest escape, which is what stops a body being flung out
	// the long way: that pod is 2.4 m tall and its near wall 0.05 m away, and
	// pushing up left the character standing on the pod's invisible roof.
	//
	// ponytail: the candidates are the world axes, exact for the axis-aligned
	// boxes that level geometry is built from and an approximation elsewhere.
	// A true minimum translation needs both shapes' face normals plus their
	// edge cross products; add that if angled geometry starts trapping bodies.
	const Vector3 axes[3] = { Vector3(1, 0, 0), Vector3(0, 1, 0), Vector3(0, 0, 1) };

	real_t best_depth = 1e30;
	Vector3 best_normal;
	for (int a = 0; a < 3; a++) {
		const Vector3 axis = axes[a];

		real_t ours_min = 1e30, ours_max = -1e30;
		for (int i = 0; i < p_ours.proxy.count; i++) {
			const real_t d = axis.dot(g_vec(p_ours.points[i]));
			ours_min = MIN(ours_min, d);
			ours_max = MAX(ours_max, d);
		}
		ours_min -= p_ours.proxy.radius;
		ours_max += p_ours.proxy.radius;

		real_t theirs_min = 1e30, theirs_max = -1e30;
		for (int i = 0; i < p_theirs.count; i++) {
			const real_t d = axis.dot(g_vec(p_theirs.points[i]));
			theirs_min = MIN(theirs_min, d);
			theirs_max = MAX(theirs_max, d);
		}
		theirs_min -= p_theirs.radius;
		theirs_max += p_theirs.radius;

		// Both are positive while the pair overlaps on this axis: how far we
		// must travel along +axis, and along -axis, to clear them.
		const real_t push_positive = theirs_max - ours_min;
		const real_t push_negative = ours_max - theirs_min;
		if (push_positive <= 0.0 || push_negative <= 0.0) {
			return false; // a separating axis: they do not actually overlap
		}
		if (push_positive < best_depth) {
			best_depth = push_positive;
			best_normal = axis;
		}
		if (push_negative < best_depth) {
			best_depth = push_negative;
			best_normal = -axis;
		}
	}

	if (best_normal.length_squared() < CMP_EPSILON) {
		return false;
	}

	// Put the witness point on the face we are escaping through.
	Vector3 centroid;
	for (int i = 0; i < p_ours.proxy.count; i++) {
		centroid += g_vec(p_ours.points[i]);
	}
	centroid /= MAX(p_ours.proxy.count, 1);

	r_normal = best_normal;
	r_point = centroid - best_normal * (best_depth * 0.5);
	r_depth = best_depth + p_margin;
	return true;
}

// Meshes and height fields have no point-cloud form, so the contact phase used to
// skip them entirely: the body then rested on level geometry the sweep could see
// but the recovery could not, every cast started already touching, and the motion
// was clamped to zero in every direction. Their triangles ARE point clouds, so
// query the ones near the body and treat each as one more candidate shape.
struct TriangleContacts {
	const Box3DWorldProxy *ours = nullptr;
	Transform to_world;
	real_t margin = 0.0;
	b3ShapeId shape = b3_nullShapeId;
	int local_shape = 0;

	Vector3 *recover = nullptr;
	Contact *deepest = nullptr;
	bool any = false;
};

bool triangle_callback(b3Vec3 p_a, b3Vec3 p_b, b3Vec3 p_c, int, void *p_context) {
	TriangleContacts *ctx = (TriangleContacts *)p_context;

	Box3DWorldProxy tri;
	tri.points[0] = b3_vec(ctx->to_world.xform(g_vec(p_a)));
	tri.points[1] = b3_vec(ctx->to_world.xform(g_vec(p_b)));
	tri.points[2] = b3_vec(ctx->to_world.xform(g_vec(p_c)));
	tri.proxy.points = tri.points;
	tri.proxy.count = 3;
	tri.proxy.radius = 0.0f;

	Vector3 normal;
	Vector3 point;
	real_t depth = 0.0;
	if (!proxy_contact(*ctx->ours, tri.proxy, ctx->margin, normal, point, depth)) {
		return true; // keep walking the mesh
	}
	ctx->any = true;

	if (ctx->recover) {
		*ctx->recover += normal * depth * RECOVER_SCALE;
	}
	if (ctx->deepest && depth > ctx->deepest->depth) {
		ctx->deepest->normal = normal;
		ctx->deepest->point = point;
		ctx->deepest->depth = depth;
		ctx->deepest->shape = ctx->shape;
		ctx->deepest->local_shape = ctx->local_shape;
		ctx->deepest->valid = true;
	}
	return true;
}

// Vertical penetration probe for mesh and height field shapes.
struct MeshProbeContext {
	b3BodyId skip;
	const Set<RID> *exclude;
	uint32_t mask;
	Vector3 point;

	bool hit = false;
	float fraction = 1.0f;
	b3Pos point_hit;
	b3Vec3 normal;
	b3ShapeId shape = b3_nullShapeId;
};

float mesh_probe_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t, int, int, void *p_context) {
	MeshProbeContext *ctx = (MeshProbeContext *)p_context;
	b3BodyId owner = b3Shape_GetBody(p_shape);
	if (B3_ID_EQUALS(owner, ctx->skip)) {
		return -1.0f;
	}
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(owner);
	if (!entity || entity->is_area) {
		return -1.0f;
	}
	Box3DBody *other = (Box3DBody *)entity;
	if (ctx->exclude && !ctx->exclude->empty() && ctx->exclude->has(other->self)) {
		return -1.0f;
	}
	// The probe targets the queried shape's own layer mask.
	b3Filter shape_filter = b3Shape_GetFilter(p_shape);
	if (((shape_filter.categoryBits & ctx->mask) == 0) || ((shape_filter.maskBits & BOX3D_QUERY_BIT) == 0)) {
		return -1.0f;
	}

	if (p_fraction < ctx->fraction) {
		ctx->fraction = p_fraction;
		ctx->point_hit = p_point;
		ctx->normal = p_normal;
		ctx->shape = p_shape;
		ctx->hit = true;
	}
	return p_fraction;
}

// The world-space proxy, expressed as bounds in the queried shape's own frame.
b3AABB local_bounds(const Box3DWorldProxy &p_proxy, const Transform &p_to_local, float p_inflate) {	Vector3 lower = p_to_local.xform(g_vec(p_proxy.points[0]));
	Vector3 upper = lower;
	for (int i = 1; i < p_proxy.proxy.count; i++) {
		Vector3 v = p_to_local.xform(g_vec(p_proxy.points[i]));
		lower.x = MIN(lower.x, v.x);
		lower.y = MIN(lower.y, v.y);
		lower.z = MIN(lower.z, v.z);
		upper.x = MAX(upper.x, v.x);
		upper.y = MAX(upper.y, v.y);
		upper.z = MAX(upper.z, v.z);
	}
	float pad = p_proxy.proxy.radius + p_inflate;
	b3AABB bounds;
	bounds.lowerBound = b3_vec(lower - Vector3(pad, pad, pad));
	bounds.upperBound = b3_vec(upper + Vector3(pad, pad, pad));
	return bounds;
}

// Closest contact between one of our shapes and everything within p_margin.
// Returns the deepest one, which is what Godot reports as "the" collision.
// Ray shapes are skipped: they have no contact surface to rest on.
bool query_contacts(Box3DBody *p_body, const Transform &p_xform, real_t p_margin, const Set<RID> &p_exclude,
		Contact *r_deepest, Vector3 *r_recover, bool p_include_rays) {
	bool any = false;
	if (r_recover) {
		*r_recover = Vector3();
	}

	// Query group = BOX3D_QUERY_BIT (see box3d_types.h): the broadphase filter
	// is bidirectional, and only the reserved query bit satisfies its second
	// term for every shape, leaving Godot's query.mask & body.layer rule.
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = BOX3D_QUERY_BIT;
	filter.maskBits = p_body->collision_mask;

	for (int i = 0; i < p_body->shapes.size(); i++) {
		const Box3DBody::ShapeInstance &si = p_body->shapes[i];
		if (si.disabled || !si.shape) {
			continue;
		}
		if (!p_include_rays && ray_shape(si.shape)) {
			continue;
		}

		Box3DWorldProxy ours;
		if (!box3d_build_godot_proxy(si.shape, p_xform * si.xform, ours)) {
			continue;
		}

		Candidates candidates;
		candidates.skip = p_body->id;
		candidates.exclude = &p_exclude;
		b3World_OverlapAABB(p_body->space->world, box3d_proxy_aabb(ours, p_margin), filter, collect_candidate, &candidates);

		for (int c = 0; c < candidates.shapes.size(); c++) {
			const b3ShapeType type = b3Shape_GetType(candidates.shapes[c]);
			if (type == b3_meshShape || type == b3_heightShape) {
				b3WorldTransform wt = b3Body_GetTransform(b3Shape_GetBody(candidates.shapes[c]));
				Transform to_world = g_transform(wt.p, wt.q);

				TriangleContacts tri_ctx;
				tri_ctx.ours = &ours;
				tri_ctx.to_world = to_world;
				tri_ctx.margin = p_margin;
				tri_ctx.shape = candidates.shapes[c];
				tri_ctx.local_shape = i;
				tri_ctx.recover = r_recover;
				tri_ctx.deepest = r_deepest;

				b3AABB bounds = local_bounds(ours, to_world.affine_inverse(), p_margin);
				if (type == b3_meshShape) {
					b3Mesh mesh = b3Shape_GetMesh(candidates.shapes[c]);
					b3QueryMesh(&mesh, bounds, triangle_callback, &tri_ctx);
				} else {
					b3QueryHeightField(b3Shape_GetHeightField(candidates.shapes[c]), bounds, triangle_callback, &tri_ctx);
				}
				any = any || tri_ctx.any;

				// GJK cannot measure a shape that already overlaps the triangle
				// (distance collapses to zero with no direction), which is exactly
				// the state a body resting on level geometry is in. Probe each
				// proxy point with a vertical ray from slightly above: a hit
				// closer than the point's surface means penetration, and the hit
				// normal doubles as the recovery direction. Bullet recovers these
				// through its custom mesh contact pairs; Box3D has none for
				// kinematic bodies.
				const float probe_down = 2.0f * p_margin + ours.proxy.radius;
				for (int pi = 0; pi < ours.proxy.count; pi++) {
					Vector3 from = g_vec(ours.points[pi]) + Vector3(0, p_margin, 0);
					Vector3 to = from - Vector3(0, p_margin + probe_down, 0);

					MeshProbeContext probe_ctx;
					probe_ctx.skip = p_body->id;
					probe_ctx.exclude = &p_exclude;
					probe_ctx.mask = p_body->collision_mask;
					probe_ctx.point = from;

					b3World_CastRay(p_body->space->world, b3_pos(from), b3_vec(to - from), filter,
							mesh_probe_callback, &probe_ctx);

					if (!probe_ctx.hit) {
						continue;
					}

					float penetration = (p_margin + ours.proxy.radius) - probe_ctx.fraction * probe_down;
					if (penetration <= 0.0f && penetration > -p_margin) {
						// Touching within the margin: resting contact, no recovery.
						float depth = p_margin + penetration;
						if (r_deepest && depth > r_deepest->depth) {
							r_deepest->normal = g_vec(probe_ctx.normal);
							r_deepest->point = g_pos(probe_ctx.point_hit);
							r_deepest->depth = depth;
							r_deepest->shape = candidates.shapes[c];
							r_deepest->local_shape = i;
							r_deepest->valid = true;
							any = true;
						}
						continue;
					}
					if (penetration <= 0.0f) {
						continue;
					}

					Vector3 normal = g_vec(probe_ctx.normal);
					if (normal.length_squared() < 0.5f) {
						// Initial-overlap hits carry no normal (the probe started
						// inside the surface); recover straight up the probe.
						normal = Vector3(0, 1, 0);
					}
					if (r_recover) {
						*r_recover += normal * penetration * RECOVER_SCALE;
					}
					if (r_deepest && penetration > r_deepest->depth) {
						r_deepest->normal = normal;
						r_deepest->point = g_pos(probe_ctx.point_hit);
						r_deepest->depth = penetration;
						r_deepest->shape = candidates.shapes[c];
						r_deepest->local_shape = i;
						r_deepest->valid = true;
					}
					any = true;
				}
				continue;
			}

			Box3DWorldProxy theirs;
			if (!box3d_build_b3_proxy(candidates.shapes[c], theirs)) {
				continue;
			}

			// Both clouds are already in world space, so B's pose in A's frame is identity.
			Vector3 normal;
			Vector3 point;
			real_t depth = 0.0;
			if (!proxy_contact(ours, theirs.proxy, p_margin, normal, point, depth)) {
				continue;
			}
			any = true;

			if (r_recover) {
				*r_recover += normal * depth * RECOVER_SCALE;
			}
			if (r_deepest && depth > r_deepest->depth) {
				r_deepest->normal = normal;
				r_deepest->point = point;
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
	b3Pos point;
	b3Vec3 normal;
	b3ShapeId shape = b3_nullShapeId;
	int local_shape = 0;
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
		ctx->point = p_point;
		ctx->normal = p_normal;
		ctx->shape = p_shape;
		ctx->hit = true;
	}
	return p_fraction; // clip the sweep here
}

// Ray separation narrows down to a single closest surface crossing per ray.
struct RaySepContext {
	b3BodyId skip;
	const Set<RID> *exclude;
	bool infinite_inertia;
	bool hit = false;
	float fraction = 1.0f;
	b3Pos point;
	b3Vec3 normal;
	b3ShapeId shape = b3_nullShapeId;
};

float ray_sep_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t, int, int, void *p_context) {
	RaySepContext *ctx = (RaySepContext *)p_context;

	b3BodyId owner = b3Shape_GetBody(p_shape);
	if (B3_ID_EQUALS(owner, ctx->skip)) {
		return -1.0f;
	}
	Box3DEntity *entity = (Box3DEntity *)b3Body_GetUserData(owner);
	if (!entity || entity->is_area) {
		return -1.0f;
	}
	Box3DBody *other = (Box3DBody *)entity;
	if (ctx->infinite_inertia && other->mode != PhysicsServer::BODY_MODE_STATIC &&
			other->mode != PhysicsServer::BODY_MODE_KINEMATIC) {
		// Infinite inertia: the character shoves dynamics instead of resting.
		return -1.0f;
	}
	if (ctx->exclude && !ctx->exclude->empty() && ctx->exclude->has(other->self)) {
		return -1.0f;
	}

	if (p_fraction < ctx->fraction) {
		ctx->fraction = p_fraction;
		ctx->point = p_point;
		ctx->normal = p_normal;
		ctx->shape = p_shape;
		ctx->hit = true;
	}
	return p_fraction;
}

} // namespace

bool Box3DSpace::test_motion(Box3DBody *p_body, const Transform &p_from, const Vector3 &p_motion, real_t p_margin,
		PhysicsServer::MotionResult *r_result, bool p_exclude_raycast_shapes, const Set<RID> &p_exclude) {
	ERR_FAIL_COND_V(!p_body || !p_body->in_world(), false);

	const real_t margin = MAX(p_margin, (real_t)MIN_MARGIN);

	// Box3D has no body scale, so the query runs on the rotation only.
	Transform xform = p_from;
	xform.basis.orthonormalize();

	// Phase 1: depenetrate real shapes; ray shapes have no contact surface.
	Vector3 recovered;
	for (int i = 0; i < RECOVER_CYCLES; i++) {
		Vector3 step;
		if (!query_contacts(p_body, xform, margin, p_exclude, nullptr, &step, false)) {
			break;
		}
		xform.origin += step;
		recovered += step;
	}

	// Phase 2: sweep, clamping the motion at the first thing each shape hits.
	// Ray shapes sweep too unless the caller excludes them (move_and_slide
	// excludes, floor snapping does not).
	Vector3 motion = p_motion;
	const real_t total_length = p_motion.length();
	real_t safe_fraction = 1.0;
	real_t unsafe_fraction = 1.0;
	CastHit sweep;

	if (total_length > CMP_EPSILON) {
		// Query group = BOX3D_QUERY_BIT: see the note in query_contacts().
		b3QueryFilter filter = b3DefaultQueryFilter();
		filter.categoryBits = BOX3D_QUERY_BIT;
		filter.maskBits = p_body->collision_mask;

		for (int i = 0; i < p_body->shapes.size(); i++) {
			const Box3DBody::ShapeInstance &si = p_body->shapes[i];
			if (si.disabled || !si.shape) {
				continue;
			}
			if (p_exclude_raycast_shapes && ray_shape(si.shape)) {
				continue;
			}
			if (motion.length_squared() <= CMP_EPSILON * CMP_EPSILON) {
				break;
			}

			Box3DWorldProxy ours;
			if (!box3d_build_godot_proxy(si.shape, xform * si.xform, ours)) {
				continue;
			}

			CastHit hit;
			hit.skip = p_body->id;
			hit.exclude = &p_exclude;
			b3World_CastShape(world, b3_pos(Vector3()), &ours.proxy, b3_vec(motion), filter, cast_callback, &hit);

			b3TreeStats stats = b3World_CastShape(world, b3_pos(Vector3()), &ours.proxy, b3_vec(motion), filter, cast_callback, &hit);


			if (!hit.hit) {
				continue;
			}

			// Fractions are reported against the already clipped motion, so scale
			// them back onto the caller's motion before comparing shapes.
			real_t hit_fraction = hit.fraction * motion.length() / total_length;
			if (hit_fraction < unsafe_fraction) {
				unsafe_fraction = hit_fraction;
				safe_fraction = MAX(hit_fraction - margin / total_length, (real_t)0.0);
				sweep = hit;
				sweep.local_shape = i;
			}
			motion *= hit.fraction;
		}
	}

	xform.origin += motion;

	// Phase 3: what are we resting against now?
	Contact contact;
	bool colliding = query_contacts(p_body, xform, margin, p_exclude, &contact, nullptr, false) && contact.valid;

	// The contact phase compares point clouds, so it cannot see meshes or height
	// fields, and ray shapes have no surface to rest on. The sweep sees all of
	// them. Whenever it stopped the motion, that hit IS the collision: reporting
	// "nothing hit" while having clamped the motion to zero freezes the body in
	// place and never grounds it.
	if (!colliding && sweep.hit && B3_IS_NON_NULL(sweep.shape)) {
		Vector3 normal = g_vec(sweep.normal);
		if (normal.length_squared() < CMP_EPSILON) {
			// b3ShapeCast leaves the normal at zero when the cast starts already
			// touching. Facing back down the motion is the honest answer there.
			normal = -p_motion.normalized();
		}
		contact.normal = normal;
		contact.point = g_pos(sweep.point);
		contact.depth = margin; // touching within the sweep margin
		contact.shape = sweep.shape;
		contact.local_shape = sweep.local_shape;
		contact.valid = normal.length_squared() > CMP_EPSILON;
		colliding = contact.valid;
	}

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

int Box3DSpace::test_ray_separation(Box3DBody *p_body, const Transform &p_transform, bool p_infinite_inertia,
		Vector3 &r_recover_motion, PhysicsServer::SeparationResult *r_results, int p_result_max, float p_margin) {
	r_recover_motion = Vector3();
	ERR_FAIL_COND_V(!p_body || !p_body->in_world(), 0);
	if (p_result_max <= 0) {
		return 0;
	}

	// The Bullet backend recovers ray shapes from penetration through a custom
	// one-sided contact pair. Box3D has no such pair, so each ray shape is a
	// segment cast from its tip back toward the origin: a hit means the
	// surface crosses the ray, and the body recovers along the hit normal by
	// the penetration depth. That covers the kinematic ray-feet pattern.
	Transform xform = p_transform;
	xform.basis.orthonormalize();

	const float margin = MAX(p_margin, MIN_MARGIN);
	Vector3 recover;
	int total = 0;

	for (int cycle = 0; cycle < RECOVER_CYCLES && total < p_result_max; cycle++) {
		int found_this_round = 0;

		for (int i = 0; i < p_body->shapes.size() && total < p_result_max; i++) {
			const Box3DBody::ShapeInstance &si = p_body->shapes[i];
			if (si.disabled || !si.shape || !ray_shape(si.shape)) {
				continue;
			}

			Dictionary d = si.shape->data;
			float length = d.has("length") ? (float)(real_t)d["length"] : 1.0f;

			Transform shape_xform = xform * si.xform;
			shape_xform.origin += recover;

			Vector3 origin = shape_xform.origin;
			Vector3 tip = shape_xform.xform(Vector3(0, 0, length));
			Vector3 down = tip - origin;
			float ray_length = down.length();
			if (ray_length < CMP_EPSILON) {
				continue;
			}
			down /= ray_length;

			b3QueryFilter filter = b3DefaultQueryFilter();
			filter.categoryBits = BOX3D_QUERY_BIT;
			filter.maskBits = p_body->collision_mask;

			// Cast from the origin down to the tip (the origin stays outside
			// the floor in equilibrium): a hit means the surface crosses the
			// ray, and the tip is penetration-deep past it.
			RaySepContext ctx;
			ctx.skip = p_body->id;
			ctx.exclude = &p_body->exceptions;
			ctx.infinite_inertia = p_infinite_inertia;

			b3World_CastRay(world, b3_pos(origin), b3_vec(down * ray_length), filter, ray_sep_callback, &ctx);

			if (!ctx.hit) {
				continue;
			}

			// The surface crossing sits at fraction * length from the origin,
			// so the tip is that much past it.
			float penetration = ray_length * (1.0f - ctx.fraction);
			if (penetration <= margin) {
				continue;
			}

			Vector3 normal = g_vec(ctx.normal);
			if (normal.length_squared() < 0.5f) {
				// Initial-overlap hits carry no normal (the ray started inside
				// the surface); recover straight back up the ray.
				normal = -down;
			}
			if (normal.dot(-down) < 0.0f) {
				// Only recover against surfaces facing back up the ray.
				continue;
			}

			recover += normal * penetration * RECOVER_SCALE;
			found_this_round++;

			if (r_results && total < p_result_max) {
				PhysicsServer::SeparationResult &result = r_results[total];
				Box3DBody *other = body_of(ctx.shape);
				result.collision_depth = penetration;
				result.collision_point = g_pos(ctx.point);
				result.collision_normal = normal;
				result.collision_local_shape = i;
				result.collider_shape = shape_index_of(ctx.shape);
				if (other) {
					result.collider = other->self;
					result.collider_id = other->instance_id;
					result.collider_velocity = g_vec(b3Body_GetWorldPointVelocity(b3Shape_GetBody(ctx.shape), ctx.point));
				}
				total++;
			}
		}

		if (found_this_round == 0) {
			break;
		}
	}

	r_recover_motion = recover;
	return total;
}
