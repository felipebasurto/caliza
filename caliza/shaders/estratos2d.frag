#version 450
// Caliza Estratos 2D primitives. params.w selects glyph coverage, analytic rounded
// rectangle coverage, sampled RGBA or unsampled solid geometry.
// SPDX-License-Identifier: GPL-3.0-or-later

layout(set = 0, binding = 0) uniform sampler2D primitive_tex;

layout(location = 0) in vec2 v_uv;
layout(location = 1) in vec4 v_color;
layout(location = 2) in vec4 v_params;
layout(location = 0) out vec4 out_color;

void main()
{
	// Keep the texture access outside the mode selection: some console shader compilers mishandle
	// coordinates for samples nested in divergent branches.
	vec4 texel = texture(primitive_tex, v_uv);
	float alpha = v_color.a;
	if (v_params.w < 0.5)
	{
		float field = texel.r;
		alpha *= smoothstep(v_params.x - v_params.y, v_params.x + v_params.y, field);
	}
	else if (v_params.w < 1.5)
	{
		vec2 inside = abs(v_uv) - v_params.xy + vec2(v_params.z);
		float dist = length(max(inside, vec2(0.0))) + min(max(inside.x, inside.y), 0.0) - v_params.z;
		alpha *= 1.0 - smoothstep(-0.8, 0.8, dist);
	}
	else if (v_params.w < 2.5)
	{
		out_color = texel * v_color;
		return;
	}
	out_color = vec4(v_color.rgb, alpha);
}
