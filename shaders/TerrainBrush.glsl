#version 460

// Terrain brush. Dispatched only over the brush's bounding rectangle.
// Smooth reads neighbours from a snapshot (heightScratch) so invocations never read
// texels that other invocations are writing.

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, r32f)  uniform image2D heightmap;
layout(binding = 1, rgba8) uniform image2D splatmap;
layout(binding = 2, r32f)  uniform readonly image2D heightScratch;

// Must match engine::renderer::Terrain::BrushParams (48 bytes).
layout(push_constant) uniform PushConstants {
    vec2  pos;          // UV center
    float radius;       // UV radius
    float strength;
    uint  mode;         // 0=Raise, 1=Lower, 2=Flatten, 3=Smooth, 4=Paint
    uint  layer;        // 0..3
    float targetHeight; // Flatten
    float pad0;
    ivec2 origin;       // first texel of the dispatched rectangle
    ivec2 pad1;
} pc;

void main() {
    ivec2 size = imageSize(heightmap);
    ivec2 pixel = pc.origin + ivec2(gl_GlobalInvocationID.xy);
    if (any(lessThan(pixel, ivec2(0))) || any(greaterThanEqual(pixel, size))) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    float dist = distance(uv, pc.pos);
    if (dist > pc.radius) return;

    float falloff = smoothstep(pc.radius, pc.radius * 0.5, dist);
    float w = clamp(pc.strength * falloff, 0.0, 1.0);

    if (pc.mode == 0u) {
        float h = imageLoad(heightmap, pixel).r;
        imageStore(heightmap, pixel, vec4(h + pc.strength * falloff));
    } else if (pc.mode == 1u) {
        float h = imageLoad(heightmap, pixel).r;
        imageStore(heightmap, pixel, vec4(h - pc.strength * falloff));
    } else if (pc.mode == 2u) {
        float h = imageLoad(heightmap, pixel).r;
        imageStore(heightmap, pixel, vec4(mix(h, pc.targetHeight, w)));
    } else if (pc.mode == 3u) {
        float sum = 0.0;
        float count = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec2 q = pixel + ivec2(x, y);
                if (all(greaterThanEqual(q, ivec2(0))) && all(lessThan(q, size))) {
                    sum += imageLoad(heightScratch, q).r;
                    count += 1.0;
                }
            }
        }
        float h = imageLoad(heightScratch, pixel).r;
        imageStore(heightmap, pixel, vec4(mix(h, sum / count, w)));
    } else if (pc.mode == 4u) {
        vec4 splat = imageLoad(splatmap, pixel);
        splat[min(pc.layer, 3u)] += pc.strength * falloff;
        splat = max(splat, vec4(0.0));
        float total = dot(splat, vec4(1.0));
        if (total > 0.0001) splat /= total;
        imageStore(splatmap, pixel, splat);
    }
}
