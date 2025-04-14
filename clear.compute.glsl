#version 450
layout(local_size_x = 16, local_size_y = 16) in;

#include "bindings.glsl.h"

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	vec3 color = vec3(0.0);
	imageStore(OutputImage, texel, vec4(color, 1.0));
}
