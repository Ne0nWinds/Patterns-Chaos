#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"
#include "shared_constants.h"

uint RoundUpPowerOf2(uint N, uint Multiple) {
	uint MultipleMinusOne = Multiple - 1;
	uint Mask = ~MultipleMinusOne;
	uint Result = (N + MultipleMinusOne) & Mask;
	return Result;
}

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	// vec4 value = imageLoad(OutputImage, texel);
	// value.xyz *= 1.0 - (1.0 / 32.0); 
	// value.xyz *= step(0.125, value.xyz);
	vec4 value = vec4(0.0);

	uint width = (ImageSize.x + (DENSITY_BUFFER_DOWNSCALE - 1)) / DENSITY_BUFFER_DOWNSCALE;
	uint index = texel.y/DENSITY_BUFFER_DOWNSCALE * width + texel.x/DENSITY_BUFFER_DOWNSCALE;

	uint particle_count = DensityField[index];
	value.x = float(particle_count) / 4.0;

	imageStore(OutputImage, flip_y(texel), value);
}
