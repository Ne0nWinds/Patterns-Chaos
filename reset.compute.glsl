#version 450
layout(local_size_x = 128) in;

#include "bindings.glsl.h"
#include "shared_constants.h"

void main() {
	uint idx = gl_GlobalInvocationID.x;

	if (idx.x >= ParticleCount) {
		return;
	}

	uint random_seed = init_seed(idx);

	vec2 position = random_vec2(random_seed) * vec2(ImageSize.x, ImageSize.y);
	Positions[idx] = position;
	Angles[idx] = random(random_seed) * TWO_PI;

	uint width = (ImageSize.x + DENSITY_BUFFER_DOWNSCALE - 1) / DENSITY_BUFFER_DOWNSCALE;
	ivec2 index = ivec2(position / DENSITY_BUFFER_DOWNSCALE);
	atomicAdd(DensityField[index.y * width + index.x], 1u);
}
