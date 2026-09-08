/**************************************************************************/
/*  box3d_queries.cpp                                                     */
/*  PhysicsDirectSpaceState queries over the Box3D world.                 */
/**************************************************************************/

#include "box3d_objects.h"

#include "box3d_proxies.h"

#include "box3d_physics_server.h"

/* Shared query plumbing */

struct QueryEntity {
	Box3DBody *body = nullptr;
	Box3DArea *area = nullptr;
	RID rid;
	Object *collider = nullptr;
	ObjectID instance_id = 0;
	int shape_index = 0;
	bool valid = false;
};

static QueryEntity query_entity_of(b3ShapeId p_shape) {
	QueryEntity out;
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
		out.rid = out.area->self;
		out.instance_id = out.area->instance_id;
	} else {
		out.body = (Box3DBody *)entity;
		out.rid = out.body->self;
		out.instance_id = out.body->instance_id;
	}
	out.collider = ObjectDB::get_instance(out.instance_id);
	out.valid = true;
	return out;
}

static bool query_accepts(const QueryEntity &p_entity, const Set<RID> &p_exclude, uint32_t p_collision_mask,
		bool p_collide_with_bodies, bool p_collide_with_areas, bool p_pick_ray_only) {
	if (!p_entity.valid) {
		return false;
	}
	if (p_entity.area) {
		if (!p_collide_with_areas) {
			return false;
		}
		if (p_pick_ray_only && !p_entity.area->ray_pickable) {
			return false;
		}
	} else {
		if (!p_collide_with_bodies) {
			return false;
		}
		if (p_pick_ray_only && !p_entity.body->ray_pickable) {
			return false;
		}
	}
	if (!p_exclude.empty() && p_exclude.has(p_entity.rid)) {
		return false;
	}
	return true;
}

// A world shape passes when its category bits meet the query mask, which is
// what the Box3D broadphase applies on every query.
static b3QueryFilter query_filter(uint32_t p_collision_mask) {
	// See BOX3D_QUERY_BIT: claiming it as the query's category satisfies Box3D's
	// second filter term for every shape, leaving Godot's rule, query.mask & layer.
	b3QueryFilter filter = b3DefaultQueryFilter();
	filter.categoryBits = BOX3D_QUERY_BIT;
	filter.maskBits = p_collision_mask;
	return filter;
}

/* intersect_ray */

struct RayContext {
	const Set<RID> *exclude;
	uint32_t collision_mask;
	bool collide_with_bodies;
	bool collide_with_areas;
	bool pick_ray;

	bool hit = false;
	float fraction = 1.0f;
	b3Pos point;
	b3Vec3 normal;
	b3ShapeId shape = b3_nullShapeId;
};

static float ray_callback(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t, int, int, void *p_context) {
	RayContext *ctx = (RayContext *)p_context;
	QueryEntity entity = query_entity_of(p_shape);
	if (!query_accepts(entity, *ctx->exclude, ctx->collision_mask, ctx->collide_with_bodies, ctx->collide_with_areas, ctx->pick_ray)) {
		return -1.0f; // skip this shape, keep going
	}
	if (p_fraction < ctx->fraction) {
		ctx->fraction = p_fraction;
		ctx->point = p_point;
		ctx->normal = p_normal;
		ctx->shape = p_shape;
		ctx->hit = true;
	}
	return p_fraction; // clip the ray at this hit
}

bool Box3DDirectSpaceState::intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, bool p_pick_ray) {
	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), false);

	Vector3 delta = p_to - p_from;
	if (delta.length_squared() < CMP_EPSILON * CMP_EPSILON) {
		return false;
	}

	RayContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collision_mask = p_collision_mask;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.collide_with_areas = p_collide_with_areas;
	ctx.pick_ray = p_pick_ray;

	b3World_CastRay(space->world, b3_pos(p_from), b3_vec(delta), query_filter(p_collision_mask), ray_callback, &ctx);

	if (!ctx.hit) {
		return false;
	}

	QueryEntity entity = query_entity_of(ctx.shape);
	r_result.position = g_pos(ctx.point);
	r_result.normal = g_vec(ctx.normal);
	r_result.rid = entity.rid;
	r_result.collider = entity.collider;
	r_result.collider_id = entity.instance_id;
	r_result.shape = entity.shape_index;
	return true;
}

