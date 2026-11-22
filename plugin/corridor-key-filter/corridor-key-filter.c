/* Corridor Key 2.0 - native OBS video filter.
 * Registers a filter named "Corridor Key 2.0" that appears next to the
 * built-in "Chroma Key Filter" in the filter list. Based on OBS's own
 * chroma-key-filter.c (obs-filters) pattern; keying math from CorridorKey
 * (brightness-invariant normalized-RGB distance + despill). CC BY-NC-SA.
 */
#include <obs-module.h>
#include <graphics/vec2.h>
#include <graphics/vec4.h>
#include <util/platform.h>

#define SETTING_KEY_COLOR   "key_color"
#define SETTING_SIMILARITY  "similarity"
#define SETTING_SMOOTHNESS  "smoothness"
#define SETTING_DESPILL     "despill_strength"
#define SETTING_DENOISE     "denoise"
#define SETTING_PLATE_MODE  "plate_mode"
#define SETTING_PLATE_TOLERANCE "plate_tolerance"
#define SETTING_PLATE_SOFTNESS "plate_softness"
#define SETTING_PLATE_DELAY "plate_auto_delay"

struct corridor_key_filter_data {
	obs_source_t *context;

	gs_effect_t *effect;

	gs_eparam_t *color_param;
	gs_eparam_t *uv_interval_param;
	gs_eparam_t *similarity_param;
	gs_eparam_t *smoothness_param;
	gs_eparam_t *despill_param;
	gs_eparam_t *denoise_param;
	gs_eparam_t *plate_param;
	gs_eparam_t *plate_tolerance_param;
	gs_eparam_t *plate_softness_param;

	struct vec4 color;
	float similarity;
	float smoothness;
	float despill;
	float denoise;

	/* Static background removal: one frame of the empty background
	 * ("plate") captured on demand, plus the difference-key settings. The
	 * plate lives in its own render target so its colour space matches the
	 * frame the key pass compares it against. */
	gs_texrender_t *plate_render;
	gs_texture_t *null_texture;

	bool plate_mode;
	bool plate_capture_requested;
	bool plate_clear_requested;
	int plate_auto_delay;
	/* Wall-clock deadline for the automatic plate capture (0 = disarmed).
	 * Filters are not ticked by libobs, so the countdown runs off the render
	 * callback instead of video_tick. */
	uint64_t plate_arm_ns;
	float plate_tolerance;
	float plate_softness;
};

static const char *corridor_key_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return "Corridor Key 2.0";
}

static inline void settings_update(struct corridor_key_filter_data *filter, obs_data_t *settings)
{
	uint32_t key_color = (uint32_t)obs_data_get_int(settings, SETTING_KEY_COLOR);

	/* OBS color ints are 0xAABBGGRR (R in the low byte): decode exactly like
	 * the built-in chroma key does. */
	vec4_from_rgba(&filter->color, key_color | 0xFF000000);

	filter->similarity = (float)obs_data_get_int(settings, SETTING_SIMILARITY) / 1000.0f;
	filter->smoothness = (float)obs_data_get_int(settings, SETTING_SMOOTHNESS) / 1000.0f;
	filter->despill = (float)obs_data_get_int(settings, SETTING_DESPILL) / 1000.0f;
	filter->denoise = (float)obs_data_get_double(settings, SETTING_DENOISE);

	filter->plate_mode = obs_data_get_bool(settings, SETTING_PLATE_MODE);
	filter->plate_tolerance = (float)obs_data_get_int(settings, SETTING_PLATE_TOLERANCE) / 1000.0f;
	filter->plate_softness = (float)obs_data_get_int(settings, SETTING_PLATE_SOFTNESS) / 1000.0f;
	filter->plate_auto_delay = (int)obs_data_get_int(settings, SETTING_PLATE_DELAY);
}

/* (Re)arm the auto-capture deadline, or disarm it when there is nothing to
 * capture (plate mode off, plate already captured, delay 0). */
static void plate_schedule_capture(struct corridor_key_filter_data *filter)
{
	if (filter->plate_mode && !filter->plate_render && filter->plate_auto_delay > 0)
		filter->plate_arm_ns =
			os_gettime_ns() + (uint64_t)filter->plate_auto_delay * 1000000000ULL;
	else
		filter->plate_arm_ns = 0;
}

static gs_texture_t *plate_texture(struct corridor_key_filter_data *filter)
{
	return filter->plate_render ? gs_texrender_get_texture(filter->plate_render) : NULL;
}

