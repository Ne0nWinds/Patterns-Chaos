#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	vec4 value = imageLoad(OutputImage, texel);
	value.xyz *= 1.0 - (1.0 / 32.0); 
	value.xyz *= step(0.125, value.xyz);
	imageStore(OutputImage, texel, value);
}
