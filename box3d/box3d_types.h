/**************************************************************************/
/*  box3d_types.h                                                         */
/*  Conversions between Godot and Box3D math types.                       */
/**************************************************************************/

#ifndef BOX3D_TYPES_H
#define BOX3D_TYPES_H

#include "core/math/transform.h"

#include <box3d/box3d.h>

// Box3D accepts a shape for a query only when the filters agree both ways:
//   (shape.categoryBits & query.maskBits) && (shape.maskBits & query.categoryBits)
// Godot has no second term: a space query hits a body when query.mask & body.layer,
// and the body's own mask says what it detects, not what may detect it. A body with
// collision_mask 0 (ordinary for level geometry) would be invisible to every query.
// Godot's layers are 32 bits and these filters are 64, so the top bit is free: every
// shape claims it in maskBits and every query claims it as its category. No shape
// ever carries it as a category, so body-vs-body filtering is untouched.
#define BOX3D_QUERY_BIT (((uint64_t)1) << 63)

// Areas have the same mismatch. Godot fires body_entered when area.mask &
// body.layer; the body's own mask has no say, and a trigger normally sits on a
// layer the player deliberately does not collide with. Area shapes claim this
// bit as a category and body shapes carry it in their mask, so Box3D's first
// term is always satisfied for a sensor against a body and the second term is
// left holding exactly Godot's rule.
// Area against area stays bidirectional: one symmetric AND cannot express
// Godot's one-way rule in both directions at once.
#define BOX3D_SENSOR_BIT (((uint64_t)1) << 62)

// Box3D keeps world positions in b3Pos, which widens to double only in large-world
// builds, and everything else in float.

_FORCE_INLINE_ b3Vec3 b3_vec(const Vector3 &p_v) {
	b3Vec3 v = { (float)p_v.x, (float)p_v.y, (float)p_v.z };
	return v;
}

_FORCE_INLINE_ Vector3 g_vec(const b3Vec3 &p_v) {
	return Vector3(p_v.x, p_v.y, p_v.z);
}

// b3Pos is double only in BOX3D_DOUBLE_PRECISION builds, so assign rather than
// brace-initialize and let the compiler pick the width.
_FORCE_INLINE_ b3Pos b3_pos(const Vector3 &p_v) {
	b3Pos p;
	p.x = p_v.x;
	p.y = p_v.y;
	p.z = p_v.z;
	return p;
}

_FORCE_INLINE_ Vector3 g_pos(const b3Pos &p_p) {
	return Vector3(p_p.x, p_p.y, p_p.z);
}

// Both engines use the same quaternion convention: (axis * sin(a/2), cos(a/2)).
// Any scale in the basis is dropped, Box3D has no per-body scale.
_FORCE_INLINE_ b3Quat b3_quat(const Basis &p_basis) {
	Quat q = p_basis.get_rotation_quat();
	b3Quat r = { { (float)q.x, (float)q.y, (float)q.z }, (float)q.w };
	return r;
}

_FORCE_INLINE_ Basis g_basis(const b3Quat &p_q) {
	return Basis(Quat(p_q.v.x, p_q.v.y, p_q.v.z, p_q.s));
}

// b3Matrix3 holds columns, Basis stores rows and set_axis() writes a column.
_FORCE_INLINE_ Basis g_basis(const b3Matrix3 &p_m) {
	Basis b;
	b.set_axis(0, g_vec(p_m.cx));
	b.set_axis(1, g_vec(p_m.cy));
	b.set_axis(2, g_vec(p_m.cz));
	return b;
}

_FORCE_INLINE_ Transform g_transform(const b3Pos &p_p, const b3Quat &p_q) {
	return Transform(g_basis(p_q), g_pos(p_p));
}

_FORCE_INLINE_ b3Transform b3_transform(const Transform &p_t) {
	b3Transform t = { b3_vec(p_t.origin), b3_quat(p_t.basis) };
	return t;
}

#endif // BOX3D_TYPES_H