static void plate_release(struct corridor_key_filter_data *filter)
{
	if (!filter->plate_render)
		return;

	/* The effect keeps a raw pointer to the texture it was last given, and
	 * OBS re-applies every shader parameter on each draw. Point the parameter
	 * at a blank 1x1 texture before freeing the plate so the next draw can
	 * never bind a freed texture. */
	if (filter->plate_param) {
		if (!filter->null_texture) {
			static const uint8_t blank[4] = {0, 0, 0, 0};
			const uint8_t *blank_mip[1] = {blank};
			filter->null_texture = gs_texture_create(1, 1, GS_RGBA, 1, blank_mip, 0);
		}
		if (filter->null_texture)
			gs_effect_set_texture(filter->plate_param, filter->null_texture);
	}

	gs_texrender_destroy(filter->plate_render);
	filter->plate_render = NULL;
}

/* Grabs one frame of the filter input as the plate: the parent is rendered
 * through this filter's "Copy" technique into the plate's own render target,
 * using the same colour space as the key pass, so the stored frame and the
 * live frame are directly comparable in the shader. */
static void plate_capture(struct corridor_key_filter_data *filter, enum gs_color_format format,
			  enum gs_color_space space, uint32_t width, uint32_t height)
{
	struct vec4 clear_color;
	bool rendered;

	if (!width || !height)
		return;

	plate_release(filter);

	filter->plate_render = gs_texrender_create(format, GS_ZS_NONE);
	if (!filter->plate_render) {
		blog(LOG_WARNING, "[corridor-key] plate capture failed: no capture texture");
		return;
	}

	gs_matrix_push();
	if (!gs_texrender_begin_with_color_space(filter->plate_render, width, height, space)) {
		gs_matrix_pop();
		blog(LOG_WARNING, "[corridor-key] plate capture failed: cannot render to texture");
		plate_release(filter);
		return;
	}

	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, (float)width, 0.0f, (float)height, -100.0f, 100.0f);

	rendered = obs_source_process_filter_begin_with_color_space(filter->context, format, space,
								    OBS_NO_DIRECT_RENDERING);
	if (rendered)
		obs_source_process_filter_tech_end(filter->context, filter->effect, width, height, "Copy");

	gs_texrender_end(filter->plate_render);
	gs_matrix_pop();

	if (!rendered) {
		blog(LOG_WARNING, "[corridor-key] plate capture failed: filter bypassed, no frame read");
		plate_release(filter);
		return;
	}

	if (!plate_texture(filter)) {
		blog(LOG_WARNING, "[corridor-key] plate capture failed: empty capture texture");
		plate_release(filter);
		return;
	}

	blog(LOG_INFO, "[corridor-key] static background plate captured (%ux%u)", width, height);
}

static void corridor_key_update(void *data, obs_data_t *settings)
{
	struct corridor_key_filter_data *filter = data;

	settings_update(filter, settings);
	plate_schedule_capture(filter);
}

