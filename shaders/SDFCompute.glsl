#version 460

// ============================================================
// SDF Playground — GPU-Driven Edit Buffer Compute Shader
// ============================================================

layout(local_size_x = 8, local_size_y = 8) in;

layout(binding = 0, r16f)  uniform image3D brickAtlas;
layout(binding = 1, r32ui) uniform uimage3D sparseMap;
layout(binding = 2, rgba8) uniform image2D outImage;

// GPU Edit struct — must match CPU SDFEdit exactly
struct SDFEditGPU {
    vec3  position;   float pad1;
    vec4  rotation;
    vec3  scale;      uint  primitiveType;
    uint  operation;  float blendFactor;
    uint  isDynamic;  float pad2;
    vec3  albedo;     float roughness;
    float metallic;   float matPad1; float matPad2; float matPad3;
};

layout(std430, binding = 3) buffer EditBuffer {
    SDFEditGPU edits[];
};

layout(std430, binding = 4) buffer SelectionBuffer {
    int hitIndex;
    float hitPosX, hitPosY, hitPosZ;
};

layout(binding = 5) uniform sampler2D terrainHeight;
layout(binding = 6) uniform sampler2D terrainSplat;

layout(push_constant) uniform PushConstants {
    vec4 camPos;     // xyz + pad
    vec4 camDir;     // xyz + pad
    vec4 params;     // resX, resY, time, editCount
    uint renderMode; // 0=Lit, 1=Normals, 2=Complexity
    uint showGround; // 1=On, 0=Off
    float mouseX;    // -1 if not picking
    float mouseY;
    vec4 brushPos;   // xyz=pos, w=radius
    uint showGrid;
    float pad1;
    float pad2;
    float pad3;
};

// ============== SDF Primitives ==============

float sdSphere(vec3 p, float r) {
    return length(p) - r;
}

