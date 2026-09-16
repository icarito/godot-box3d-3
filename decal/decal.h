/**************************************************************************/
/*  decal.h                                                               */
/**************************************************************************/
/* Project-level Decal node for the GLES3 decal backport.                 */
/* Lives as a module; the renderer support it relies on is provided by    */
/* the feature_decal_gles3 engine patch applied by scripts/build.sh.      */
/**************************************************************************/

#ifndef DECAL_H
#define DECAL_H

#include "scene/3d/visual_instance.h"

class Decal : public VisualInstance {
	GDCLASS(Decal, VisualInstance);

public:
	enum DecalTexture {
		TEXTURE_ALBEDO = VisualServer::DECAL_TEXTURE_ALBEDO,
		TEXTURE_NORMAL = VisualServer::DECAL_TEXTURE_NORMAL,
		TEXTURE_ORM = VisualServer::DECAL_TEXTURE_ORM,
		TEXTURE_EMISSION = VisualServer::DECAL_TEXTURE_EMISSION
	};

private:
	RID decal;
	Ref<Texture> textures[4];
	Vector3 size;
	float emission_energy;
	float albedo_mix;
	Color modulate;
	float upper_fade;
	float lower_fade;
	float normal_fade;
	uint32_t cull_mask;
	bool distance_fade_enabled;
	float distance_fade_near;
	float distance_fade_far;
	float distance_fade_transitional;

protected:
	static void _bind_methods();
	void _notification(int p_what);
	void _update_visibility();

public:
	void set_texture(DecalTexture p_texture, const Ref<Texture> &p_texture_ref);
	Ref<Texture> get_texture(DecalTexture p_texture) const;

	void set_size(const Vector3 &p_size);
	Vector3 get_size() const;

	void set_emission_energy(float p_energy);
	float get_emission_energy() const;

	void set_albedo_mix(float p_mix);
	float get_albedo_mix() const;

	void set_modulate(const Color &p_modulate);
	Color get_modulate() const;

	void set_upper_fade(float p_amount);
	float get_upper_fade() const;

	void set_lower_fade(float p_amount);
	float get_lower_fade() const;

	void set_normal_fade(float p_fade);
	float get_normal_fade() const;

	void set_cull_mask(uint32_t p_layers);
	uint32_t get_cull_mask() const;

	void set_distance_fade_enabled(bool p_enabled);
	bool is_distance_fade_enabled() const;

	void set_distance_fade_near(float p_distance);
	float get_distance_fade_near() const;

	void set_distance_fade_far(float p_distance);
	float get_distance_fade_far() const;

	void set_distance_fade_transitional(float p_distance);
	float get_distance_fade_transitional() const;

	virtual AABB get_aabb() const;
	virtual PoolVector<Face3> get_faces(uint32_t p_usage_flags) const;

	Decal();
	~Decal();
};

VARIANT_ENUM_CAST(Decal::DecalTexture);

#endif // DECAL_H
