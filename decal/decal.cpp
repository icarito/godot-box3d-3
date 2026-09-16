#include "decal.h"

#include "servers/visual_server.h"

void Decal::set_texture(DecalTexture p_texture, const Ref<Texture> &p_texture_ref) {
	ERR_FAIL_INDEX(p_texture, 4);
	textures[p_texture] = p_texture_ref;

	RID tex_rid = p_texture_ref.is_valid() ? p_texture_ref->get_rid() : RID();
	VS::get_singleton()->decal_set_texture(decal, VisualServer::DecalTexture(p_texture), tex_rid);

	update_gizmo();
}

Ref<Texture> Decal::get_texture(DecalTexture p_texture) const {
	ERR_FAIL_INDEX_V(p_texture, 4, Ref<Texture>());
	return textures[p_texture];
}

void Decal::set_size(const Vector3 &p_size) {
	size = Vector3(MAX(p_size.x, 0.01), MAX(p_size.y, 0.01), MAX(p_size.z, 0.01));
	VS::get_singleton()->decal_set_size(decal, size);
	update_gizmo();
	_change_notify("size");
}

Vector3 Decal::get_size() const {
	return size;
}

void Decal::set_emission_energy(float p_energy) {
	emission_energy = p_energy;
	VS::get_singleton()->decal_set_emission_energy(decal, p_energy);
}

float Decal::get_emission_energy() const {
	return emission_energy;
}

void Decal::set_albedo_mix(float p_mix) {
	albedo_mix = p_mix;
	VS::get_singleton()->decal_set_albedo_mix(decal, p_mix);
}

float Decal::get_albedo_mix() const {
	return albedo_mix;
}

void Decal::set_modulate(const Color &p_modulate) {
	modulate = p_modulate;
	VS::get_singleton()->decal_set_modulate(decal, p_modulate);
}

Color Decal::get_modulate() const {
	return modulate;
}

void Decal::set_upper_fade(float p_amount) {
	upper_fade = p_amount;
	VS::get_singleton()->decal_set_upper_fade(decal, p_amount);
}

float Decal::get_upper_fade() const {
	return upper_fade;
}

void Decal::set_lower_fade(float p_amount) {
	lower_fade = p_amount;
	VS::get_singleton()->decal_set_lower_fade(decal, p_amount);
}

float Decal::get_lower_fade() const {
	return lower_fade;
}

void Decal::set_normal_fade(float p_fade) {
	normal_fade = p_fade;
	VS::get_singleton()->decal_set_normal_fade(decal, p_fade);
}

float Decal::get_normal_fade() const {
	return normal_fade;
}

void Decal::set_cull_mask(uint32_t p_layers) {
	cull_mask = p_layers;
	VS::get_singleton()->decal_set_cull_mask(decal, p_layers);
}

uint32_t Decal::get_cull_mask() const {
	return cull_mask;
}

void Decal::set_distance_fade_enabled(bool p_enabled) {
	distance_fade_enabled = p_enabled;
	VS::get_singleton()->decal_set_distance_fade(decal, distance_fade_enabled, distance_fade_near, distance_fade_far, distance_fade_transitional);
	_change_notify();
}

bool Decal::is_distance_fade_enabled() const {
	return distance_fade_enabled;
}

void Decal::set_distance_fade_near(float p_distance) {
	distance_fade_near = p_distance;
	VS::get_singleton()->decal_set_distance_fade(decal, distance_fade_enabled, distance_fade_near, distance_fade_far, distance_fade_transitional);
}

float Decal::get_distance_fade_near() const {
	return distance_fade_near;
}

void Decal::set_distance_fade_far(float p_distance) {
	distance_fade_far = p_distance;
	VS::get_singleton()->decal_set_distance_fade(decal, distance_fade_enabled, distance_fade_near, distance_fade_far, distance_fade_transitional);
}

float Decal::get_distance_fade_far() const {
	return distance_fade_far;
}

void Decal::set_distance_fade_transitional(float p_distance) {
	distance_fade_transitional = p_distance;
	VS::get_singleton()->decal_set_distance_fade(decal, distance_fade_enabled, distance_fade_near, distance_fade_far, distance_fade_transitional);
}

float Decal::get_distance_fade_transitional() const {
	return distance_fade_transitional;
}

AABB Decal::get_aabb() const {
	return AABB(-size / 2.0, size);
}

PoolVector<Face3> Decal::get_faces(uint32_t p_usage_flags) const {
	return PoolVector<Face3>();
}

void Decal::_update_visibility() {
	if (!is_inside_tree()) {
		return;
	}

	VS::get_singleton()->instance_set_visible(get_instance(), is_visible_in_tree());
}

void Decal::_notification(int p_what) {
	if (p_what == NOTIFICATION_VISIBILITY_CHANGED || p_what == NOTIFICATION_ENTER_TREE) {
		_update_visibility();
	}
}

