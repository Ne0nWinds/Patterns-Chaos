#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"
#include "shared_constants.h"

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	ivec2 position = ivec2(texel / DENSITY_BUFFER_DOWNSCALE);
	uint index = position.y * DensityBufferWidth + position.x;
	uint particle_count = DensityField[index];

	vec4 color = imageLoad(OutputImage, flip_y(texel));
	color.r = float(particle_count) / 4.0;
	imageStore(OutputImage, flip_y(texel), color);
}
