#version 460
#extension GL_EXT_ray_tracing : require

layout(location = 0) rayPayloadInEXT vec4 payload;

void main()
{
    vec3 unitDir = normalize(gl_WorldRayDirectionEXT);
    float t = 0.5 * (unitDir.y + 1.0);
    payload = vec4(mix(vec3(0.02, 0.025, 0.035), vec3(0.38, 0.46, 0.62), t), 1.0);
}
