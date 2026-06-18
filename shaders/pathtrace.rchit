#version 460
#extension GL_EXT_ray_tracing : require

struct Vertex {
    vec4 position;
    vec4 normal;
    vec4 texcoord;
};

struct ObjectRecord {
    uint vertexOffset;
    uint indexOffset;
    uint materialIndex;
    uint flags;
};

struct Material {
    vec4 baseColorRoughness;
    vec4 emissionMetallic;
};

layout(set = 0, binding = 2, std430) readonly buffer VertexBuffer {
    Vertex vertices[];
};

layout(set = 0, binding = 3, std430) readonly buffer IndexBuffer {
    uint indices[];
};

layout(set = 0, binding = 4, std430) readonly buffer ObjectBuffer {
    ObjectRecord objects[];
};

layout(set = 0, binding = 5, std430) readonly buffer MaterialBuffer {
    Material materials[];
};

layout(location = 0) rayPayloadInEXT vec4 payload;
hitAttributeEXT vec2 attribs;

void main()
{
    ObjectRecord object = objects[gl_InstanceCustomIndexEXT];
    uint baseIndex = object.indexOffset + gl_PrimitiveID * 3;

    uint i0 = object.vertexOffset + indices[baseIndex + 0];
    uint i1 = object.vertexOffset + indices[baseIndex + 1];
    uint i2 = object.vertexOffset + indices[baseIndex + 2];

    vec3 p0 = vertices[i0].position.xyz;
    vec3 p1 = vertices[i1].position.xyz;
    vec3 p2 = vertices[i2].position.xyz;

    vec3 n0 = vertices[i0].normal.xyz;
    vec3 n1 = vertices[i1].normal.xyz;
    vec3 n2 = vertices[i2].normal.xyz;
    vec3 bary = vec3(1.0 - attribs.x - attribs.y, attribs.x, attribs.y);
    vec3 localNormal = normalize(n0 * bary.x + n1 * bary.y + n2 * bary.z);

    if (length(localNormal) < 0.001) {
        localNormal = normalize(cross(p1 - p0, p2 - p0));
    }

    vec3 worldNormal = normalize((gl_ObjectToWorldEXT * vec4(localNormal, 0.0)).xyz);
    vec3 viewDir = normalize(-gl_WorldRayDirectionEXT);
    if (dot(worldNormal, viewDir) < 0.0) {
        worldNormal = -worldNormal;
    }

    Material material = materials[object.materialIndex];
    vec3 baseColor = material.baseColorRoughness.rgb;
    vec3 emission = material.emissionMetallic.rgb;
    float metallic = material.emissionMetallic.a;

    vec3 lightDir = normalize(vec3(-0.35, 0.85, 0.3));
    float ndotl = max(dot(worldNormal, lightDir), 0.0);
    vec3 diffuse = baseColor * (0.18 + 0.82 * ndotl);
    vec3 metalTint = mix(diffuse, baseColor * pow(max(dot(reflect(-lightDir, worldNormal), viewDir), 0.0), 32.0) * 2.5, metallic);

    payload = vec4(emission + metalTint, 1.0);
}
