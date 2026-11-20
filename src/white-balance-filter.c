/*
HDR White Balance for OBS
Copyright (C) 2026 SFame

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

/*
 * White balance (color temperature / tint) filter that works on both SDR and
 * HDR sources.
 *
 * OBS hands filters linear-light pixels in one of three color spaces:
 *   GS_CS_SRGB / GS_CS_SRGB_16F : linear Rec.709, 0..1 (SDR)
 *   GS_CS_709_EXTENDED          : linear Rec.709, 1.0 = SDR white, >1 = HDR
 * A white balance is a per-channel multiply in linear light, so the same
 * operation is valid in every one of these spaces. Unlike the built-in
 * Color Correction filter, this filter therefore does not skip HDR content.
 */

#include <obs-module.h>
#include <graphics/vec3.h>
#include <math.h>
#include <plugin-support.h>

#define S_INFO "info"
#define S_TEMPERATURE "temperature"
#define S_TINT "tint"
#define S_NORMALIZE "normalize"
#define S_RED "red_gain"
#define S_GREEN "green_gain"
#define S_BLUE "blue_gain"

#define REFERENCE_KELVIN 6500.0
#define MIN_KELVIN 2000
#define MAX_KELVIN 15000

enum wb_normalize {
	WB_NORMALIZE_LUMINANCE = 0,
	WB_NORMALIZE_NO_BOOST = 1,
	WB_NORMALIZE_NONE = 2,
};

struct wb_filter_data {
	obs_source_t *context;

	gs_effect_t *effect;
	gs_eparam_t *param_gain;

	struct vec3 gain;
};

/* CIE 1931 xy chromaticity of a black body at the given temperature.
 * Kim et al. (2002) cubic spline approximation, valid for 1667 K..25000 K. */
static void kelvin_to_xy(double kelvin, double *out_x, double *out_y)
{
	const double t = kelvin;
	const double t2 = t * t;
	const double t3 = t2 * t;
	double x;
	double y;

	if (t <= 4000.0)
		x = -0.2661239e9 / t3 - 0.2343589e6 / t2 + 0.8776956e3 / t + 0.179910;
	else
		x = -3.0258469e9 / t3 + 2.1070379e6 / t2 + 0.2226347e3 / t + 0.240390;

	const double x2 = x * x;
	const double x3 = x2 * x;

	if (t <= 2222.0)
		y = -1.1063814 * x3 - 1.34811020 * x2 + 2.18555832 * x - 0.20219683;
	else if (t <= 4000.0)
		y = -0.9549476 * x3 - 1.37418593 * x2 + 2.09137015 * x - 0.16748867;
	else
		y = 3.0817580 * x3 - 5.87338670 * x2 + 3.75112997 * x - 0.37001483;

	*out_x = x;
	*out_y = y;
}

/* Linear Rec.709 RGB of a black body white with luminance Y = 1. */
static void kelvin_to_rgb(double kelvin, double rgb[3])
{
	double x;
	double y;
	kelvin_to_xy(kelvin, &x, &y);

	const double X = x / y;
	const double Y = 1.0;
	const double Z = (1.0 - x - y) / y;

	rgb[0] = 3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
	rgb[1] = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
	rgb[2] = 0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;
}

static void compute_gain(struct vec3 *out, double kelvin, double tint, enum wb_normalize normalize, double red,
			 double green, double blue)
{
	double target[3];
	double reference[3];

	kelvin_to_rgb(kelvin, target);
	kelvin_to_rgb(REFERENCE_KELVIN, reference);

	/* Ratio against the same curve at 6500 K, so the default setting is an
	 * exact identity and only the change in temperature is applied. */
	double r = target[0] / reference[0];
	double g = target[1] / reference[1];
	double b = target[2] / reference[2];

	/* Tint: positive = magenta (less green), negative = green. */
	g *= pow(2.0, -tint / 100.0);

	r *= red;
	g *= green;
	b *= blue;

	double scale = 1.0;
	switch (normalize) {
	case WB_NORMALIZE_LUMINANCE: {
		const double luma = 0.2126 * r + 0.7152 * g + 0.0722 * b;
		if (luma > 0.0)
			scale = 1.0 / luma;
		break;
	}
	case WB_NORMALIZE_NO_BOOST: {
		double m = r;
		if (g > m)
			m = g;
		if (b > m)
			m = b;
		if (m > 0.0)
			scale = 1.0 / m;
		break;
	}
	case WB_NORMALIZE_NONE:
	default:
		break;
	}

	r *= scale;
	g *= scale;
	b *= scale;

	if (r < 0.0)
		r = 0.0;
	if (g < 0.0)
		g = 0.0;
	if (b < 0.0)
		b = 0.0;

	vec3_set(out, (float)r, (float)g, (float)b);
}

static const char *wb_filter_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("HdrWhiteBalance");
}

static void wb_filter_update(void *data, obs_data_t *settings)
{
	struct wb_filter_data *filter = data;

	const double kelvin = (double)obs_data_get_int(settings, S_TEMPERATURE);
	const double tint = obs_data_get_double(settings, S_TINT);
	const enum wb_normalize normalize = (enum wb_normalize)obs_data_get_int(settings, S_NORMALIZE);
	const double red = obs_data_get_double(settings, S_RED);
	const double green = obs_data_get_double(settings, S_GREEN);
	const double blue = obs_data_get_double(settings, S_BLUE);

	compute_gain(&filter->gain, kelvin, tint, normalize, red, green, blue);
}

