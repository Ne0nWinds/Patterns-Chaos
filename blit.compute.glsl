#version 450
layout(local_size_x = 16, local_size_y = 16) in;

layout(set = 0, binding = 0, rgba8) uniform image2D OutputImage;
layout(set = 0, binding = 1) uniform ImageInfo {
	ivec2 ImageSize;
};

void main() {
	ivec2 texel = ivec2(gl_GlobalInvocationID.xy);

	if (texel.x >= ImageSize.x || texel.y >= ImageSize.y) {
		return;
	}

	imageStore(OutputImage, texel, vec4(texel.x / float(ImageSize.x), texel.y / float(ImageSize.y), 0.0, 1.0));
}