static void corridor_key_destroy(void *data)
{
	struct corridor_key_filter_data *filter = data;

	if (filter->effect || filter->plate_render || filter->null_texture) {
		obs_enter_graphics();

		plate_release(filter);

		if (filter->null_texture) {
			gs_texture_destroy(filter->null_texture);
			filter->null_texture = NULL;
		}

		if (filter->effect) {
			gs_effect_destroy(filter->effect);
			filter->effect = NULL;
		}

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
		filter->plate_param = gs_effect_get_param_by_name(filter->effect, "plate_image");
		filter->plate_tolerance_param =
			gs_effect_get_param_by_name(filter->effect, "plate_tolerance");
		filter->plate_softness_param =
			gs_effect_get_param_by_name(filter->effect, "plate_softness");

		/* Support evidence when a machine keys differently: which colour
		 * space libobs is rendering in for this session. */
		blog(LOG_INFO, "[corridor-key] ready (linear sRGB: %s)",
		     gs_get_linear_srgb() ? "on" : "off");
	}

	obs_leave_graphics();

	bfree(effect_path);

	/* Defaults first so a freshly created filter is usable immediately. */
	obs_data_set_default_int(settings, SETTING_KEY_COLOR, 0x00FF00);
	obs_data_set_default_int(settings, SETTING_SIMILARITY, 350);
	obs_data_set_default_int(settings, SETTING_SMOOTHNESS, 100);
	obs_data_set_default_int(settings, SETTING_DESPILL, 600);
	obs_data_set_default_double(settings, SETTING_DENOISE, 1.0);
	obs_data_set_default_bool(settings, SETTING_PLATE_MODE, false);
	obs_data_set_default_int(settings, SETTING_PLATE_TOLERANCE, 60);
	obs_data_set_default_int(settings, SETTING_PLATE_SOFTNESS, 60);
	obs_data_set_default_int(settings, SETTING_PLATE_DELAY, 5);

	if (!filter->effect) {
		corridor_key_destroy(filter);
		return NULL;
	}

	settings_update(filter, settings);
	plate_schedule_capture(filter);
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
		const char *tech_name = "Draw";
		gs_texture_t *plate_tex;

		if (filter->plate_clear_requested) {
			plate_release(filter);
			filter->plate_clear_requested = false;
			plate_schedule_capture(filter);
		}

		if (filter->plate_arm_ns && os_gettime_ns() >= filter->plate_arm_ns) {
			filter->plate_arm_ns = 0;
			filter->plate_capture_requested = true;
		}

		/* Capture before the key pass: both run one begin/end pair of their
		 * own, and the capture must read the unkeyed frame. */
		if (filter->plate_mode && filter->plate_capture_requested) {
			plate_capture(filter, format, source_space, width, height);
			filter->plate_capture_requested = false;
		}

		plate_tex = plate_texture(filter);
		if (filter->plate_mode)
			tech_name = plate_tex ? "DrawPlate" : "Copy";

		if (obs_source_process_filter_begin_with_color_space(filter->context, format, source_space,
								    OBS_ALLOW_DIRECT_RENDERING)) {
			vec2_set(&pixel_size, 1.0f / (float)width, 1.0f / (float)height);

			gs_effect_set_vec4(filter->color_param, &filter->color);
			gs_effect_set_vec2(filter->uv_interval_param, &pixel_size);
			gs_effect_set_float(filter->similarity_param, filter->similarity);
			gs_effect_set_float(filter->smoothness_param, filter->smoothness);
			gs_effect_set_float(filter->despill_param, filter->despill);
			gs_effect_set_float(filter->denoise_param, filter->denoise);

			if (plate_tex && filter->plate_param) {
				/* Read the plate raw. The capture pass writes the frame into
				 * its render target without an sRGB encode on either backend
				 * (measured on D3D11 and GL), so the stored values must not be
				 * decoded again: the live frame already arrives in whatever
				 * space libobs hands the shader. Binding an sRGB view here
				 * makes the plate darker than the live frame and nothing ever
				 * keys (that is exactly how 2.1.0 failed on Windows). */
				gs_effect_set_texture(filter->plate_param, plate_tex);

				gs_effect_set_float(filter->plate_tolerance_param, filter->plate_tolerance);
				gs_effect_set_float(filter->plate_softness_param, filter->plate_softness);
			}

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

			obs_source_process_filter_tech_end(filter->context, filter->effect, 0, 0, tech_name);

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

static bool plate_capture_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct corridor_key_filter_data *filter = data;

	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	if (filter) {
		filter->plate_clear_requested = false;
		filter->plate_capture_requested = true;
	}

	return true;
}

static bool plate_clear_clicked(obs_properties_t *props, obs_property_t *property, void *data)
{
	struct corridor_key_filter_data *filter = data;

	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	if (filter) {
		filter->plate_capture_requested = false;
		filter->plate_clear_requested = true;
		filter->plate_arm_ns = 0;
	}

	return true;
}

static obs_properties_t *corridor_key_properties(void *data)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_color(props, SETTING_KEY_COLOR, "Key color (sample your screen)");
	obs_properties_add_int_slider(props, SETTING_SIMILARITY, "Similarity", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_SMOOTHNESS, "Smoothness", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_DESPILL, "Despill strength", 0, 1000, 1);
	obs_properties_add_float_slider(props, SETTING_DENOISE, "Denoise (edge blur of key distance)", 0.0, 1.0, 0.01);

	obs_properties_add_bool(props, SETTING_PLATE_MODE,
				"Remove static background (captured plate)");
	obs_properties_add_int_slider(props, SETTING_PLATE_TOLERANCE, "Plate tolerance", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_PLATE_SOFTNESS, "Plate softness", 1, 1000, 1);
	obs_properties_add_int_slider(props, SETTING_PLATE_DELAY,
				      "Auto-capture delay (seconds, 0 = manual only)", 0, 30, 1);
	obs_properties_add_button2(props, "plate_capture", "Capture plate now (be out of frame)",
				  plate_capture_clicked, data);
	obs_properties_add_button2(props, "plate_clear", "Clear captured plate", plate_clear_clicked,
				  data);

	return props;
}

static struct obs_source_info corridor_key_filter = {
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
/* Module name must not contain '.': OBS strips a per-user plugin folder name
 * at its last '.', so "corridor-key-2.0-filter" would be looked up as
 * "corridor-key-2" and never load. Must equal buildspec.json "name". */
OBS_MODULE_USE_DEFAULT_LOCALE("corridor-key-filter", "en-US")

MODULE_EXPORT const char *obs_module_description(void)
{
	return "Corridor Key 2.0 chroma key filter";
}

bool obs_module_load(void)
{
	obs_register_source(&corridor_key_filter);
	return true;
}
