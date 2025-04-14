#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	vec4 value = imageLoad(OutputImage, texel);
	value.xyz *= 0.75; 
	value.xyz *= step(1.0 / 256.0, value.xyz);
	imageStore(OutputImage, texel, value);
}
