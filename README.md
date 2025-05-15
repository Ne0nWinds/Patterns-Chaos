
This is an implementation of a Primordial Particle System described this paper:
_How a life-like system emerges from a simple particle motion law_ by Thomas Schmickl, Martin Stefanec, and Karl Crailsheim (Scientific Reports, 2016).

https://github.com/user-attachments/assets/fd531808-e73b-4d54-9e85-7e6428d999e2

It's implemented with Vulkan compute shaders and scales to several million particles in real time.

Because the original algorithm requires each particle to know the positions of nearby particles, this implementation avoids the naïve O(N²) approach by discretizing space into a density buffer each frame. Particles write their positions into this grid using atomic adds, and then sample local densities (e.g., to the left and right) to determine movement.
