/**************************************************************************/
/*  box3d_proxies.h                                                       */
/*  Godot shapes as Box3D point-cloud proxies, shared by motion and       */
/*  space queries.                                                        */
/**************************************************************************/

#ifndef BOX3D_PROXIES_H
#define BOX3D_PROXIES_H

#include "box3d_objects.h"

// Everything runs in world space with a zero query origin. That is exact for a
// float build; a BOX3D_DOUBLE_PRECISION large world would want a local origin.

struct Box3DWorldProxy {
	b3Vec3 points[B3_MAX_SHAPE_CAST_POINTS];
	b3ShapeProxy proxy;
};

// A Godot shape definition, as a world space point cloud. Rays become
// one-segment proxies (origin, origin + local Z * length); meshes, height
// fields and planes have no point cloud form and are rejected.
inline bool box3d_build_godot_proxy(const Box3DShape *p_shape, const Transform &p_xform, Box3DWorldProxy &r_proxy) {
	switch (p_shape->type) {
		case PhysicsServer::SHAPE_BOX: {
			Vector3 he = p_shape->data;
			for (int i = 0; i < 8; i++) {
				Vector3 corner((i & 1) ? he.x : -he.x, (i & 2) ? he.y : -he.y, (i & 4) ? he.z : -he.z);
				r_proxy.points[i] = b3_vec(p_xform.xform(corner));
			}
			r_proxy.proxy.count = 8;
			r_proxy.proxy.radius = 0.0f;
		} break;
		case PhysicsServer::SHAPE_SPHERE: {
			float radius = p_shape->data;
			r_proxy.points[0] = b3_vec(p_xform.origin);
			r_proxy.proxy.count = 1;
			r_proxy.proxy.radius = radius;
		} break;
		case PhysicsServer::SHAPE_CAPSULE: {
			Dictionary d = p_shape->data;
			float radius = d.has("radius") ? (float)(real_t)d["radius"] : 0.5;
			float height = d.has("height") ? (float)(real_t)d["height"] : 1.0;
			r_proxy.points[0] = b3_vec(p_xform.xform(Vector3(0, -height * 0.5, 0)));
			r_proxy.points[1] = b3_vec(p_xform.xform(Vector3(0, height * 0.5, 0)));
			r_proxy.proxy.count = 2;
			r_proxy.proxy.radius = radius;
		} break;
		case PhysicsServer::SHAPE_CYLINDER: {
			Dictionary d = p_shape->data;
			float radius = d.has("radius") ? (float)(real_t)d["radius"] : 0.5;
			float height = d.has("height") ? (float)(real_t)d["height"] : 1.0;
			// Circle rim approximation: enough points for a conservative proxy.
			const int sides = 12;
			for (int i = 0; i < sides; i++) {
				float a = Math_TAU * i / sides;
				r_proxy.points[2 * i] = b3_vec(p_xform.xform(Vector3(radius * cos(a), -height * 0.5, radius * sin(a))));
				r_proxy.points[2 * i + 1] = b3_vec(p_xform.xform(Vector3(radius * cos(a), height * 0.5, radius * sin(a))));
			}
			r_proxy.proxy.count = 2 * sides;
			r_proxy.proxy.radius = 0.0f;
		} break;
		case PhysicsServer::SHAPE_CONVEX_POLYGON: {
			PoolVector3Array points = p_shape->data;
			int count = points.size();
			if (count < 1) {
				return false;
			}
			PoolVector3Array::Read r = points.read();
			count = MIN(count, (int)B3_MAX_SHAPE_CAST_POINTS);
			for (int i = 0; i < count; i++) {
				r_proxy.points[i] = b3_vec(p_xform.xform(r[i]));
			}
			r_proxy.proxy.count = count;
			r_proxy.proxy.radius = 0.0f;
		} break;
		case PhysicsServer::SHAPE_RAY: {
			Dictionary d = p_shape->data;
			float length = d.has("length") ? (float)(real_t)d["length"] : 1.0;
			r_proxy.points[0] = b3_vec(p_xform.xform(Vector3()));
			r_proxy.points[1] = b3_vec(p_xform.xform(Vector3(0, 0, length)));
			r_proxy.proxy.count = 2;
			r_proxy.proxy.radius = 0.0f;
		} break;
		default:
			return false;
	}

	r_proxy.proxy.points = r_proxy.points;
	return true;
}

// A live Box3D shape, as a world space point cloud.
inline bool box3d_build_b3_proxy(b3ShapeId p_shape, Box3DWorldProxy &r_proxy) {
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

inline b3AABB box3d_proxy_aabb(const Box3DWorldProxy &p_proxy, float p_inflate) {
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

#endif // BOX3D_PROXIES_H
