#version 450
layout(local_size_x = 128) in;

#include "shared_constants.h"
#include "bindings.glsl.h"

float deg2rad(float degrees) {
    return degrees * 0.017453292519943295; // π / 180
}

void main() {

	uint idx = gl_GlobalInvocationID.x;

	if (idx >= ParticleCount) {
		return;
	}

	vec2 position = Positions[idx];
	float angle = Angles[idx];

	vec2 direction = vec2(0.125);
	position = mod(position + direction, vec2(ImageSize.x, ImageSize.y));
	Positions[idx] = position;

	vec4 color = vec4(0.0, 1.0, 0.0, 1.0);
	imageStore(OutputImage, flip_y(ivec2(position)), color);

	{
		ivec2 rounded_pos = ivec2(position / DENSITY_BUFFER_DOWNSCALE);
		uint index = rounded_pos.y * DensityBufferWidth + rounded_pos.x;
		atomicAdd(DensityField[index], 1u);
	}
}