void Decal::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_texture", "texture", "texture_ref"), &Decal::set_texture);
	ClassDB::bind_method(D_METHOD("get_texture", "texture"), &Decal::get_texture);

	ClassDB::bind_method(D_METHOD("set_size", "size"), &Decal::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &Decal::get_size);

	ClassDB::bind_method(D_METHOD("set_emission_energy", "energy"), &Decal::set_emission_energy);
	ClassDB::bind_method(D_METHOD("get_emission_energy"), &Decal::get_emission_energy);

	ClassDB::bind_method(D_METHOD("set_albedo_mix", "albedo_mix"), &Decal::set_albedo_mix);
	ClassDB::bind_method(D_METHOD("get_albedo_mix"), &Decal::get_albedo_mix);

	ClassDB::bind_method(D_METHOD("set_modulate", "modulate"), &Decal::set_modulate);
	ClassDB::bind_method(D_METHOD("get_modulate"), &Decal::get_modulate);

	ClassDB::bind_method(D_METHOD("set_upper_fade", "amount"), &Decal::set_upper_fade);
	ClassDB::bind_method(D_METHOD("get_upper_fade"), &Decal::get_upper_fade);

	ClassDB::bind_method(D_METHOD("set_lower_fade", "amount"), &Decal::set_lower_fade);
	ClassDB::bind_method(D_METHOD("get_lower_fade"), &Decal::get_lower_fade);

	ClassDB::bind_method(D_METHOD("set_normal_fade", "fade"), &Decal::set_normal_fade);
	ClassDB::bind_method(D_METHOD("get_normal_fade"), &Decal::get_normal_fade);

	ClassDB::bind_method(D_METHOD("set_cull_mask", "cull_mask"), &Decal::set_cull_mask);
	ClassDB::bind_method(D_METHOD("get_cull_mask"), &Decal::get_cull_mask);

	ClassDB::bind_method(D_METHOD("set_distance_fade_enabled", "enabled"), &Decal::set_distance_fade_enabled);
	ClassDB::bind_method(D_METHOD("is_distance_fade_enabled"), &Decal::is_distance_fade_enabled);

	ClassDB::bind_method(D_METHOD("set_distance_fade_near", "distance"), &Decal::set_distance_fade_near);
	ClassDB::bind_method(D_METHOD("get_distance_fade_near"), &Decal::get_distance_fade_near);

	ClassDB::bind_method(D_METHOD("set_distance_fade_far", "distance"), &Decal::set_distance_fade_far);
	ClassDB::bind_method(D_METHOD("get_distance_fade_far"), &Decal::get_distance_fade_far);

	ClassDB::bind_method(D_METHOD("set_distance_fade_transitional", "distance"), &Decal::set_distance_fade_transitional);
	ClassDB::bind_method(D_METHOD("get_distance_fade_transitional"), &Decal::get_distance_fade_transitional);

	ADD_GROUP("Decal", "");
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "texture_albedo", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_texture", "get_texture", TEXTURE_ALBEDO);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "texture_normal", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_texture", "get_texture", TEXTURE_NORMAL);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "texture_orm", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_texture", "get_texture", TEXTURE_ORM);
	ADD_PROPERTYI(PropertyInfo(Variant::OBJECT, "texture_emission", PROPERTY_HINT_RESOURCE_TYPE, "Texture"), "set_texture", "get_texture", TEXTURE_EMISSION);
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "albedo_mix", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_albedo_mix", "get_albedo_mix");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "modulate"), "set_modulate", "get_modulate");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "emission_energy", PROPERTY_HINT_RANGE, "0,16,0.001,or_greater"), "set_emission_energy", "get_emission_energy");
	ADD_GROUP("Cull Mask", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cull_mask", PROPERTY_HINT_LAYERS_3D_RENDER), "set_cull_mask", "get_cull_mask");
	ADD_GROUP("Fades", "");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "upper_fade", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_upper_fade", "get_upper_fade");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "lower_fade", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_lower_fade", "get_lower_fade");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "normal_fade", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_normal_fade", "get_normal_fade");
	ADD_GROUP("Distance Fade", "distance_fade_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "distance_fade_enabled"), "set_distance_fade_enabled", "is_distance_fade_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "distance_fade_near", PROPERTY_HINT_RANGE, "0,8192,0.001,or_greater"), "set_distance_fade_near", "get_distance_fade_near");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "distance_fade_far", PROPERTY_HINT_RANGE, "0,8192,0.001,or_greater"), "set_distance_fade_far", "get_distance_fade_far");
	ADD_PROPERTY(PropertyInfo(Variant::REAL, "distance_fade_transitional", PROPERTY_HINT_RANGE, "0,8192,0.001,or_greater"), "set_distance_fade_transitional", "get_distance_fade_transitional");

	BIND_ENUM_CONSTANT(TEXTURE_ALBEDO);
	BIND_ENUM_CONSTANT(TEXTURE_NORMAL);
	BIND_ENUM_CONSTANT(TEXTURE_ORM);
	BIND_ENUM_CONSTANT(TEXTURE_EMISSION);
}

Decal::Decal() {
	decal = VisualServer::get_singleton()->decal_create();
	set_base(decal);

	size = Vector3(2, 2, 2);
	emission_energy = 1.0;
	albedo_mix = 1.0;
	modulate = Color(1, 1, 1, 1);
	upper_fade = 0.3;
	lower_fade = 0.3;
	normal_fade = 0.0;
	cull_mask = 0xFFFFFFFF;
	distance_fade_enabled = false;
	distance_fade_near = 0.0;
	distance_fade_far = 10.0;
	distance_fade_transitional = 1.0;

	VS::get_singleton()->decal_set_size(decal, size);
	VS::get_singleton()->decal_set_emission_energy(decal, emission_energy);
	VS::get_singleton()->decal_set_albedo_mix(decal, albedo_mix);
	VS::get_singleton()->decal_set_modulate(decal, modulate);
	VS::get_singleton()->decal_set_upper_fade(decal, upper_fade);
	VS::get_singleton()->decal_set_lower_fade(decal, lower_fade);
	VS::get_singleton()->decal_set_normal_fade(decal, normal_fade);
	VS::get_singleton()->decal_set_cull_mask(decal, cull_mask);
	VS::get_singleton()->decal_set_distance_fade(decal, distance_fade_enabled, distance_fade_near, distance_fade_far, distance_fade_transitional);

	set_disable_scale(true);
}

Decal::~Decal() {
	VS::get_singleton()->instance_set_base(get_instance(), RID());

	if (decal.is_valid()) {
		VisualServer::get_singleton()->free(decal);
	}
}
