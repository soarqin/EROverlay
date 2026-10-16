// Companion vertex shader for testing a locally extracted SM6 game compositor.
// dxc -T vs_6_0 -E main tools/ui_scene_compositor_verify.hlsl -Fo build/ui-scene/ui_scene_compositor_verify.vso
struct Output {
    float4 position : SV_POSITION;
    float4 unused : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

Output main(uint id : SV_VertexID) {
    Output output;
    output.uv = float2((id << 1) & 2, id & 2);
    output.position = float4(output.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    output.unused = 0;
    return output;
}