float sdBox(vec3 p, vec3 b) {
    vec3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorus(vec3 p, vec2 t) {
    vec2 q = vec2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float sdCapsule(vec3 p, float h, float r) {
    p.y -= clamp(p.y, 0.0, h);
    return length(p) - r;
}

float sdCylinder(vec3 p, float h, float r) {
    vec2 d = abs(vec2(length(p.xz), p.y)) - vec2(r, h);
    return min(max(d.x, d.y), 0.0) + length(max(d, 0.0));
}

float sdPlane(vec3 p) {
    return p.y;
}

float sdTerrain(vec3 p) {
    const float worldSize = 256.0;
    vec2 uv = (p.xz + worldSize * 0.5) / worldSize;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return p.y;
    
    float h = textureLod(terrainHeight, uv, 0.0).r;
    return (p.y - h) * 0.5; // Conservative step
}

vec4 getTerrainMaterial(vec3 p) {
    const float worldSize = 256.0;
    vec2 uv = (p.xz + worldSize * 0.5) / worldSize;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) return vec4(0.5, 0.8, 0.0, 0.0);

    vec4 splat = textureLod(terrainSplat, uv, 0.0);
    
    // Define 4 materials
    // Mat 0 (Red ch): Grass
    vec3 col0 = vec3(0.1, 0.4, 0.1); float rough0 = 0.9; float met0 = 0.0;
    // Mat 1 (Green ch): Dirt
    vec3 col1 = vec3(0.4, 0.3, 0.2); float rough1 = 1.0; float met1 = 0.0;
    // Mat 2 (Blue ch): Rock
    vec3 col2 = vec3(0.5, 0.5, 0.5); float rough2 = 0.7; float met2 = 0.0;
    // Mat 3 (Alpha ch): Snow
    vec3 col3 = vec3(0.9, 0.9, 0.95); float rough3 = 0.3; float met3 = 0.0;
    
    vec3 albedo = col0 * splat.r + col1 * splat.g + col2 * splat.b + col3 * splat.a;
    float roughness = rough0 * splat.r + rough1 * splat.g + rough2 * splat.b + rough3 * splat.a;
    float metallic = met0 * splat.r + met1 * splat.g + met2 * splat.b + met3 * splat.a;
    
    // Normalize if sum > 1 (it should be roughly 1)
    float sum = dot(splat, vec4(1.0));
    if (sum > 0.001) {
        albedo /= sum;
        roughness /= sum;
        metallic /= sum;
    }
    
    return vec4(albedo, roughness); // pack roughness in A? No, wait.
    // Return struct-like? No, vec4 is rigid. 
    // Let's just return a packed vec4: rgb=albedo, a=roughness. Metallic separate?
    // Metallic is mostly 0 for terrain.
}

// ============== SDF for a single edit ==============

float evalPrimitive(vec3 p, SDFEditGPU e) {
    vec3 lp = p - e.position;
    
    switch (e.primitiveType) {
        case 0: return sdSphere(lp, e.scale.x);
        case 1: return sdBox(lp, e.scale);
        case 2: return sdTorus(lp, vec2(e.scale.x, e.scale.y));
        case 3: return sdCapsule(lp, e.scale.y, e.scale.x);
        case 4: return sdCylinder(lp, e.scale.y, e.scale.x);
        default: return sdSphere(lp, e.scale.x);
    }
}

// ============== Boolean Operations ==============

float opUnion(float d1, float d2) { return min(d1, d2); }
float opSubtract(float d1, float d2) { return max(d1, -d2); }
float opIntersect(float d1, float d2) { return max(d1, d2); }

float opSmoothUnion(float d1, float d2, float k) {
    float h = clamp(0.5 + 0.5 * (d2 - d1) / k, 0.0, 1.0);
    return mix(d2, d1, h) - k * h * (1.0 - h);
}

float opSmoothSub(float d1, float d2, float k) {
    float h = clamp(0.5 - 0.5 * (d1 + d2) / k, 0.0, 1.0);
    return mix(d1, -d2, h) + k * h * (1.0 - h);
}

// ============== Scene (ground + edits) ==============

struct HitResult {
    float dist;
    vec3  albedo;
    float roughness;
    float metallic;
    int   index; // -1 for sky/nothing, 0 for ground, 1+ for edits
};

HitResult mapScene(vec3 p) {
    int count = int(params.w);
    
    // Start with infinite distance
    HitResult res;
    res.dist = 1e10;
    res.albedo = vec3(0.5);
    res.roughness = 0.8;
    res.metallic = 0.0;
    res.index = -1;

    // Ground plane (optional)
    if (showGround == 1) {
        float ground = sdTerrain(p);
        if (ground < res.dist) {
            res.dist = ground;
            
            // Material from splatmap
            const float worldSize = 256.0;
            vec2 uv = (p.xz + worldSize * 0.5) / worldSize;
            
            if (uv.x >= 0.0 && uv.x <= 1.0 && uv.y >= 0.0 && uv.y <= 1.0) {
                 vec4 splat = textureLod(terrainSplat, uv, 0.0);
                 
                // Mat 0 (Red ch): Grass
                vec3 col0 = vec3(0.1, 0.4, 0.1); float rough0 = 0.9; float met0 = 0.0;
                // Mat 1 (Green ch): Dirt
                vec3 col1 = vec3(0.4, 0.3, 0.2); float rough1 = 1.0; float met1 = 0.0;
                // Mat 2 (Blue ch): Rock
                vec3 col2 = vec3(0.5, 0.5, 0.5); float rough2 = 0.7; float met2 = 0.0;
                // Mat 3 (Alpha ch): Snow
                vec3 col3 = vec3(0.9, 0.9, 0.95); float rough3 = 0.3; float met3 = 0.0;
                
                res.albedo = col0 * splat.r + col1 * splat.g + col2 * splat.b + col3 * splat.a;
                res.roughness = rough0 * splat.r + rough1 * splat.g + rough2 * splat.b + rough3 * splat.a;
                res.metallic = met0 * splat.r + met1 * splat.g + met2 * splat.b + met3 * splat.a;
                
                float sum = dot(splat, vec4(1.0));
                if (sum > 0.001) {
                    res.albedo /= sum;
                    res.roughness /= sum;
                    res.metallic /= sum;
                }
            } else {
                 // Checkerboard fallback
                float checker = mod(floor(p.x * 0.5) + floor(p.z * 0.5), 2.0);
                res.albedo = mix(vec3(0.35, 0.38, 0.42), vec3(0.55, 0.58, 0.62), checker);
                res.roughness = 0.9;
                res.metallic = 0.0;
            }

            // --- Debug Grid ---
            if (showGrid == 1) {
                // World space grid
                float gridSize = 1.0;
                float lineThickness = 0.02;
                
                // Simple non-anti-aliased grid for now to avoid derivative issues in compute
                vec2 g = abs(fract(p.xz / gridSize - 0.5) - 0.5);
                float gLine = min(g.x, g.y);
                if (gLine < lineThickness) {
                    res.albedo = mix(res.albedo, vec3(0.8), 0.5); // Light grid
                }
                
                // Major grid
                vec2 gMajor = abs(fract(p.xz / 10.0) - 0.5);
                float gMajorLine = min(gMajor.x, gMajor.y);
                if (gMajorLine < 0.005) { // 0.005 * 10 = 0.05
                    res.albedo = mix(res.albedo, vec3(1.0), 0.6); // White major lines
                }
            }

            // --- Brush Cursor ---
            if (brushPos.w > 0.0) {
                 float dist = distance(p, brushPos.xyz);
                 float ringWidth = 0.1;
                 
                 // Ring
                 if (abs(dist - brushPos.w) < ringWidth) {
                     res.albedo = mix(res.albedo, vec3(1.0, 0.5, 0.0), 0.8); // Orange ring
                 }
                 // Area
                 else if (dist < brushPos.w) {
                     res.albedo = mix(res.albedo, vec3(1.0, 0.5, 0.0), 0.1); // Faint orange fill
                 }
            }
            
            res.index = 0; // Ground index
        }
    }

    // Evaluate each edit
    for (int i = 0; i < count && i < 256; i++) {
        SDFEditGPU e = edits[i];
        float d = evalPrimitive(p, e);

        float prevDist = res.dist;
        
        switch (e.operation) {
            case 0: // Union
                if (d < res.dist) {
                    res.dist = d;
                    res.albedo = e.albedo;
                    res.roughness = e.roughness;
                    res.metallic = e.metallic;
                    res.index = i + 1; // Edit index (1-based because 0 is ground)
                }
                break;
            case 1: // Subtraction
            {
                float newDist = opSubtract(res.dist, d);
                // Subtraction keeps base material/index
                res.dist = newDist;
                break;
            }
            case 2: // Intersection
            {
                float newDist = opIntersect(res.dist, d);
                // If intersection is closer to the new part, swap index?
                // For simplicity, we keep the previous index for now or take the new one if closer
                if (newDist < res.dist && d < res.dist) {
                    res.index = i + 1;
                }
                res.dist = newDist;
                break;
            }
            case 3: // Smooth Union
            {
                float k = max(e.blendFactor, 0.01);
                float newDist = opSmoothUnion(res.dist, d, k);
                float h = clamp(0.5 + 0.5 * (d - prevDist) / k, 0.0, 1.0);
                res.albedo = mix(e.albedo, res.albedo, h);
                res.roughness = mix(e.roughness, res.roughness, h);
                res.metallic = mix(e.metallic, res.metallic, h);
                if (h < 0.5) res.index = i + 1; // Take index of the "closer" or "more dominant" part
                res.dist = newDist;
                break;
            }
            case 4: // Smooth Subtraction
            {
                float k = max(e.blendFactor, 0.01);
                float newDist = opSmoothSub(res.dist, d, k);
                float h = clamp(0.5 - 0.5 * (res.dist + d) / k, 0.0, 1.0);
                res.albedo = mix(res.albedo, e.albedo, h * 0.5); 
                res.dist = newDist;
                break;
            }
        }
    }

    return res;
}

// ============== Lighting ==============

vec3 calcNormal(vec3 p) {
    const float h = 0.001;
    return normalize(vec3(
        mapScene(p + vec3(h, 0, 0)).dist - mapScene(p - vec3(h, 0, 0)).dist,
        mapScene(p + vec3(0, h, 0)).dist - mapScene(p - vec3(0, h, 0)).dist,
        mapScene(p + vec3(0, 0, h)).dist - mapScene(p - vec3(0, 0, h)).dist
    ));
}

float softShadow(vec3 ro, vec3 rd, float mint, float maxt, float k) {
    float res = 1.0;
    float t = mint;
    for (int i = 0; i < 32 && t < maxt; i++) {
        float h = mapScene(ro + rd * t).dist;
        if (h < 0.001) return 0.0;
        res = min(res, k * h / t);
        t += clamp(h, 0.02, 0.5);
    }
    return clamp(res, 0.0, 1.0);
}

float calcAO(vec3 p, vec3 n) {
    float occ = 0.0;
    float sca = 1.0;
    for (int i = 0; i < 5; i++) {
        float h = 0.02 + 0.12 * float(i);
        float d = mapScene(p + n * h).dist;
        occ += (h - d) * sca;
        sca *= 0.75;
    }
    return clamp(1.0 - 1.5 * occ, 0.0, 1.0);
}

vec3 shade(vec3 p, vec3 n, vec3 viewDir, vec3 albedo, float roughness, float metallic) {
    // Two lights
    vec3 lightDir1 = normalize(vec3(0.6, 0.8, -0.4));
    vec3 lightDir2 = normalize(vec3(-0.4, 0.5, 0.7));
    vec3 lightCol1 = vec3(1.4, 1.3, 1.2);
    vec3 lightCol2 = vec3(0.3, 0.4, 0.6);

    float diff1 = max(dot(n, lightDir1), 0.0);
    float diff2 = max(dot(n, lightDir2), 0.0);
    
    float shadow1 = softShadow(p + n * 0.01, lightDir1, 0.02, 20.0, 12.0);
    
    vec3 h1 = normalize(lightDir1 + viewDir);
    float spec1 = pow(max(dot(n, h1), 0.0), mix(8.0, 128.0, 1.0 - roughness));
    float specIntensity = mix(0.04, 1.0, metallic);

    float ao = calcAO(p, n);
    
    vec3 ambient = vec3(0.08, 0.09, 0.12) * ao;
    vec3 color = ambient * albedo;
    color += albedo * lightCol1 * diff1 * shadow1;
    color += albedo * lightCol2 * diff2 * 0.3;
    color += vec3(specIntensity) * lightCol1 * spec1 * shadow1;
    
    return color;
}

// ACES tone mapping
vec3 ACESFilm(vec3 x) {
    float a = 2.51;
    float b = 0.03;
    float c = 2.43;
    float d = 0.59;
    float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

// ============== Main ==============

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = ivec2(params.x, params.y);
    
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    vec2 uv = (vec2(pixel) + 0.5) / vec2(size) * 2.0 - 1.0;
    uv.y = -uv.y; // Vulkan Y flip
    uv.x *= float(size.x) / float(size.y);

    // Camera setup
    vec3 ro = camPos.xyz;
    vec3 forward = normalize(camDir.xyz);
    vec3 worldUp = vec3(0, 1, 0);
    vec3 right = normalize(cross(forward, worldUp));
    vec3 up = cross(right, forward);
    float fov = 1.0;
    vec3 rd = normalize(forward * fov + right * uv.x + up * uv.y);

    // Ray march
    float t = 0.0;
    HitResult hit;
    hit.dist = 1e10;
    hit.index = -1;
    bool hitSurface = false;
    int steps = 0;

    for (int i = 0; i < 128; i++) {
        steps++;
        vec3 p = ro + rd * t;
        hit = mapScene(p);
        if (hit.dist < 0.001) {
            hitSurface = true;
            break;
        }
        if (t > 100.0) break;
        t += hit.dist;
    }

    // Write selection result if this pixel is the target
    if (pixel.x == int(mouseX) && pixel.y == int(mouseY)) {
        hitIndex = hitSurface ? hit.index : -1;
        vec3 hp = ro + rd * t;
        hitPosX = hp.x;
        hitPosY = hp.y;
        hitPosZ = hp.z;
    }

    vec3 color = vec3(0);
    if (renderMode == 2) { // Complexity
        float c = float(steps) / 128.0;
        color = vec3(c * c, c, 0.5 * c); // Heatmap
    } else if (hitSurface) {
        vec3 p = ro + rd * t;
        vec3 n = calcNormal(p);
        
        if (renderMode == 1) { // Normals
            color = n * 0.5 + 0.5;
        } else { // Lit
            vec3 viewDir = -rd;
            color = shade(p, n, viewDir, hit.albedo, hit.roughness, hit.metallic);
            
            // Distance fog
            float fog = 1.0 - exp(-0.003 * t * t);
            vec3 fogColor = vec3(0.45, 0.50, 0.60);
            color = mix(color, fogColor, fog);
        }
    } else {
        // Sky gradient
        float skyT = 0.5 * (rd.y + 1.0);
        color = mix(vec3(0.45, 0.50, 0.60), vec3(0.20, 0.30, 0.55), skyT);
    }

    // Tone map + gamma
    if (renderMode == 0) {
        color = ACESFilm(color);
        color = pow(color, vec3(1.0 / 2.2));
    } else if (renderMode == 1) {
        color = pow(color, vec3(1.0 / 2.2));
    }

    imageStore(outImage, pixel, vec4(color, 1.0));
}
