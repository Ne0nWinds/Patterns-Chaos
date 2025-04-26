#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"
#include "shared_constants.h"

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	vec4 value = imageLoad(OutputImage, texel);
	value.x *= 0.9525;
	value.x *= step(0.125, value.x);
	value.y = 0.0;

	uint clear_offset = bool(FrameNumber & 0x1) ? DensityBufferLength : 0;
	ivec2 position = ivec2(texel / DENSITY_BUFFER_DOWNSCALE);
	uint index = position.y * DensityBufferWidth + position.x;
	DensityField[clear_offset + index] = 0;

	imageStore(OutputImage, texel, value);
}
