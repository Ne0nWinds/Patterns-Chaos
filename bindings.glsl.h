
#define PI 3.14159265358979323846
#define TWO_PI 6.28318530717958647692

layout(set = 0, binding = 0, rgba8) uniform image2D OutputImage;
layout(set = 0, binding = 1) uniform BoundUniforms {
	ivec2 ImageSize;
	uint ParticleCount;
};
layout(set = 0, binding = 2, std430) buffer PositionBuffer {
	vec2 Positions[];
};
layout(set = 0, binding = 3, std430) buffer AngleBuffer {
	float Angles[];
};

float random(inout uint state) {

	uint lcg_result = state * 747796405u + 2891336453u;
	uint hashed_state = lcg_result ^ (lcg_result >> 14);
	state = hashed_state;

#if 0
	// bit cast to maximally use the bits in the mantissa
	uint bits = floatBitsToUint(1.0) | (hashed_state >> 9); // [1.0, 2.0)
	float result = uintBitsToFloat(bits) - 1.0; // [0.0, 1.0)
#else
	const float InvMaxInt = 1.0 / 16777216.0;
	float result = float(hashed_state >> 8) * InvMaxInt;
#endif

	return result;
}

uint init_seed(uint idx) {
	uint result = idx;
	for (uint i = 0; i < 3; ++i) {
		result = result * 2654435761u + 1692572869u;
		result = result ^ (result >> 18);
	}
	return result;
}

vec2 random_vec2(inout uint seed) {
	float x = random(seed);
	float y = random(seed);
	return vec2(x, y);
}