/* intersect_point / intersect_shape */

struct OverlapContext {
	const Set<RID> *exclude;
	uint32_t collision_mask;
	bool collide_with_bodies;
	bool collide_with_areas;

	PhysicsDirectSpaceState::ShapeResult *results;
	int result_max;
	int count = 0;
};

static bool overlap_callback(b3ShapeId p_shape, void *p_context) {
	OverlapContext *ctx = (OverlapContext *)p_context;
	QueryEntity entity = query_entity_of(p_shape);
	if (!query_accepts(entity, *ctx->exclude, ctx->collision_mask, ctx->collide_with_bodies, ctx->collide_with_areas, false)) {
		return true; // keep searching
	}
	if (ctx->results && ctx->count < ctx->result_max) {
		PhysicsDirectSpaceState::ShapeResult &r = ctx->results[ctx->count];
		r.rid = entity.rid;
		r.collider = entity.collider;
		r.collider_id = entity.instance_id;
		r.shape = entity.shape_index;
	}
	ctx->count++;
	return ctx->count < ctx->result_max;
}

int Box3DDirectSpaceState::intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), 0);
	if (p_result_max <= 0) {
		return 0;
	}

	OverlapContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collision_mask = p_collision_mask;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.collide_with_areas = p_collide_with_areas;
	ctx.results = r_results;
	ctx.result_max = p_result_max;

	// A zero-radius point cloud at the query position: Box3D treats it as a
	// solid point-in-shape test.
	Box3DWorldProxy proxy;
	proxy.points[0] = b3_vec(Vector3());
	proxy.proxy.points = proxy.points;
	proxy.proxy.count = 1;
	proxy.proxy.radius = 0.0f;

	b3World_OverlapShape(space->world, b3_pos(p_point), &proxy.proxy, query_filter(p_collision_mask), overlap_callback, &ctx);

	return ctx.count;
}

int Box3DDirectSpaceState::intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), 0);
	if (p_result_max <= 0) {
		return 0;
	}

	Box3DShape *shape = Box3DPhysicsServer::shape_from_rid(p_shape);
	ERR_FAIL_COND_V(!shape, 0);

	// Proxy points relative to the query origin keep the query exact; scale in
	// the basis is dropped, Box3D has no shape scale.
	Box3DWorldProxy proxy;
	if (!box3d_build_godot_proxy(shape, Transform(p_xform.basis.orthonormalized(), Vector3()), proxy)) {
		ERR_PRINT("Box3D: shape type " + itos(shape->type) + " is not supported for shape queries.");
		return 0;
	}
	proxy.proxy.radius += p_margin;

	OverlapContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collision_mask = p_collision_mask;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.collide_with_areas = p_collide_with_areas;
	ctx.results = r_results;
	ctx.result_max = p_result_max;

	b3World_OverlapShape(space->world, b3_pos(p_xform.origin), &proxy.proxy, query_filter(p_collision_mask), overlap_callback, &ctx);

	return ctx.count;
}

/* cast_motion */

struct CastContext {
	const Set<RID> *exclude;
	uint32_t collision_mask;
	bool collide_with_bodies;
	bool collide_with_areas;

	bool hit = false;
	float fraction = 1.0f;
	b3Pos point;
	b3Vec3 normal;
	b3ShapeId shape = b3_nullShapeId;
};

