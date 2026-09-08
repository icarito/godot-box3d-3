/**************************************************************************/
/*  box3d_objects.h                                                       */
/*  Shapes, spaces, bodies, areas and joints backing the Godot RIDs.      */
/**************************************************************************/

#ifndef BOX3D_OBJECTS_H
#define BOX3D_OBJECTS_H

#include "box3d_types.h"

#include "core/rid.h"
#include "core/set.h"
#include "servers/physics_server.h"

class Box3DSpace;
class Box3DBody;
class Box3DEntity;
class Box3DJoint;

/// A Godot shape RID is a geometry definition, not a Box3D shape: the same RID
/// can be attached to several bodies and Box3D shapes always belong to one body.
/// The b3ShapeId instances live in Box3DBody::ShapeInstance.
class Box3DShape : public RID_Data {
public:
	RID self;
	PhysicsServer::ShapeType type = PhysicsServer::SHAPE_BOX;
	Variant data;
	real_t margin = 0.04;
	real_t custom_bias = 0.0;

	// Everything holding this definition, to rebuild when it changes.
	Set<Box3DEntity *> owners;

};

/// Tag on the b3 body user data so queries can tell physics bodies from the
/// proxies created for areas. It must stay the first base class of both users:
/// the b3 user data is a void pointer, and only a first base shares the
/// complete object's address.
class Box3DEntity {
public:
	bool is_area = false;
	// A shape resource is shared, and editing it has to reach every holder.
	// Zone components resize their BoxShape once the area is live, so areas
	// need this as much as bodies do.
	virtual void rebuild_shapes() {}
	virtual ~Box3DEntity() {}
};

/// One reported contact on a body, in the shape PhysicsDirectBodyState expects.
struct Box3DContact {
	Vector3 local_position; // World point minus this body's origin.
	Vector3 world_position;
	Vector3 normal; // Points from the collider toward this body.
	real_t impulse = 0;
	int local_shape = 0;
	RID collider;
	ObjectID collider_id = 0;
	int collider_shape = 0;
};

class Box3DBody : public Box3DEntity, public RID_Data {
public:
	struct ShapeInstance {
		Box3DShape *shape = nullptr;
		Transform xform;
		bool disabled = false;
		b3ShapeId id = b3_nullShapeId;

		// Box3D clones hulls into the world's database but only REFERENCES mesh
		// and height field data, so it must outlive the b3 shape. It belongs to
		// the instance, not to the shared Box3DShape: the same trimesh resource
		// on two bodies needs two of these, and the local transform is baked in.
		b3MeshData *mesh_data = nullptr;
		b3HeightFieldData *height_data = nullptr;
	};

	RID self;
	Box3DSpace *space = nullptr;
	b3BodyId id = b3_nullBodyId;

	PhysicsServer::BodyMode mode = PhysicsServer::BODY_MODE_RIGID;
	ObjectID instance_id = 0;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	bool ray_pickable = true;
	bool ccd = false;
	bool omit_force_integration = false;
	int max_contacts_reported = 0;

	// State kept here so it survives while the body is outside a space.
	Transform transform;
	Vector3 linear_velocity;
	Vector3 angular_velocity;
	bool sleeping = false;
	bool can_sleep = true;
	uint32_t locked_axes = 0; // PhysicsServer::BodyAxis bitmask

	real_t mass = 1.0;
	real_t friction = 1.0;
	real_t bounce = 0.0;
	real_t gravity_scale = 1.0;
	real_t linear_damp = -1.0; // negative means "inherit from the space"
	real_t angular_damp = -1.0;
	real_t kinematic_safe_margin = 0.001;

	// Gravity and damping after area overrides ran, for the direct state.
	Vector3 total_gravity;
	real_t total_linear_damp = 0.0;
	real_t total_angular_damp = 0.0;
	bool gravity_from_area = false; // gravity came from an override, applied as a force

	Vector<ShapeInstance> shapes;

	// Collision exceptions, kept as RIDs so freeing order does not matter.
	Set<RID> exceptions;
	Map<RID, b3JointId> exception_joints;

	// Contacts gathered after the last step, only when reporting is on.
	Vector<Box3DContact> contacts;

	ObjectID fi_callback_id = 0;
	StringName fi_callback_method;
	Variant fi_callback_udata;
	bool was_awake = true;

	Set<Box3DJoint *> joints;

