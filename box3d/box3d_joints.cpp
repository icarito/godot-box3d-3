/**************************************************************************/
/*  box3d_joints.cpp                                                      */
/*  Godot joints mapped onto the Box3D joint families.                    */
/*  Godot frame conventions (they line up with Box3D's): hinge axis is    */
/*  the frame Z, slider axis is the frame X, cone twist axis is the       */
/*  frame Z. 6DOF has no equivalent and degrades to a weld.               */
/**************************************************************************/

#include "box3d_objects.h"

#include "core/object.h"

void Box3DJoint::set_bodies(Box3DBody *p_a, Box3DBody *p_b) {
	if (body_a) {
		body_a->joints.erase(this);
	}
	if (body_b) {
		body_b->joints.erase(this);
	}
	body_a = p_a;
	body_b = p_b;
	if (body_a) {
		body_a->joints.insert(this);
	}
	if (body_b) {
		body_b->joints.insert(this);
	}
	rebuild();
}

void Box3DJoint::invalidate() {
	_destroy_in_world();
}

void Box3DJoint::_destroy_in_world() {
	if (!in_world()) {
		return;
	}
	// The id may already be gone: it dies with its bodies, which are destroyed
	// before their joints' owners notice. Validate to stay safe.
	if (b3Joint_IsValid(id)) {
		b3DestroyJoint(id, true);
	}
	id = b3_nullJointId;
}

