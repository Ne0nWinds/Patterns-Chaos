#version 450
layout(local_size_x = 128) in;

#include "shared_constants.h"
#include "bindings.glsl.h"

float deg2rad(float degrees) {
    return degrees * 0.017453292519943295; // π / 180
}

void main() {

	uint idx = gl_GlobalInvocationID.x;

	if (idx >= ParticleCount) {
		return;
	}

	uint write_offset = bool(FrameNumber & 0x1) ? DensityBufferLength : 0;
	uint read_offset =  bool(FrameNumber & 0x1) ? 0 : DensityBufferLength;

	vec2 position = Positions[idx];
	float angle = Angles[idx];

	vec2 direction = vec2(cos(angle), sin(angle));

	int left = 0;
	int right = 0;
	int search_radius = 128;
	ivec2 density_buffer_position = ivec2(position / DENSITY_BUFFER_DOWNSCALE);

	for (int y = -search_radius; y <= search_radius; ++y) {
		for (int x = -search_radius; x <= search_radius; ++x) {
			if (y*y + x*x > search_radius) continue;

			int is_self = (y == 0 && x == 0) ? 1 : 0;

			ivec2 DensityBufferDimensions = ivec2(DensityBufferWidth, DensityBufferHeight);
			ivec2 tile = density_buffer_position + ivec2(x, y);
			tile = (tile + DensityBufferDimensions) % DensityBufferDimensions;
			uint index = read_offset + (tile.y * DensityBufferWidth + tile.x);
			uint particle_count = DensityField[index];
			particle_count -= is_self;

			// counter-clockwise rotation by 90 degrees
			vec2 rotated_direction = vec2(-direction.y, direction.x);
			if (dot(vec2(x, y), rotated_direction) > 0.0) {
				left += int(particle_count);
			} else {
				right += int(particle_count);
			}
		}
	}

	float alpha = deg2rad(5.0);
	float beta = deg2rad(12.0);
	float count = float(left + right);
	angle -= alpha + beta * count * sign(right - left);
	direction = vec2(cos(angle), sin(angle));

	position = mod(position + direction, vec2(ImageSize.x, ImageSize.y));
	Positions[idx] = position;
	Angles[idx] = angle;

	vec4 color = vec4(0.0, 1.0, 0.0, 1.0);
	imageStore(OutputImage, flip_y(ivec2(position)), color);

	{
		ivec2 rounded_pos = ivec2(position / DENSITY_BUFFER_DOWNSCALE);
		uint index = rounded_pos.y * DensityBufferWidth + rounded_pos.x;
		atomicAdd(DensityField[write_offset + index], 1u);
	}
}