static float cast_callback2(b3ShapeId p_shape, b3Pos p_point, b3Vec3 p_normal, float p_fraction, uint64_t, int, int, void *p_context) {
	CastContext *ctx = (CastContext *)p_context;
	QueryEntity entity = query_entity_of(p_shape);
	if (!query_accepts(entity, *ctx->exclude, ctx->collision_mask, ctx->collide_with_bodies, ctx->collide_with_areas, false)) {
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

bool Box3DDirectSpaceState::cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &p_closest_safe, float &p_closest_unsafe, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas, ShapeRestInfo *r_info) {
	p_closest_safe = 0.0f;
	p_closest_unsafe = 0.0f;

	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), false);

	Box3DShape *shape = Box3DPhysicsServer::shape_from_rid(p_shape);
	ERR_FAIL_COND_V(!shape, false);

	Box3DWorldProxy proxy;
	if (!box3d_build_godot_proxy(shape, Transform(p_xform.basis.orthonormalized(), Vector3()), proxy)) {
		ERR_PRINT("Box3D: shape type " + itos(shape->type) + " is not supported for motion casts.");
		return false;
	}
	proxy.proxy.radius += p_margin;

	if (p_motion.length_squared() < CMP_EPSILON * CMP_EPSILON) {
		p_closest_safe = 1.0f;
		p_closest_unsafe = 1.0f;
		return true;
	}

	CastContext ctx;
	ctx.exclude = &p_exclude;
	ctx.collision_mask = p_collision_mask;
	ctx.collide_with_bodies = p_collide_with_bodies;
	ctx.collide_with_areas = p_collide_with_areas;

	b3World_CastShape(space->world, b3_pos(p_xform.origin), &proxy.proxy, b3_vec(p_motion),
			query_filter(p_collision_mask), cast_callback2, &ctx);

	if (ctx.hit) {
		// Same safe/unsafe relationship as the Bullet backend.
		const real_t l = p_motion.length();
		p_closest_unsafe = ctx.fraction;
		p_closest_safe = MAX(p_closest_unsafe - (real_t)0.01 / l, (real_t)0.0);
		if (r_info) {
			QueryEntity entity = query_entity_of(ctx.shape);
			r_info->point = g_pos(ctx.point);
			r_info->normal = g_vec(ctx.normal);
			r_info->rid = entity.rid;
			r_info->collider_id = entity.instance_id;
			r_info->shape = entity.shape_index;
			if (entity.body && entity.body->in_world()) {
				r_info->linear_velocity = g_vec(b3Body_GetWorldPointVelocity(entity.body->id, ctx.point));
			}
		}
	} else {
		p_closest_safe = 1.0f;
		p_closest_unsafe = 1.0f;
	}

	return true;
}

/* collide_shape / rest_info */

struct ClosestHit {
	bool any = false;
	Vector3 point_a; // on the queried shape
	Vector3 point_b; // on the world shape
	Vector3 normal;  // from the world shape toward the queried shape
	real_t depth = -1e30;
	b3ShapeId shape = b3_nullShapeId;
};

struct CollectContext {
	const Set<RID> *exclude;
	uint32_t collision_mask;
	bool collide_with_bodies;
	bool collide_with_areas;
	Vector<b3ShapeId> shapes;
};

static bool collect_callback(b3ShapeId p_shape, void *p_context) {
	CollectContext *ctx = (CollectContext *)p_context;
	QueryEntity entity = query_entity_of(p_shape);
	if (query_accepts(entity, *ctx->exclude, ctx->collision_mask, ctx->collide_with_bodies, ctx->collide_with_areas, false)) {
		ctx->shapes.push_back(p_shape);
	}
	return true;
}

static ClosestHit find_closest(Box3DSpace *p_space, Box3DShape *p_shape, const Transform &p_xform, float p_margin,
		const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ClosestHit best;

	Box3DWorldProxy ours;
	if (!box3d_build_godot_proxy(p_shape, Transform(p_xform.basis.orthonormalized(), p_xform.origin), ours)) {
		return best;
	}
	ours.proxy.radius += p_margin;

	// AABB prefilter, then a precise GJK distance per candidate. Both clouds
	// are in world space, so B's pose in A's frame is identity.
	b3QueryFilter filter = query_filter(p_collision_mask);

	CollectContext collect;
	collect.exclude = &p_exclude;
	collect.collision_mask = p_collision_mask;
	collect.collide_with_bodies = p_collide_with_bodies;
	collect.collide_with_areas = p_collide_with_areas;
	b3World_OverlapAABB(p_space->world, box3d_proxy_aabb(ours, p_margin), filter, collect_callback, &collect);

	for (int i = 0; i < collect.shapes.size(); i++) {
		Box3DWorldProxy theirs;
		if (!box3d_build_b3_proxy(collect.shapes[i], theirs)) {
			continue;
		}

		b3DistanceInput input = { 0 };
		input.proxyA = ours.proxy;
		input.proxyB = theirs.proxy;
		input.transform = b3Transform_identity;
		input.useRadii = true;

		b3SimplexCache cache = { 0 };
		b3DistanceOutput out = b3ShapeDistance(&input, &cache, nullptr, 0);
		if (out.distance > p_margin) {
			continue;
		}

		real_t depth = p_margin - out.distance;
		if (depth > best.depth) {
			best.any = true;
			best.depth = depth;
			best.point_a = g_vec(out.pointA);
			best.point_b = g_vec(out.pointB);
			// The GJK normal runs A to B; flip it to point at the queried shape.
			best.normal = -g_vec(out.normal);
			best.shape = collect.shapes[i];
		}
	}

	return best;
}