void Box3DJoint::rebuild() {
	_destroy_in_world();

	if (!body_a || !body_b || !body_a->space || body_a->space != body_b->space) {
		return;
	}
	if (!body_a->in_world() || !body_b->in_world()) {
		return;
	}

	b3WorldId world = body_a->space->world;

	// The specific default defs carry Box3D's internal validation cookie;
	// fill their base in place instead of assigning over it.
	switch (type) {
		case PhysicsServer::JOINT_PIN: {
			b3SphericalJointDef def = b3DefaultSphericalJointDef();
			def.base.bodyIdA = body_a->id;
			def.base.bodyIdB = body_b->id;
			def.base.collideConnected = !disable_collisions;
			def.base.localFrameA = b3_transform(frame_a);
			def.base.localFrameB = b3_transform(frame_b);
			id = b3CreateSphericalJoint(world, &def);
		} break;
		case PhysicsServer::JOINT_HINGE: {
			b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
			def.base.bodyIdA = body_a->id;
			def.base.bodyIdB = body_b->id;
			def.base.collideConnected = !disable_collisions;
			def.base.localFrameA = b3_transform(frame_a);
			def.base.localFrameB = b3_transform(frame_b);
			id = b3CreateRevoluteJoint(world, &def);
			const float use_limit = flags.has(PhysicsServer::HINGE_JOINT_FLAG_USE_LIMIT) ? flags[PhysicsServer::HINGE_JOINT_FLAG_USE_LIMIT] : false;
			if (use_limit) {
				b3RevoluteJoint_SetLimits(id,
						params.has(PhysicsServer::HINGE_JOINT_LIMIT_LOWER) ? params[PhysicsServer::HINGE_JOINT_LIMIT_LOWER] : 0.0,
						params.has(PhysicsServer::HINGE_JOINT_LIMIT_UPPER) ? params[PhysicsServer::HINGE_JOINT_LIMIT_UPPER] : 0.0);
			}
			if (flags.has(PhysicsServer::HINGE_JOINT_FLAG_ENABLE_MOTOR) && flags[PhysicsServer::HINGE_JOINT_FLAG_ENABLE_MOTOR]) {
				b3RevoluteJoint_EnableMotor(id, true);
				b3RevoluteJoint_SetMotorSpeed(id, params.has(PhysicsServer::HINGE_JOINT_MOTOR_TARGET_VELOCITY) ? params[PhysicsServer::HINGE_JOINT_MOTOR_TARGET_VELOCITY] : 0.0);
				b3RevoluteJoint_SetMaxMotorTorque(id, params.has(PhysicsServer::HINGE_JOINT_MOTOR_MAX_IMPULSE) ? params[PhysicsServer::HINGE_JOINT_MOTOR_MAX_IMPULSE] : 0.0);
			}
		} break;
		case PhysicsServer::JOINT_SLIDER: {
			b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
			def.base.bodyIdA = body_a->id;
			def.base.bodyIdB = body_b->id;
			def.base.collideConnected = !disable_collisions;
			def.base.localFrameA = b3_transform(frame_a);
			def.base.localFrameB = b3_transform(frame_b);
			id = b3CreatePrismaticJoint(world, &def);
			// Godot 3's slider has no flags: the linear limits are always
			// active (defaults span -1..1); angular limits have no prismatic
			// counterpart because Box3D prismatic joints lock every rotation.
			const float lower = params.has(PhysicsServer::SLIDER_JOINT_LINEAR_LIMIT_LOWER) ? params[PhysicsServer::SLIDER_JOINT_LINEAR_LIMIT_LOWER] : -1.0;
			const float upper = params.has(PhysicsServer::SLIDER_JOINT_LINEAR_LIMIT_UPPER) ? params[PhysicsServer::SLIDER_JOINT_LINEAR_LIMIT_UPPER] : 1.0;
			if (upper > lower) {
				b3PrismaticJoint_EnableLimit(id, true);
				b3PrismaticJoint_SetLimits(id, lower, upper);
			}
		} break;
		case PhysicsServer::JOINT_CONE_TWIST: {
			b3SphericalJointDef def = b3DefaultSphericalJointDef();
			def.base.bodyIdA = body_a->id;
			def.base.bodyIdB = body_b->id;
			def.base.collideConnected = !disable_collisions;
			def.base.localFrameA = b3_transform(frame_a);
			def.base.localFrameB = b3_transform(frame_b);
			id = b3CreateSphericalJoint(world, &def);

			const real_t swing_span = params.has(PhysicsServer::CONE_TWIST_JOINT_SWING_SPAN) ? params[PhysicsServer::CONE_TWIST_JOINT_SWING_SPAN] : 0.0;
			const real_t twist_span = params.has(PhysicsServer::CONE_TWIST_JOINT_TWIST_SPAN) ? params[PhysicsServer::CONE_TWIST_JOINT_TWIST_SPAN] : 0.0;
			if (swing_span < Math_PI) {
				b3SphericalJoint_EnableConeLimit(id, true);
				b3SphericalJoint_SetConeLimit(id, MAX((float)swing_span, 0.01f));
			}
			if (twist_span < Math_PI) {
				b3SphericalJoint_EnableTwistLimit(id, true);
				b3SphericalJoint_SetTwistLimits(id, -(float)twist_span, (float)twist_span);
			}
		} break;
		case PhysicsServer::JOINT_6DOF: {
			// No per-axis limits or motors in Box3D: Godot's 6DOF defaults are
			// all-locked, which is exactly a weld.
			b3WeldJointDef def = b3DefaultWeldJointDef();
			def.base.bodyIdA = body_a->id;
			def.base.bodyIdB = body_b->id;
			def.base.collideConnected = !disable_collisions;
			def.base.localFrameA = b3_transform(frame_a);
			def.base.localFrameB = b3_transform(frame_b);
			id = b3CreateWeldJoint(world, &def);
			ERR_PRINT_ONCE("Box3D: generic 6DOF joints are approximated with a weld; per-axis limits, motors and flags have no effect.");
		} break;
		default:
			ERR_PRINT("Box3D: joint type " + itos(type) + " is not supported.");
			break;
	}
}

Box3DJoint::~Box3DJoint() {
	_destroy_in_world();
	if (body_a) {
		body_a->joints.erase(this);
	}
	if (body_b) {
		body_b->joints.erase(this);
	}
}
