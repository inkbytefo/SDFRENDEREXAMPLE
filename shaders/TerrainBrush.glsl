#version 460

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, r32f) uniform image2D heightmap;
layout(binding = 1, rgba8) uniform image2D splatmap;

layout(push_constant) uniform PushConstants {
    vec2 pos;      // UV center
    float radius;  // UV radius
    float strength;
    uint mode;     // 0=Raise, 1=Lower, 2=Flatten, 3=Smooth, 4=Paint
    uint layer;    // 0..3
    float targetHeight;
    float padding;
} pc;

void main() {
    ivec2 size = imageSize(heightmap);
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size);
    float dist = distance(uv, pc.pos);

    if (dist > pc.radius) return;

    // Falloff function (smoothstep-like)
    float falloff = smoothstep(pc.radius, pc.radius * 0.5, dist);
    
    if (pc.mode == 0) { // Raise
        float h = imageLoad(heightmap, pixel).r;
        h += pc.strength * falloff;
        imageStore(heightmap, pixel, vec4(h, 0, 0, 0));
    } 
    else if (pc.mode == 1) { // Lower
        float h = imageLoad(heightmap, pixel).r;
        h -= pc.strength * falloff;
        imageStore(heightmap, pixel, vec4(h, 0, 0, 0));
    }
    else if (pc.mode == 3) { // Smooth
        // Box blur kernel (3x3)
        float sum = 0.0;
        float count = 0.0;
        for (int y = -1; y <= 1; y++) {
            for (int x = -1; x <= 1; x++) {
                ivec2 p = pixel + ivec2(x, y);
                if (p.x >= 0 && p.x < size.x && p.y >= 0 && p.y < size.y) {
                    sum += imageLoad(heightmap, p).r;
                    count += 1.0;
                }
            }
        }
        float avg = sum / count;
        float h = imageLoad(heightmap, pixel).r;
        h = mix(h, avg, pc.strength * falloff);
        imageStore(heightmap, pixel, vec4(h, 0, 0, 0));
    }
    else if (pc.mode == 4) { // Paint
        vec4 splat = imageLoad(splatmap, pixel);
        
        // Target layer gets +strength, others get -strength (normalize)
        if (pc.layer == 0) splat.r += pc.strength * falloff;
        if (pc.layer == 1) splat.g += pc.strength * falloff;
        if (pc.layer == 2) splat.b += pc.strength * falloff;
        if (pc.layer == 3) splat.a += pc.strength * falloff;
        
        splat = max(splat, vec4(0));
        float total = splat.r + splat.g + splat.b + splat.a;
        if (total > 0.0001) splat /= total;
        
        imageStore(splatmap, pixel, splat);
    }
}
