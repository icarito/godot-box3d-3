extends RigidBody

# Stores what the direct state reports each integration, for the main test
# script to inspect.

func _integrate_forces(state):
	if state.get_contact_count() > 0:
		var data = {
			"count": state.get_contact_count(),
			"normal": state.get_contact_local_normal(0),
			"position": state.get_contact_local_position(0),
			"collider_position": state.get_contact_collider_position(0),
			"impulse": state.get_contact_impulse(0),
			"local_shape": state.get_contact_local_shape(0),
			"collider": state.get_contact_collider(0),
			"collider_shape": state.get_contact_collider_shape(0),
			"total_gravity": state.get_total_gravity(),
		}
		set_meta("contacts", data)
