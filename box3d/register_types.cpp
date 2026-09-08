#include "register_types.h"

#include "box3d_physics_server.h"

#ifndef _3D_DISABLED
PhysicsServer *_createBox3DPhysicsServerCallback() {
	return memnew(Box3DPhysicsServer);
}
#endif

void register_box3d_types() {
#ifndef _3D_DISABLED
	PhysicsServerManager::register_server("Box3D", &_createBox3DPhysicsServerCallback);
#endif
}

void unregister_box3d_types() {
}