bool Box3DDirectSpaceState::collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	r_result_count = 0;
	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), false);
	if (p_result_max < 2) {
		return false;
	}

	Box3DShape *shape = Box3DPhysicsServer::shape_from_rid(p_shape);
	ERR_FAIL_COND_V(!shape, false);

	ClosestHit hit = find_closest(space, shape, p_shape_xform, p_margin, p_exclude, p_collision_mask, p_collide_with_bodies, p_collide_with_areas);
	if (!hit.any) {
		return false;
	}

	// One point pair, in Godot's order: queried shape first, world shape second.
	r_results[0] = hit.point_a;
	r_results[1] = hit.point_b;
	r_result_count = 1;
	return true;
}

bool Box3DDirectSpaceState::rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude, uint32_t p_collision_mask, bool p_collide_with_bodies, bool p_collide_with_areas) {
	ERR_FAIL_COND_V(!space || B3_IS_NULL(space->world), false);

	Box3DShape *shape = Box3DPhysicsServer::shape_from_rid(p_shape);
	ERR_FAIL_COND_V(!shape, false);

	ClosestHit hit = find_closest(space, shape, p_shape_xform, p_margin, p_exclude, p_collision_mask, p_collide_with_bodies, p_collide_with_areas);
	if (!hit.any) {
		return false;
	}

	QueryEntity entity = query_entity_of(hit.shape);
	r_info->point = hit.point_b;
	r_info->normal = hit.normal;
	r_info->rid = entity.rid;
	r_info->collider_id = entity.instance_id;
	r_info->shape = entity.shape_index;
	if (entity.body && entity.body->in_world()) {
		r_info->linear_velocity = g_vec(b3Body_GetWorldPointVelocity(entity.body->id, b3_pos(hit.point_b)));
	}
	return true;
}

Vector3 Box3DDirectSpaceState::get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const {
	ERR_FAIL_COND_V(!space, Vector3());

	b3BodyId body_id = b3_nullBodyId;
	for (List<Box3DBody *>::Element *E = space->bodies.front(); E; E = E->next()) {
		if (E->get()->self == p_object && E->get()->in_world()) {
			body_id = E->get()->id;
			break;
		}
	}
	if (B3_IS_NULL(body_id)) {
		for (List<Box3DArea *>::Element *E = space->areas.front(); E; E = E->next()) {
			if (E->get()->self == p_object && E->get()->in_world()) {
				body_id = E->get()->id;
				break;
			}
		}
	}
	ERR_FAIL_COND_V(B3_IS_NULL(body_id), Vector3());

	int capacity = b3Body_GetShapeCount(body_id);
	if (capacity <= 0) {
		return Vector3();
	}
	LocalVector<b3ShapeId> shapes;
	shapes.resize(capacity);
	int count = b3Body_GetShapes(body_id, shapes.ptr(), capacity);

	real_t best_distance = 1e30;
	Vector3 best_point = Vector3();
	for (int i = 0; i < count; i++) {
		Vector3 candidate = g_vec(b3Shape_GetClosestPoint(shapes[i], b3_vec(p_point)));
		real_t d = p_point.distance_squared_to(candidate);
		if (d < best_distance) {
			best_distance = d;
			best_point = candidate;
		}
	}
	return best_point;
}
