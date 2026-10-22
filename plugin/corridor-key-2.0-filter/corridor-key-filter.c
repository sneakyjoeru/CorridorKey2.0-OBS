/* Corridor Key 2.0 - native OBS video filter.
 * Registers a filter named "Corridor Key 2.0" that appears next to the
 * built-in "Chroma Key Filter" in the filter list. Based on OBS's own
 * chroma-key-filter.c (obs-filters) pattern; keying math from CorridorKey
 * (brightness-invariant normalized-RGB distance + despill). CC BY-NC-SA.
 */
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>

#define SETTING_KEY_COLOR   "key_color"
#define SETTING_SIMILARITY  "similarity"
#define SETTING_SMOOTHNESS  "smoothness"
#define SETTING_DESPILL     "despill_strength"
#define SETTING_DENOISE     "denoise"

struct corridor_key_filter_data {
	obs_source_t *context;

	gs_effect_t *effect;

	gs_eparam_t *color_param;
	gs_eparam_t *uv_interval_param;
	gs_eparam_t *similarity_param;
	gs_eparam_t *smoothness_param;
	gs_eparam_t *despill_param;
	gs_eparam_t *denoise_param;

	struct vec4 color;
	float similarity;
	float smoothness;
	float despill;
	float denoise;
};

static const char *corridor_key_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Corridor Key 2.0";
}

static inline void settings_update(struct corridor_key_filter_data *filter, obs_data_t *settings)
{
	uint32_t key_color = (uint32_t)obs_data_get_int(settings, SETTING_KEY_COLOR);

	/* OBS stores colors as RRGGBB hex ints; build a 0-1 sRGB vec4. */
	filter->color.x = ((key_color >> 16) & 0xFF) / 255.0f;
	filter->color.y = ((key_color >> 8) & 0xFF) / 255.0f;
	filter->color.z = (key_color & 0xFF) / 255.0f;
	filter->color.w = 1.0f;

	filter->similarity = (float)obs_data_get_int(settings, SETTING_SIMILARITY) / 1000.0f;
	filter->smoothness = (float)obs_data_get_int(settings, SETTING_SMOOTHNESS) / 1000.0f;
	filter->despill = (float)obs_data_get_int(settings, SETTING_DESPILL) / 1000.0f;
	filter->denoise = (float)obs_data_get_double(settings, SETTING_DENOISE);
}

static void corridor_key_update(void *data, obs_data_t *settings)
{
	struct corridor_key_filter_data *filter = data;

	settings_update(filter, settings);
}

static void corridor_key_destroy(void *data)
{
	struct corridor_key_filter_data *filter = data;

	if (filter->effect) {
		obs_enter_graphics();
		gs_effect_destroy(filter->effect);
		obs_leave_graphics();
	}

	bfree(data);
}

static void *corridor_key_create(obs_data_t *settings, obs_source_t *context)
{
	struct corridor_key_filter_data *filter =
		bzalloc(sizeof(struct corridor_key_filter_data));
	char *effect_path = obs_module_file("corridor_key_2.effect");

	filter->context = context;

	obs_enter_graphics();

	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	if (filter->effect) {
		filter->color_param = gs_effect_get_param_by_name(filter->effect, "key_color");
		filter->uv_interval_param = gs_effect_get_param_by_name(filter->effect, "uv_pixel_interval");
		filter->similarity_param = gs_effect_get_param_by_name(filter->effect, "similarity");
	 filter->smoothness_param = gs_effect_get_param_by_name(filter->effect, "smoothness");
	 filter->despill_param = gs_effect_get_param_by_name(filter->effect, "despill_strength");
	 filter->denoise_param = gs_effect_get_param_by_name(filter->effect, "denoise");
	}

	obs_leave_graphics();

	bfree(effect_path);

	/* Defaults first so a freshly created filter is usable immediately. */
	obs_data_set_default_int(settings, SETTING_KEY_COLOR, 0x00FF00);
	obs_data_set_default_int(settings, SETTING_SIMILARITY, 350);
	obs_data_set_default_int(settings, SETTING_SMOOTHNESS, 100);
	obs_data_set_default_int(settings, SETTING_DESPILL, 600);
	obs_data_set_default_double(settings, SETTING_DENOISE, 1.0);

	if (!filter->effect) {
		corridor_key_destroy(filter);
		return NULL;
	}

	settings_update(filter, settings);
	return filter;
}

static void corridor_key_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct corridor_key_filter_data *filter = data;
	obs_source_t *target = obs_filter_get_target(filter->context);
	uint32_t width = obs_source_get_base_width(target);
	uint32_t height = obs_source_get_base_height(target);
	struct vec2 pixel_size;

	const enum gs_color_space preferred_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->context), OBS_COUNTOF(preferred_spaces), preferred_spaces);
	if (source_space == GS_CS_709_EXTENDED) {
		obs_source_skip_video_filter(filter->context);
	} else {
		const enum gs_color_format format = gs_get_format_from_space(source_space);
		if (obs_source_process_filter_begin_with_color_space(filter->context, format, source_space,
								    OBS_ALLOW_DIRECT_RENDERING)) {
			vec2_set(&pixel_size, 1.0f / (float)width, 1.0f / (float)height);

			gs_effect_set_vec4(filter->color_param, &filter->color);
			gs_effect_set_vec2(filter->uv_interval_param, &pixel_size);
			gs_effect_set_float(filter->similarity_param, filter->similarity);
			gs_effect_set_float(filter->smoothness_param, filter->smoothness);
			gs_effect_set_float(filter->despill_param, filter->despill);
			gs_effect_set_float(filter->denoise_param, filter->denoise);

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

			obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

			gs_blend_state_pop();
		}
	}
}

static enum gs_color_space corridor_key_get_color_space(void *data, size_t count,
                                                       const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);

	struct corridor_key_filter_data *const filter = data;
	const enum gs_color_space potential_spaces[] = {
		GS_CS_SRGB,
		GS_CS_SRGB_16F,
		GS_CS_709_EXTENDED,
	};

	return obs_source_get_color_space(obs_filter_get_target(filter->context),
	 OBS_COUNTOF(potential_spaces), potential_spaces);
}

static obs_properties_t *corridor_key_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_color(props, SETTING_KEY_COLOR, "Key color (sample your screen)");
	obs_properties_add_int_slider(props, SETTING_SIMILARITY, "Similarity", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_SMOOTHNESS, "Smoothness", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_DESPILL, "Despill strength", 0, 1000, 1);
	obs_properties_add_float_slider(props, SETTING_DENOISE, "Denoise (edge blur of key distance)", 0.0, 1.0, 0.01);

	UNUSED_PARAMETER(data);
	return props;
}

struct obs_source_info corridor_key_filter = {
	.id = "corridor_key_2_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = corridor_key_name,
	.create = corridor_key_create,
	.destroy = corridor_key_destroy,
	.video_render = corridor_key_render,
	.update = corridor_key_update,
	.get_properties = corridor_key_properties,
	.video_get_color_space = corridor_key_get_color_space,
};

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("corridor-key-2.0-filter", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Corridor Key 2.0 chroma key filter";
}

bool obs_module_load(void)
{
	obs_register_source(&corridor_key_filter);
	return true;
}
