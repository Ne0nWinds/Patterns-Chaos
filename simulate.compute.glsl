#version 450
layout(local_size_x = 128) in;

#include "shared_constants.h"
#include "bindings.glsl.h"

float deg2rad(float degrees) {
    return degrees * 0.017453292519943295; // π / 180
}

void main() {

	uint idx = gl_GlobalInvocationID.x;
	uint write_offset = bool(FrameNumber & 0x1) ? 0 : MAX_PARTICLE_COUNT;
	uint read_offset =  bool(FrameNumber & 0x1) ? MAX_PARTICLE_COUNT : 0;

	vec2 position = Positions[idx + read_offset];
	float angle = Angles[idx + read_offset];
	vec2 direction = vec2(cos(angle), sin(angle));

#if 1
	Positions[idx + write_offset] = position;
	Angles[idx + write_offset] = angle;
#else
	float left = 0.0;
	float right = 0.0;

	for (uint i = 0; i < ParticleCount; ++i) {
		if (i == idx) continue;

		vec2 other_position = Positions[i + read_offset];
		if (distance(position, other_position) < 30.0) {

			// counter-clockwise rotation by 90 degrees
			vec2 rotated_direction = vec2(-direction.y, direction.x);

			if (dot(other_position - position, rotated_direction) > 0.0) {
				left += 1.0;
			} else {
				right += 1.0;
			}
		}
	}

	float alpha = deg2rad(180.0);
	float beta = deg2rad(17.0);
	angle -= alpha + beta * (left + right) * sign(right - left);
	direction = vec2(cos(angle), sin(angle));

	direction *= 0.67;
	position = mod(position + direction, vec2(ImageSize.x, ImageSize.y));

	Positions[idx + write_offset] = position;
	Angles[idx + write_offset] = angle;
#endif

	vec4 color = vec4(0.0, 1.0, 0.0, 1.0);
	imageStore(OutputImage, flip_y(ivec2(position)), color);
}
