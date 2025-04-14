#version 450
layout(local_size_x = 64) in;

#include "bindings.glsl.h"

void main() {
	uint idx = gl_GlobalInvocationID.x;

	if (idx.x >= ParticleCount) {
		return;
	}

	uint random_seed = init_seed(idx);

	Positions[idx] = random_vec2(random_seed) * vec2(ImageSize.x, ImageSize.y);
	Angles[idx] = random(random_seed) * TWO_PI;
}
