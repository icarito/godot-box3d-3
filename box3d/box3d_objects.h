/**************************************************************************/
/*  box3d_objects.h                                                       */
/*  Shapes, spaces and bodies backing the Godot RIDs.                     */
/**************************************************************************/

#ifndef BOX3D_OBJECTS_H
#define BOX3D_OBJECTS_H

#include "box3d_types.h"

#include "core/rid.h"
#include "core/set.h"
#include "servers/physics_server.h"

class Box3DSpace;
class Box3DBody;

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

	// Bodies to rebuild when the geometry changes under them.
	Set<Box3DBody *> owners;
};

/// Placeholder so Area nodes and the World's default-area parameters have
/// somewhere to land. Real area behaviour is M6.
class Box3DArea : public RID_Data {
public:
	RID space;
	ObjectID instance_id = 0;
	uint32_t collision_layer = 1;
	uint32_t collision_mask = 1;
	bool ray_pickable = true;
	Transform transform;
	Map<PhysicsServer::AreaParameter, Variant> params;
};

class Box3DBody : public RID_Data {
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

	Vector<ShapeInstance> shapes;

	ObjectID fi_callback_id = 0;
	StringName fi_callback_method;
	Variant fi_callback_udata;
	bool was_awake = true;

	_FORCE_INLINE_ bool in_world() const { return B3_IS_NON_NULL(id); }

	void set_space(Box3DSpace *p_space);
	void rebuild_shapes();
	void apply_mass();
	void apply_damping();
	void apply_filter();
	void apply_material();
	void apply_motion_locks();

	Transform get_transform() const;
	Vector3 get_linear_velocity() const;
	Vector3 get_angular_velocity() const;
	bool is_sleeping() const;

	void dispatch_force_integration(real_t p_delta);

	~Box3DBody();

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

	// Godot's World feeds gravity through the space RID's default area.
	real_t gravity_magnitude = 9.8;
	Vector3 gravity_vector = Vector3(0, -1, 0);
	real_t area_linear_damp = 0.1;
	real_t area_angular_damp = 0.1;

	real_t last_step = 0.0;
	List<Box3DBody *> bodies;
	Box3DDirectSpaceState *direct_state = nullptr;

	void apply_gravity();
	void step(real_t p_delta);

	Box3DSpace();
	~Box3DSpace();
};

class Box3DDirectSpaceState : public PhysicsDirectSpaceState {
	GDCLASS(Box3DDirectSpaceState, PhysicsDirectSpaceState);

public:
	Box3DSpace *space = nullptr;

	// Queries land in M5. Until then they answer "nothing hit" rather than lie.
	virtual int intersect_point(const Vector3 &p_point, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false) { return 0; }
	virtual bool intersect_ray(const Vector3 &p_from, const Vector3 &p_to, RayResult &r_result, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, bool p_pick_ray = false) { return false; }
	virtual int intersect_shape(const RID &p_shape, const Transform &p_xform, float p_margin, ShapeResult *r_results, int p_result_max, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false) { return 0; }
	virtual bool cast_motion(const RID &p_shape, const Transform &p_xform, const Vector3 &p_motion, float p_margin, float &p_closest_safe, float &p_closest_unsafe, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false, ShapeRestInfo *r_info = nullptr) {
		p_closest_safe = 1.0f;
		p_closest_unsafe = 1.0f;
		return false;
	}
	virtual bool collide_shape(RID p_shape, const Transform &p_shape_xform, float p_margin, Vector3 *r_results, int p_result_max, int &r_result_count, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false) {
		r_result_count = 0;
		return false;
	}
	virtual bool rest_info(RID p_shape, const Transform &p_shape_xform, float p_margin, ShapeRestInfo *r_info, const Set<RID> &p_exclude = Set<RID>(), uint32_t p_collision_mask = 0xFFFFFFFF, bool p_collide_with_bodies = true, bool p_collide_with_areas = false) { return false; }
	virtual Vector3 get_closest_point_to_object_volume(RID p_object, const Vector3 p_point) const { return Vector3(); }
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

	// Contact reporting is M6.
	virtual int get_contact_count() const { return 0; }
	virtual Vector3 get_contact_local_position(int p_contact_idx) const { return Vector3(); }
	virtual Vector3 get_contact_local_normal(int p_contact_idx) const { return Vector3(); }
	virtual float get_contact_impulse(int p_contact_idx) const { return 0; }
	virtual int get_contact_local_shape(int p_contact_idx) const { return 0; }
	virtual RID get_contact_collider(int p_contact_idx) const { return RID(); }
	virtual Vector3 get_contact_collider_position(int p_contact_idx) const { return Vector3(); }
	virtual ObjectID get_contact_collider_id(int p_contact_idx) const { return 0; }
	virtual int get_contact_collider_shape(int p_contact_idx) const { return 0; }
	virtual Vector3 get_contact_collider_velocity_at_position(int p_contact_idx) const { return Vector3(); }

	virtual real_t get_step() const { return delta; }
	virtual PhysicsDirectSpaceState *get_space_state();
};

#endif // BOX3D_OBJECTS_H