static void *wb_filter_create(obs_data_t *settings, obs_source_t *context)
{
	struct wb_filter_data *filter = bzalloc(sizeof(*filter));
	char *effect_path = obs_module_file("white_balance.effect");

	filter->context = context;

	obs_enter_graphics();
	filter->effect = gs_effect_create_from_file(effect_path, NULL);
	obs_leave_graphics();

	bfree(effect_path);

	if (!filter->effect) {
		obs_log(LOG_ERROR, "failed to load white_balance.effect");
		bfree(filter);
		return NULL;
	}

	filter->param_gain = gs_effect_get_param_by_name(filter->effect, "gain");

	wb_filter_update(filter, settings);
	return filter;
}

static void wb_filter_destroy(void *data)
{
	struct wb_filter_data *filter = data;

	obs_enter_graphics();
	gs_effect_destroy(filter->effect);
	obs_leave_graphics();

	bfree(filter);
}

static const enum gs_color_space wb_potential_spaces[] = {
	GS_CS_SRGB,
	GS_CS_SRGB_16F,
	GS_CS_709_EXTENDED,
};

static void wb_filter_render(void *data, gs_effect_t *effect)
{
	UNUSED_PARAMETER(effect);

	struct wb_filter_data *filter = data;

	const enum gs_color_space source_space = obs_source_get_color_space(
		obs_filter_get_target(filter->context), OBS_COUNTOF(wb_potential_spaces), wb_potential_spaces);

	switch (source_space) {
	case GS_CS_SRGB:
	case GS_CS_SRGB_16F:
	case GS_CS_709_EXTENDED: {
		const enum gs_color_format format = gs_get_format_from_space(source_space);
		if (obs_source_process_filter_begin_with_color_space(filter->context, format, source_space,
								     OBS_ALLOW_DIRECT_RENDERING)) {
			gs_effect_set_vec3(filter->param_gain, &filter->gain);

			gs_blend_state_push();
			gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

			obs_source_process_filter_end(filter->context, filter->effect, 0, 0);

			gs_blend_state_pop();
		}
		break;
	}
	default:
		obs_source_skip_video_filter(filter->context);
		break;
	}
}

static enum gs_color_space wb_filter_get_color_space(void *data, size_t count,
						     const enum gs_color_space *preferred_spaces)
{
	UNUSED_PARAMETER(count);
	UNUSED_PARAMETER(preferred_spaces);

	struct wb_filter_data *const filter = data;
	return obs_source_get_color_space(obs_filter_get_target(filter->context), OBS_COUNTOF(wb_potential_spaces),
					  wb_potential_spaces);
}

static obs_properties_t *wb_filter_properties(void *data)
{
	UNUSED_PARAMETER(data);

	obs_properties_t *props = obs_properties_create();
	obs_property_t *p;

	obs_properties_add_text(props, S_INFO, obs_module_text("HdrWhiteBalance.Info"), OBS_TEXT_INFO);

	p = obs_properties_add_int_slider(props, S_TEMPERATURE, obs_module_text("HdrWhiteBalance.Temperature"),
					  MIN_KELVIN, MAX_KELVIN, 50);
	obs_property_int_set_suffix(p, " K");
	obs_property_set_long_description(p, obs_module_text("HdrWhiteBalance.Temperature.Description"));

	p = obs_properties_add_float_slider(props, S_TINT, obs_module_text("HdrWhiteBalance.Tint"), -50.0, 50.0, 0.5);
	obs_property_set_long_description(p, obs_module_text("HdrWhiteBalance.Tint.Description"));

	p = obs_properties_add_list(props, S_NORMALIZE, obs_module_text("HdrWhiteBalance.Normalize"),
				    OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(p, obs_module_text("HdrWhiteBalance.Normalize.Luminance"), WB_NORMALIZE_LUMINANCE);
	obs_property_list_add_int(p, obs_module_text("HdrWhiteBalance.Normalize.NoBoost"), WB_NORMALIZE_NO_BOOST);
	obs_property_list_add_int(p, obs_module_text("HdrWhiteBalance.Normalize.None"), WB_NORMALIZE_NONE);
	obs_property_set_long_description(p, obs_module_text("HdrWhiteBalance.Normalize.Description"));

	obs_properties_add_float_slider(props, S_RED, obs_module_text("HdrWhiteBalance.RedGain"), 0.5, 1.5, 0.005);
	obs_properties_add_float_slider(props, S_GREEN, obs_module_text("HdrWhiteBalance.GreenGain"), 0.5, 1.5, 0.005);
	obs_properties_add_float_slider(props, S_BLUE, obs_module_text("HdrWhiteBalance.BlueGain"), 0.5, 1.5, 0.005);

	return props;
}

static void wb_filter_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, S_TEMPERATURE, (long long)REFERENCE_KELVIN);
	obs_data_set_default_double(settings, S_TINT, 0.0);
	obs_data_set_default_int(settings, S_NORMALIZE, WB_NORMALIZE_LUMINANCE);
	obs_data_set_default_double(settings, S_RED, 1.0);
	obs_data_set_default_double(settings, S_GREEN, 1.0);
	obs_data_set_default_double(settings, S_BLUE, 1.0);
}

struct obs_source_info hdr_white_balance_filter = {
	.id = "hdr_white_balance_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_SOURCE_VIDEO | OBS_SOURCE_SRGB,
	.get_name = wb_filter_get_name,
	.create = wb_filter_create,
	.destroy = wb_filter_destroy,
	.update = wb_filter_update,
	.get_properties = wb_filter_properties,
	.get_defaults = wb_filter_defaults,
	.video_render = wb_filter_render,
	.video_get_color_space = wb_filter_get_color_space,
};
