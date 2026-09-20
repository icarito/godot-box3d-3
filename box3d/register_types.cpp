#include "register_types.h"

#include "box3d_physics_server.h"

#include "core/class_db.h"

#ifndef _3D_DISABLED
PhysicsServer *_createBox3DPhysicsServerCallback() {
	return memnew(Box3DPhysicsServer);
}
#endif

void register_box3d_types() {
#ifndef _3D_DISABLED
	PhysicsServerManager::register_server("Box3D", &_createBox3DPhysicsServerCallback);
	// Registra la clase para que GDScript pueda llamar las perillas runtime
	// (worker count, substeps, warm starting, speculative, sleeping) sobre el
	// singleton PhysicsServer activo.
	ClassDB::register_class<Box3DPhysicsServer>();
#endif
}

void unregister_box3d_types() {
}
