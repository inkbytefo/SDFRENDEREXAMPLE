# Roadmap

Status of the ideas from the original design document. Items without a check are **not implemented**.

## Done

- [x] Edit list with 5 primitives, rotation, and 5 CSG operations
- [x] Sparse brick map + brick atlas, GPU bake with incremental (dirty-region) updates
- [x] Empty-space skipping in the raymarcher
- [x] Analytic refinement, normals and materials at the hit point
- [x] Editor: inspector, click picking, render modes, brick statistics
- [x] Heightfield terrain with Raise / Lower / Flatten / Smooth / Paint brushes
- [x] Jolt Physics initialized and stepped (no bodies)

## Next

- [ ] **Physics collision**: query the brick field (or the analytic edits) for Jolt shapes. An alternative is meshing settled regions with marching cubes into static Jolt meshes.
- [ ] **Volume beyond 56 m**: clipmaps or a movable volume window (nested grids, each level twice the cell size).
- [ ] **Atlas growth / LRU**: today a full atlas drops surface cells, and the editor reports it.
- [ ] **Memory allocator**: every resource gets its own `vkAllocateMemory` (switch to VMA).
- [ ] **Terrain in bricks**, so terrain and edits blend and share shadows.
- [ ] **Cheaper dirty regions**: one union box per frame. Deleting from the middle of the list dirties every later edit.

## Later / exploratory

- [ ] Deferred G-buffer and rasterized meshes alongside SDFs
- [ ] HiZ occlusion for marching
- [ ] SDF global illumination using the brick atlas
- [ ] PBD cloth and SPH fluids colliding with the SDF
- [ ] Async compute queue for bakes
