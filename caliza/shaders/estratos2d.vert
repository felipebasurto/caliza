#version 450
// Caliza Estratos 2D vertex transform: screen-pixel geometry into clip space.
// SPDX-License-Identifier: GPL-3.0-or-later

layout(push_constant) uniform ScreenPush
{
	vec4 screen; // width, height, 1/width, 1/height
} pc;

layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in vec4 in_params;

layout(location = 0) out vec2 v_uv;
layout(location = 1) out vec4 v_color;
layout(location = 2) out vec4 v_params;

void main()
{
	v_uv = in_uv;
	v_color = in_color;
	v_params = in_params;
	vec2 clip = vec2(in_pos.x * pc.screen.z * 2.0 - 1.0, in_pos.y * pc.screen.w * 2.0 - 1.0);
	gl_Position = vec4(clip, 0.0, 1.0);
}