	_FORCE_INLINE_ bool in_world() const { return B3_IS_NON_NULL(id); }

	void set_space(Box3DSpace *p_space);
	void rebuild_shapes();
	void apply_mass();
	void apply_damping(real_t p_linear = -1.0, real_t p_angular = -1.0);
	void apply_filter();
	void apply_material();
	void apply_motion_locks();
	// Dropping a shape instance has to release the geometry it owns.
	void free_shape_geometry(int p_idx);
	void apply_exceptions();
	void clear_exceptions();

	Transform get_transform() const;
	Vector3 get_linear_velocity() const;
	Vector3 get_angular_velocity() const;
	bool is_sleeping() const;

	void dispatch_force_integration(real_t p_delta);
	void collect_contacts();
	~Box3DBody();

private:
	void _create_in_world();
	void _destroy_in_world();
	void _create_shape(int p_idx);
};

/// Godot Areas ride on a kinematic proxy body whose shapes are Box3D sensors:
/// that gives body monitoring, area monitoring and query support for free.
class Box3DArea : public Box3DEntity, public RID_Data {
public:
	struct ShapeInstance {
		Box3DShape *shape = nullptr;
		Transform xform;
		bool disabled = false;
		b3ShapeId id = b3_nullShapeId;
	};

	RID self;
	Box3DSpace *space = nullptr;
	b3BodyId id = b3_nullBodyId;

	ObjectID instance_id = 0;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	PhysicsServer::AreaSpaceOverrideMode override_mode = PhysicsServer::AREA_SPACE_OVERRIDE_DISABLED;
	bool monitorable = true;
	bool ray_pickable = true;
	Transform transform;

	Map<PhysicsServer::AreaParameter, Variant> params;

	Vector<ShapeInstance> shapes;

	ObjectID monitor_callback_id = 0;
	StringName monitor_callback_method;
	ObjectID area_monitor_callback_id = 0;
	StringName area_monitor_callback_method;

	_FORCE_INLINE_ bool in_world() const { return B3_IS_NON_NULL(id); }
	_FORCE_INLINE_ bool monitoring() const { return monitor_callback_id != 0 || area_monitor_callback_id != 0; }

	void set_space(Box3DSpace *p_space);
	void set_transform(const Transform &p_transform);
	void rebuild_shapes();
	void apply_filter();
	void apply_material();

	void set_monitor_callback(Object *p_receiver, const StringName &p_method);
	void set_area_monitor_callback(Object *p_receiver, const StringName &p_method);

	void report_body(uint32_t p_status, Box3DBody *p_body, int p_body_shape, int p_area_shape);
	void report_area(uint32_t p_status, Box3DArea *p_area, int p_area_shape, int p_self_shape);

	~Box3DArea();

private:
	void _create_in_world();
	void _destroy_in_world();
	void _create_shape(int p_idx);
};

class Box3DDirectSpaceState;

class Box3DSpace : public RID_Data {
public:
	RID self;
	b3WorldId world = b3_nullWorldId;
	bool active = false;
	// Sub-steps buy solver quality and cost close to linear time. Two is the
	// low-end default; physics/3d/box3d_substeps overrides it, range 1-8.
	int sub_steps = 2;

	// Godot's World feeds gravity through the space RID's default area.
	real_t gravity_magnitude = 9.8;
	Vector3 gravity_vector = Vector3(0, -1, 0);
	real_t area_linear_damp = 0.1;
	real_t area_angular_damp = 0.1;

	real_t last_step = 0.0;
	List<Box3DBody *> bodies;
	List<Box3DArea *> areas;

	Box3DBody *owner_body(RID p_rid) const;

	// Debug contact buffer for the physics visualization.
	int debug_contact_max = 0;
	Vector<Vector3> debug_contacts;

	Box3DDirectSpaceState *direct_state = nullptr;

	void apply_gravity();
	void step(real_t p_delta);
	void pump_events(real_t p_delta);
	void apply_area_overrides();

	// Kinematic queries, see box3d_motion.cpp.
	bool test_motion(Box3DBody *p_body, const Transform &p_from, const Vector3 &p_motion, real_t p_margin,
			PhysicsServer::MotionResult *r_result, bool p_exclude_raycast_shapes, const Set<RID> &p_exclude);
	int test_ray_separation(Box3DBody *p_body, const Transform &p_transform, bool p_infinite_inertia,
			Vector3 &r_recover_motion, PhysicsServer::SeparationResult *r_results, int p_result_max, float p_margin);

