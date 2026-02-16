# Physics & Simulation

Dynamic geometry requires a hybrid approach to physics. We utilize **Jolt Physics** for its high performance and multi-threaded architecture.

## 1. Static Geometry Collision

While SDFs are mathematical, many physics engines work best with triangle meshes for static environments.

### Marching Cubes Baking
When a region of the SDF world is finalized or moves out of the "active edit zone":
1. **Compute Shader**: Runs the **Marching Cubes** algorithm on the Brick Atlas data.
2. **Mesh Generation**: A lightweight triangle mesh is generated.
3. **Jolt Integration**: This mesh is added to Jolt as a `StaticCompoundShape` for efficient broad-phase collision.

## 2. Dynamic SDF Bodies

For objects that are themselves SDF-based (e.g., a boulder that can be chipped away while rolling):
- **Volume Bodies**: The physics body points directly to an SDF evaluation function or a small local Brick-Map.
- **SDF-to-SDF Collision**: Handled by sampling the distance field of object A at the surface points of object B.
- **Impulse Response**: Forces are applied at the point where the distance field value `dist < 0`.

## 3. Position Based Dynamics (PBD)

For cloth and soft-body simulations:
- **Verlet Integration**: Particles represent the cloth vertices.
- **SDF Constraints**: The distance field serves as a global collision constraint. If a cloth particle enters an SDF surface (`dist < 0`), it is projected back to `dist = epsilon` along the surface normal.
- **Benefit**: This allows cloth to interact with any part of the dynamic world without custom collision meshes.

## 4. Fluid Simulation (SPH)

**Smoothed Particle Hydrodynamics** is used for liquid effects.
- **Particle-SDF Interaction**: Fluid particles treat the SDF distance as a boundary.
- **Voxel-based Pressure**: The Brick Atlas can be used to accelerate the neighbor search for SPH particles.
- **Visuals**: Fluid particles can be "blitted" back into the SDF representation for high-quality rendering via ray-marching.
