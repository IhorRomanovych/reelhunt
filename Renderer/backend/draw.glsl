// draw.glsl - per-draw push constants of the scene pipelines. Mirrors gpu_types.h (DrawConstants).

layout(push_constant) uniform Draw {
    mat4 model;      // rigid model -> world (identity for skinned meshes)
    vec4 material;   // x metallic, y roughness, z alpha test, w baked lighting
    vec4 tint;       // rgb material colour
    vec4 ambient;    // rgb per-object ambient, w > 0 overrides the frame's ambient
    uint boneOffset; // first matrix of this mesh's pose in the bone buffer
    uint cascade;    // shadow pass: the cascade being drawn
} draw;

// Every skin pose drawn this frame, back to back (indexed from draw.boneOffset).
layout(std430, set = 0, binding = 1) readonly buffer Bones {
    mat4 bones[];
};

mat4 skinMatrix(uvec4 index, vec4 weight) {
    float sum = dot(weight, vec4(1.0));
    vec4 w = sum > 0.0 ? weight / sum : vec4(1.0, 0.0, 0.0, 0.0);
    return bones[draw.boneOffset + index.x] * w.x + bones[draw.boneOffset + index.y] * w.y +
           bones[draw.boneOffset + index.z] * w.z + bones[draw.boneOffset + index.w] * w.w;
}
