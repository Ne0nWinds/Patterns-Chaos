#version 450
layout(local_size_x = 64) in;

#include "bindings.glsl.h"

void main() {
	uint idx = gl_GlobalInvocationID.x;
	vec2 position = Positions[idx];
	float angle = Angles[idx];

	vec2 direction = vec2(cos(angle), sin(angle));
	position += direction;

	Positions[gl_GlobalInvocationID.x] = position;

	vec4 color = vec4(1.0, 0.0, 0.0, 1.0);
	imageStore(OutputImage, ivec2(position), color);
}
