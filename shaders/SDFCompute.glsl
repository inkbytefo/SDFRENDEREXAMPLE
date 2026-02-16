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

layout(push_constant) uniform PushConstants {
    vec4 camPos;     // xyz + pad
    vec4 camDir;     // xyz + pad
    vec4 params;     // resX, resY, time, editCount
    uint renderMode; // 0=Lit, 1=Normals, 2=Complexity
    uint showGround; // 1=On, 0=Off
    float pad3;
    float pad4;
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
};

HitResult mapScene(vec3 p) {
    int count = int(params.w);
    
    // Start with infinite distance
    HitResult res;
    res.dist = 1e10;
    res.albedo = vec3(0.5);
    res.roughness = 0.8;
    res.metallic = 0.0;

    // Ground plane (optional)
    if (showGround == 1) {
        float ground = sdPlane(p);
        if (ground < res.dist) {
            res.dist = ground;
            // Checkerboard
            float checker = mod(floor(p.x * 0.5) + floor(p.z * 0.5), 2.0);
            res.albedo = mix(vec3(0.35, 0.38, 0.42), vec3(0.55, 0.58, 0.62), checker);
            res.roughness = 0.9;
            res.metallic = 0.0;
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
                }
                break;
            case 1: // Subtraction
            {
                float newDist = opSubtract(res.dist, d);
                if (newDist < res.dist && d < 0.001) { // If we are cutting, take material if inside
                     // Usually subtraction keeps the base material but we can blend if needed
                }
                res.dist = newDist;
                break;
            }
            case 2: // Intersection
                res.dist = opIntersect(res.dist, d);
                // For intersection, we might want to blend based on which is closer to surface
                break;
            case 3: // Smooth Union
            {
                float k = max(e.blendFactor, 0.01);
                float newDist = opSmoothUnion(res.dist, d, k);
                float h = clamp(0.5 + 0.5 * (d - prevDist) / k, 0.0, 1.0);
                res.albedo = mix(e.albedo, res.albedo, h);
                res.roughness = mix(e.roughness, res.roughness, h);
                res.metallic = mix(e.metallic, res.metallic, h);
                res.dist = newDist;
                break;
            }
            case 4: // Smooth Subtraction
            {
                float k = max(e.blendFactor, 0.01);
                float newDist = opSmoothSub(res.dist, d, k);
                float h = clamp(0.5 - 0.5 * (res.dist + d) / k, 0.0, 1.0);
                // Optional: blend material into the "cut" surface
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

    // Tone map + gamma (skipped for debug modes usually, but kept for consistency)
    if (renderMode == 0) {
        color = ACESFilm(color);
        color = pow(color, vec3(1.0 / 2.2));
    } else if (renderMode == 1) {
        // Just gamma for normals
        color = pow(color, vec3(1.0 / 2.2));
    }

    imageStore(outImage, pixel, vec4(color, 1.0));
}