	Box3DSpace();
	~Box3DSpace();
};

class Box3DDirectSpaceState : public PhysicsDirectSpaceState {
	GDCLASS(Box3DDirectSpaceState, PhysicsDirectSpaceState);

public:
	Box3DSpace *space = nullptr;

	virtual int intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, bool p_pick_ray = false);
	virtual int intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &p_closest_safe, float &p_closest_unsafe, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, ShapeRestInfo *r_info = nullptr);
	virtual bool collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual bool rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false);
	virtual Vector3 get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const;
};

/// One shared instance, like Godot's own servers do: it is only ever live for
/// the duration of a force integration callback.
class Box3DDirectBodyState : public PhysicsDirectBodyState {
	GDCLASS(Box3DDirectBodyState, PhysicsDirectBodyState);

	static Box3DDirectBodyState *singleton;

public:
	Box3DBody *body = nullptr;
	real_t delta = 0.0;

	static Box3DDirectBodyState *get_singleton();
	static void free_singleton();

	virtual Vector3 get_total_gravity() const;
	virtual float get_total_angular_damp() const;
	virtual float get_total_linear_damp() const;

	virtual Vector3 get_center_of_mass() const;
	virtual Basis get_principal_inertia_axes() const;
	virtual float get_inverse_mass() const;
	virtual Vector3 get_inverse_inertia() const;
	virtual Basis get_inverse_inertia_tensor() const;

	virtual void set_linear_velocity(const Vector3 &p_velocity);
	virtual Vector3 get_linear_velocity() const;
	virtual void set_angular_velocity(const Vector3 &p_velocity);
	virtual Vector3 get_angular_velocity() const;

	virtual void set_transform(const Transform &p_transform);
	virtual Transform get_transform() const;

	virtual Vector3 get_velocity_at_local_position(const Vector3 &p_position) const;

	virtual void add_central_force(const Vector3 &p_force);
	virtual void add_force(const Vector3 &p_force, const Vector3 &p_pos);
	virtual void add_torque(const Vector3 &p_torque);
	virtual void apply_central_impulse(const Vector3 &p_j);
	virtual void apply_impulse(const Vector3 &p_pos, const Vector3 &p_j);
	virtual void apply_torque_impulse(const Vector3 &p_j);

	virtual void set_sleep_state(bool p_enable);
	virtual bool is_sleeping() const;

	virtual int get_contact_count() const;
	virtual Vector3 get_contact_local_position(int p_contact_idx) const;
	virtual Vector3 get_contact_local_normal(int p_contact_idx) const;
	virtual float get_contact_impulse(int p_contact_idx) const;
	virtual int get_contact_local_shape(int p_contact_idx) const;
	virtual RID get_contact_collider(int p_contact_idx) const;
	virtual Vector3 get_contact_collider_position(int p_contact_idx) const;
	virtual ObjectID get_contact_collider_id(int p_contact_idx) const;
	virtual int get_contact_collider_shape(int p_contact_idx) const;
	virtual Vector3 get_contact_collider_velocity_at_position(int p_contact_idx) const;

	virtual real_t get_step() const { return delta; }
	virtual PhysicsDirectSpaceState *get_space_state();
};

/// Box3D joints live as long as both bodies share a space, and are rebuilt
/// when a body re-enters. Godot-side parameters are kept so getters work.
class Box3DJoint : public RID_Data {
public:
	RID self;
	PhysicsServer::JointType type = PhysicsServer::JOINT_PIN;
	b3JointId id = b3_nullJointId;

	Box3DBody *body_a = nullptr;
	Box3DBody *body_b = nullptr;

	bool disable_collisions = false;
	int solver_priority = 1;

	Transform frame_a;
	Transform frame_b;

	// Generic parameter storage, keyed per joint family.
	Map<int, real_t> params;
	Map<int, bool> flags;
	Vector3 axis_a;
	Vector3 axis_b;

	_FORCE_INLINE_ bool in_world() const { return B3_IS_NON_NULL(id); }

	void set_bodies(Box3DBody *p_a, Box3DBody *p_b);
	void rebuild();
	void invalidate();

	~Box3DJoint();

private:
	void _destroy_in_world();
};

#endif // BOX3D_OBJECTS_H
