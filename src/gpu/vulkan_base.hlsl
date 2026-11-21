struct PushConstants {
    float4 geometry;
    float4 texture_info;
    uint4 alpha;
    float4 clear_color;
};

[[vk::push_constant]]
ConstantBuffer<PushConstants> constants;

struct VertexInput {
    float4 position : POSITION;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

struct PixelInput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[[vk::binding(0, 0)]]
Texture2D<float4> texture0;

[[vk::binding(1, 0)]]
SamplerState sampler0;

PixelInput base_vs(VertexInput input)
{
    PixelInput output;
    float w = input.position.w > 0.0 ? 1.0 / input.position.w : 1.0;
    float x = input.position.x * constants.geometry.x * 2.0 - 1.0;
    float y = 1.0 - input.position.y * constants.geometry.y * 2.0;
    float z = (input.position.z - constants.geometry.z) * constants.geometry.w;

    output.position = float4(x * w, y * w, z * w, w);
    output.color = input.color;
    output.uv = input.uv * constants.texture_info.xy;
    return output;
}

float4 base_ps(PixelInput input) : SV_Target
{
    float4 color = input.color;
    if (constants.texture_info.z > 0.5)
        color *= texture0.Sample(sampler0, input.uv);
    if (constants.texture_info.w > 0.5) {
        uint value = (uint)(color.a * 255.0 + 0.5);
        uint reference = constants.alpha.y;
        uint function = constants.alpha.x;
        bool pass = function == 7
            || (function == 1 && value < reference)
            || (function == 2 && value == reference)
            || (function == 3 && value <= reference)
            || (function == 4 && value > reference)
            || (function == 5 && value != reference)
            || (function == 6 && value >= reference);
        if (!pass)
            discard;
    }
    return color;
}

PixelInput clear_vs(uint vertex_id : SV_VertexID)
{
    static const float2 positions[3] = {
        float2(-1.0, -1.0),
        float2(-1.0, 3.0),
        float2(3.0, -1.0),
    };
    PixelInput output;
    output.position = float4(positions[vertex_id], 0.0, 1.0);
    output.color = constants.clear_color;
    output.uv = 0.0;
    return output;
}

float4 clear_ps(PixelInput input) : SV_Target
{
    return input.color;
}

struct ExtendedVertexInput {
    float4 position : POSITION;
    float4 d0 : COLOR0;
    float4 d1 : COLOR1;
    float4 fog : TEXCOORD4;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
    float4 tc3 : TEXCOORD3;
};

struct ExtendedPixelInput {
    float4 position : SV_Position;
    noperspective float4 d0 : COLOR0;
    noperspective float4 d1 : COLOR1;
    noperspective float fog : FOG;
    float4 tc0 : TEXCOORD0;
    float4 tc1 : TEXCOORD1;
    float4 tc2 : TEXCOORD2;
    float4 tc3 : TEXCOORD3;
    float2 clip : SV_ClipDistance0;
};

[[vk::binding(2, 0)]]
Texture2D<float4> texture1;

[[vk::binding(3, 0)]]
SamplerState sampler1;

[[vk::binding(4, 0)]]
Texture2D<float4> texture2;

[[vk::binding(5, 0)]]
SamplerState sampler2;

[[vk::binding(6, 0)]]
Texture2D<float4> texture3;

[[vk::binding(7, 0)]]
SamplerState sampler3;

[[vk::binding(8, 0)]]
cbuffer ExtendedConstants {
    float4 x_surface;
    float4 x_depth;
    float4 x_alpha;
    float4 x_fog_color;
    float4 x_c0[8];
    float4 x_c1[8];
    float4 x_final_c0;
    float4 x_final_c1;
    float4 x_texture_info[4];
    uint4 x_color_icw[2];
    uint4 x_alpha_icw[2];
    uint4 x_color_ocw[2];
    uint4 x_alpha_ocw[2];
    uint4 x_final_control;
    uint4 x_texture_state;
    uint4 x_clip_state;
    uint4 x_vsh_data;
    uint4 x_vsh_state;
    float4 x_vsh_info;
    float4 x_vsh_fog;
    uint4 x_vsh_verify;
};

[[vk::binding(9, 0)]]
TextureCube<float4> texture_cube0;

[[vk::binding(10, 0)]]
TextureCube<float4> texture_cube1;

[[vk::binding(11, 0)]]
TextureCube<float4> texture_cube2;

[[vk::binding(12, 0)]]
TextureCube<float4> texture_cube3;

ExtendedPixelInput extended_vs(ExtendedVertexInput input)
{
    ExtendedPixelInput output;
    float w = input.position.w > 0.0 ? 1.0 / input.position.w : 1.0;
    float x = input.position.x * x_surface.z * 2.0 - 1.0;
    float y = 1.0 - input.position.y * x_surface.w * 2.0;
    float depth = (input.position.z - x_depth.x) * x_depth.y;
    float z = x_depth.z > 0.5 ? saturate(depth) : depth;

    output.position = float4(x * w, y * w, z * w, w);
    output.d0 = input.d0;
    output.d1 = input.d1;
    output.fog = input.fog.x;
    output.tc0 = input.tc0;
    output.tc1 = input.tc1;
    output.tc2 = input.tc2;
    output.tc3 = input.tc3;
    output.clip = 1.0;
    return output;
}

[[vk::binding(13, 0)]]
ByteAddressBuffer vsh_data;

uint vsh_word(uint4 instruction, uint word)
{
    if (word == 0)
        return instruction.x;
    if (word == 1)
        return instruction.y;
    if (word == 2)
        return instruction.z;
    return instruction.w;
}

uint vsh_field(uint4 instruction, uint word, uint low, uint width)
{
    return (vsh_word(instruction, word) >> low) & ((1u << width) - 1u);
}

float4 vsh_load_float4(uint offset)
{
    return asfloat(vsh_data.Load4(offset));
}

float4 vsh_swizzle(float4 value, uint swizzle)
{
    return float4(value[(swizzle >> 6) & 3],
                  value[(swizzle >> 4) & 3],
                  value[(swizzle >> 2) & 3],
                  value[swizzle & 3]);
}

float4 vsh_temp(float4 registers[12], float4 position, uint index)
{
    if (index == 12)
        return position;
    return registers[index < 12 ? index : 0];
}

float4 vsh_source(uint4 instruction, uint source, uint vertex_id,
                  float4 registers[12], float4 position, int address)
{
    uint negate;
    uint swizzle;
    uint mux;
    uint index;
    float4 value;

    if (source == 0) {
        negate = vsh_field(instruction, 1, 8, 1);
        swizzle = vsh_field(instruction, 1, 0, 8);
        mux = vsh_field(instruction, 2, 26, 2);
        index = vsh_field(instruction, 2, 28, 4);
    } else if (source == 1) {
        negate = vsh_field(instruction, 2, 25, 1);
        swizzle = vsh_field(instruction, 2, 17, 8);
        mux = vsh_field(instruction, 2, 11, 2);
        index = vsh_field(instruction, 2, 13, 4);
    } else {
        negate = vsh_field(instruction, 2, 10, 1);
        swizzle = vsh_field(instruction, 2, 2, 8);
        mux = vsh_field(instruction, 3, 28, 2);
        index = (vsh_field(instruction, 2, 0, 2) << 2) |
                vsh_field(instruction, 3, 30, 2);
    }

    if (mux == 1) {
        value = vsh_temp(registers, position, index);
    } else if (mux == 2) {
        uint input_register = vsh_field(instruction, 1, 9, 4);
        uint input_index = countbits(
            x_vsh_state.y & ((1u << input_register) - 1u));
        uint offset = x_vsh_data.z +
                      (vertex_id * x_vsh_data.w + input_index) * 16u;
        value = vsh_load_float4(offset);
    } else {
        int constant_index = (int)vsh_field(instruction, 1, 13, 8);
        if (vsh_field(instruction, 3, 1, 1))
            constant_index += address;
        if (constant_index < 0 || constant_index >= 192)
            constant_index = 0;
        value = vsh_load_float4(
            x_vsh_data.y + (uint)constant_index * 16u);
    }
    value = vsh_swizzle(value, swizzle);
    return negate ? -value : value;
}

void vsh_mask_write(inout float4 destination, float4 value, uint mask)
{
    if (mask & 8)
        destination.x = value.x;
    if (mask & 4)
        destination.y = value.y;
    if (mask & 2)
        destination.z = value.z;
    if (mask & 1)
        destination.w = value.w;
}

float4 vsh_min(float4 a, float4 b)
{
    return float4(a.x < b.x ? a.x : b.x,
                  a.y < b.y ? a.y : b.y,
                  a.z < b.z ? a.z : b.z,
                  a.w < b.w ? a.w : b.w);
}

float4 vsh_max(float4 a, float4 b)
{
    return float4(a.x >= b.x ? a.x : b.x,
                  a.y >= b.y ? a.y : b.y,
                  a.z >= b.z ? a.z : b.z,
                  a.w >= b.w ? a.w : b.w);
}

float4 vsh_rcp(float value)
{
    return (value == 0.0 ? asfloat(0x7f800000) : 1.0 / value).xxxx;
}

float4 vsh_rcc(float value)
{
    float result = 1.0 / value;
    if (abs(result) < 5.42101e-20)
        result = result < 0.0 ? -5.42101e-20 : 5.42101e-20;
    if (abs(result) > 1.884467e19)
        result = result < 0.0 ? -1.884467e19 : 1.884467e19;
    return result.xxxx;
}

float4 vsh_exp(float value)
{
    float whole = floor(value);
    return float4(exp2(whole), value - whole, exp2(value), 1.0);
}

float4 vsh_log(float value)
{
    float magnitude = abs(value);
    if (magnitude == 0.0)
        return float4(-asfloat(0x7f800000), 1.0,
                      -asfloat(0x7f800000), 1.0);
    float exponent = floor(log2(magnitude));
    return float4(exponent, magnitude / exp2(exponent),
                  log2(magnitude), 1.0);
}

float4 vsh_lit(float4 value)
{
    float diffuse = value.x > 0.0 ? value.x : 0.0;
    float base = value.y > 0.0 ? value.y : 0.0;
    float power_value = clamp(value.w, -127.9961, 127.9961);
    float specular = base == 0.0
        ? (power_value == 0.0 ? 1.0
          : (power_value > 0.0 ? 0.0 : asfloat(0x7f800000)))
        : pow(base, power_value);
    return float4(1.0, diffuse, value.x > 0.0 ? specular : 0.0, 1.0);
}

float vsh_fog_factor(float depth)
{
    if (x_vsh_state.z == 0)
        return 1.0;
    uint mode = x_vsh_state.w;
    float x = x_vsh_fog.x;
    float y = x_vsh_fog.y;
    float value;
    if (mode == 0x800 || mode == 0x802)
        value = x + exp2(depth * y * 16.0) - 1.5;
    else if (mode == 0x801 || mode == 0x803)
        value = x + exp2(-depth * depth * y * y * 32.0) - 1.5;
    else
        value = x + depth * y - 1.0;
    if (mode == 0x802 || mode == 0x803 || mode == 0x804)
        value = abs(value);
    if (isnan(value))
        value = 1.0;
    return saturate(value);
}

ExtendedPixelInput vsh_run(uint vertex_id)
{
    float4 registers[12];
    float4 position = 0.0;
    float4 diffuse = float4(0.0, 0.0, 0.0, 1.0);
    float4 specular = float4(0.0, 0.0, 0.0, 1.0);
    float4 fog = 0.0;
    float4 texcoord0 = float4(0.0, 0.0, 0.0, 1.0);
    float4 texcoord1 = float4(0.0, 0.0, 0.0, 1.0);
    float4 texcoord2 = float4(0.0, 0.0, 0.0, 1.0);
    float4 texcoord3 = float4(0.0, 0.0, 0.0, 1.0);
    int address = 0;

    [unroll]
    for (uint register_index = 0; register_index < 12; ++register_index)
        registers[register_index] = 0.0;

    [loop]
    for (uint slot = 0; slot < x_vsh_state.x && slot < 136; ++slot) {
        uint4 instruction = vsh_data.Load4(x_vsh_data.x + slot * 16u);
        uint mac = vsh_field(instruction, 1, 21, 4);
        uint ilu = vsh_field(instruction, 1, 25, 3);
        uint mac_mask = vsh_field(instruction, 3, 24, 4);
        uint ilu_mask = vsh_field(instruction, 3, 16, 4);
        uint temporary = vsh_field(instruction, 3, 20, 4);
        uint output_mask = vsh_field(instruction, 3, 12, 4);
        uint output_address = vsh_field(instruction, 3, 3, 8);
        float4 a = 0.0;
        float4 b = 0.0;
        float4 c = 0.0;
        float4 mac_result = 0.0;
        float4 ilu_result = 0.0;

        if (mac != 0)
            a = vsh_source(instruction, 0, vertex_id,
                           registers, position, address);
        if (mac == 2 || (mac >= 4 && mac <= 12))
            b = vsh_source(instruction, 1, vertex_id,
                           registers, position, address);
        if (mac == 3 || mac == 4 || ilu != 0)
            c = vsh_source(instruction, 2, vertex_id,
                           registers, position, address);

        if (mac == 2)
            mac_result = a * b;
        else if (mac == 3)
            mac_result = a + c;
        else if (mac == 4)
            mac_result = a * b + c;
        else if (mac == 5)
            mac_result = dot(a.xyz, b.xyz).xxxx;
        else if (mac == 6)
            mac_result = (dot(a.xyz, b.xyz) + b.w).xxxx;
        else if (mac == 7)
            mac_result = dot(a, b).xxxx;
        else if (mac == 8)
            mac_result = float4(1.0, a.y * b.y, a.z, b.w);
        else if (mac == 9)
            mac_result = vsh_min(a, b);
        else if (mac == 10)
            mac_result = vsh_max(a, b);
        else if (mac == 11)
            mac_result = float4(a.x < b.x, a.y < b.y,
                                a.z < b.z, a.w < b.w);
        else if (mac == 12)
            mac_result = float4(a.x >= b.x, a.y >= b.y,
                                a.z >= b.z, a.w >= b.w);
        else if (mac != 0)
            mac_result = a;

        if (ilu == 1)
            ilu_result = c;
        else if (ilu == 2)
            ilu_result = vsh_rcp(c.x);
        else if (ilu == 3)
            ilu_result = vsh_rcc(c.x);
        else if (ilu == 4)
            ilu_result = (1.0 / sqrt(abs(c.x))).xxxx;
        else if (ilu == 5)
            ilu_result = vsh_exp(c.x);
        else if (ilu == 6)
            ilu_result = vsh_log(c.x);
        else if (ilu == 7)
            ilu_result = vsh_lit(c);

        if (mac == 13)
            address = (int)floor(a.x + 0.001);
        if (mac != 0 && mac != 13 && mac_mask != 0 &&
            temporary <= 12) {
            if (temporary == 12)
                vsh_mask_write(position, mac_result, mac_mask);
            else
                vsh_mask_write(registers[temporary],
                               mac_result, mac_mask);
        }
        if (ilu != 0 && ilu_mask != 0) {
            uint destination = mac != 0 ? 1 : temporary;
            if (destination == 12)
                vsh_mask_write(position, ilu_result, ilu_mask);
            else if (destination < 12)
                vsh_mask_write(registers[destination],
                               ilu_result, ilu_mask);
        }
        if (output_mask != 0 &&
            vsh_field(instruction, 3, 11, 1) != 0) {
            float4 value = vsh_field(instruction, 3, 2, 1)
                         ? ilu_result : mac_result;
            if (output_address == 0)
                vsh_mask_write(position, value, output_mask);
            else if (output_address == 3)
                vsh_mask_write(diffuse, value, output_mask);
            else if (output_address == 4)
                vsh_mask_write(specular, value, output_mask);
            else if (output_address == 5)
                vsh_mask_write(fog, value, output_mask);
            else if (output_address == 9)
                vsh_mask_write(texcoord0, value, output_mask);
            else if (output_address == 10)
                vsh_mask_write(texcoord1, value, output_mask);
            else if (output_address == 11)
                vsh_mask_write(texcoord2, value, output_mask);
            else if (output_address == 12)
                vsh_mask_write(texcoord3, value, output_mask);
        }
        if (vsh_field(instruction, 3, 0, 1))
            break;
    }

    if (asuint(position.w) == 0)
        position.w = 1.0;
    position.x *= x_vsh_info.x;
    position.y *= x_vsh_info.y;

    ExtendedPixelInput output;
    float w = position.w;
    float x = position.x * x_surface.z * 2.0 - 1.0;
    float y = 1.0 - position.y * x_surface.w * 2.0;
    float depth = (position.z - x_depth.x) * x_depth.y;
    float z = x_depth.z > 0.5 ? saturate(depth) : depth;
    output.position = float4(x * w, y * w, z * w, w);
    output.clip.x = w - 1e-3;
    output.clip.y = x_vsh_info.z > 0.5
                  ? (position.z - x_vsh_info.w) * w : 1.0;
    output.d0 = saturate(diffuse);
    output.d1 = saturate(specular);
    output.fog = vsh_fog_factor(fog.x);
    output.tc0 = texcoord0;
    output.tc1 = texcoord1;
    output.tc2 = texcoord2;
    output.tc3 = texcoord3;
    return output;
}

ExtendedPixelInput vsh_vs(uint vertex_id : SV_VertexID)
{
    return vsh_run(vertex_id);
}

[maxvertexcount(3)]
void vsh_gs(triangle ExtendedPixelInput input[3],
            inout TriangleStream<ExtendedPixelInput> stream)
{
    [unroll]
    for (uint i = 0; i < 3; ++i)
        if (any(!isfinite(input[i].position)) ||
            any(!isfinite(input[i].clip)))
            return;
    stream.Append(input[0]);
    stream.Append(input[1]);
    stream.Append(input[2]);
}

[[vk::binding(14, 0)]]
RWByteAddressBuffer vsh_output;

[numthreads(64, 1, 1)]
void vsh_verify_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint vertex_id = dispatch_id.x;
    if (vertex_id >= x_vsh_verify.x)
        return;
    ExtendedPixelInput output = vsh_run(vertex_id);
    uint offset = vertex_id * 128u;
    vsh_output.Store4(offset, asuint(output.position));
    vsh_output.Store4(offset + 16u, asuint(output.d0));
    vsh_output.Store4(offset + 32u, asuint(output.d1));
    vsh_output.Store4(offset + 48u,
                      asuint(float4(output.fog, output.clip, 0.0)));
    vsh_output.Store4(offset + 64u, asuint(output.tc0));
    vsh_output.Store4(offset + 80u, asuint(output.tc1));
    vsh_output.Store4(offset + 96u, asuint(output.tc2));
    vsh_output.Store4(offset + 112u, asuint(output.tc3));
}

float4 sample_texture_stage(uint stage, float2 uv)
{
    if (stage == 0)
        return texture0.Sample(sampler0, uv);
    if (stage == 1)
        return texture1.Sample(sampler1, uv);
    if (stage == 2)
        return texture2.Sample(sampler2, uv);
    return texture3.Sample(sampler3, uv);
}

float4 sample_cube_stage(uint stage, float3 direction)
{
    if (stage == 0)
        return texture_cube0.Sample(sampler0, direction);
    if (stage == 1)
        return texture_cube1.Sample(sampler1, direction);
    if (stage == 2)
        return texture_cube2.Sample(sampler2, direction);
    return texture_cube3.Sample(sampler3, direction);
}

float map_combiner_input(float value, uint mapping)
{
    float positive = max(value, 0.0);
    if (mapping == 0)
        return positive;
    if (mapping == 1)
        return 1.0 - saturate(value);
    if (mapping == 2)
        return 2.0 * positive - 1.0;
    if (mapping == 3)
        return -2.0 * positive + 1.0;
    if (mapping == 4)
        return positive - 0.5;
    if (mapping == 5)
        return -positive + 0.5;
    if (mapping == 6)
        return value;
    return -value;
}

float3 read_combiner_rgb(float4 registers[16], uint input)
{
    float3 value = (input & 0x10) != 0
                 ? registers[input & 0xF].aaa
                 : registers[input & 0xF].rgb;
    uint mapping = (input >> 5) & 7;
    return float3(map_combiner_input(value.x, mapping),
                  map_combiner_input(value.y, mapping),
                  map_combiner_input(value.z, mapping));
}

float read_combiner_alpha(float4 registers[16], uint input)
{
    float value = (input & 0x10) != 0
                ? registers[input & 0xF].a
                : registers[input & 0xF].b;
    return map_combiner_input(value, (input >> 5) & 7);
}

float3 map_combiner_output(float3 value, uint mapping)
{
    if (mapping == 0x08)
        return value - 0.5;
    if (mapping == 0x10)
        return value * 2.0;
    if (mapping == 0x18)
        return (value - 0.5) * 2.0;
    if (mapping == 0x20)
        return value * 4.0;
    if (mapping == 0x30)
        return value * 0.5;
    return value;
}

uint packed_combiner_word(uint4 first, uint4 second, uint index)
{
    return index < 4 ? first[index] : second[index - 4];
}

float4 extended_ps(ExtendedPixelInput input) : SV_Target
{
    float4 coordinates[4] = { input.tc0, input.tc1, input.tc2, input.tc3 };
    float4 sampled[4];
    float4 registers[16];
    uint stage;

    [unroll]
    for (stage = 0; stage < 4; ++stage) {
        uint mode = (x_final_control.w >> (stage * 5)) & 0x1F;
        if (mode == 0) {
            sampled[stage] = float4(0.0, 0.0, 0.0, 1.0);
        } else if (mode >= 1 && mode <= 3) {
            if ((x_texture_state[stage] & 1) == 0) {
                sampled[stage] = 1.0;
            } else if (mode == 3 && (x_texture_state[stage] & 4) != 0) {
                sampled[stage] =
                    sample_cube_stage(stage, coordinates[stage].xyz);
            } else {
                float2 uv;
                if (mode == 3) {
                    uv = coordinates[stage].xy;
                } else {
                    float q = coordinates[stage].w != 0.0
                            ? coordinates[stage].w : 1.0;
                    uv = coordinates[stage].xy / q;
                }
                if ((x_texture_state[stage] & 2) == 0)
                    uv *= x_texture_info[stage].xy;
                sampled[stage] = sample_texture_stage(stage, uv);
            }
        } else if (mode == 4) {
            sampled[stage] = saturate(coordinates[stage]);
        } else if (mode == 5) {
            sampled[stage] = 0.0;
            [unroll]
            for (uint component = 0; component < 4; ++component) {
                bool discard_nonnegative =
                    ((x_clip_state.x >> (stage * 4 + component)) & 1) != 0;
                if (discard_nonnegative
                        ? coordinates[stage][component] >= 0.0
                        : coordinates[stage][component] < 0.0)
                    discard;
            }
        } else {
            sampled[stage] = 0.0;
        }
    }

    [unroll]
    for (stage = 0; stage < 16; ++stage)
        registers[stage] = 0.0;
    registers[3] = float4(x_fog_color.rgb, input.fog);
    registers[4] = input.d0;
    registers[5] = input.d1;
    registers[8] = sampled[0];
    registers[9] = sampled[1];
    registers[10] = sampled[2];
    registers[11] = sampled[3];
    registers[12].a = (x_final_control.w & 0x1F) != 0
                    ? sampled[0].a : 1.0;

    uint count = min(x_final_control.z & 0xFF, 8);
    uint control_flags = x_final_control.z >> 8;
    [loop]
    for (stage = 0; stage < count; ++stage) {
        uint color_icw = packed_combiner_word(
            x_color_icw[0], x_color_icw[1], stage);
        uint alpha_icw = packed_combiner_word(
            x_alpha_icw[0], x_alpha_icw[1], stage);
        uint color_ocw = packed_combiner_word(
            x_color_ocw[0], x_color_ocw[1], stage);
        uint alpha_ocw = packed_combiner_word(
            x_alpha_ocw[0], x_alpha_ocw[1], stage);
        uint color_flags = color_ocw >> 12;
        uint alpha_flags = alpha_ocw >> 12;
        bool mux = (control_flags & 1) != 0
                 ? registers[12].a >= 0.5
                 : (((int)(registers[12].a * 255.0)) & 1) != 0;
        registers[1] = x_c0[(control_flags & 0x010) != 0 ? stage : 0];
        registers[2] = x_c1[(control_flags & 0x100) != 0 ? stage : 0];

        float3 a = read_combiner_rgb(registers, color_icw >> 24);
        float3 b = read_combiner_rgb(registers, color_icw >> 16);
        float3 c = read_combiner_rgb(registers, color_icw >> 8);
        float3 d = read_combiner_rgb(registers, color_icw);
        float alpha_a = read_combiner_alpha(registers, alpha_icw >> 24);
        float alpha_b = read_combiner_alpha(registers, alpha_icw >> 16);
        float alpha_c = read_combiner_alpha(registers, alpha_icw >> 8);
        float alpha_d = read_combiner_alpha(registers, alpha_icw);
        float3 product_ab = (color_flags & 2) != 0
                          ? dot(a, b).xxx : a * b;
        float3 product_cd = (color_flags & 1) != 0
                          ? dot(c, d).xxx : c * d;
        float3 sum = (color_flags & 4) != 0
                   ? (mux ? product_cd : product_ab)
                   : product_ab + product_cd;
        float3 output_ab = clamp(
            map_combiner_output(product_ab, color_flags & 0x38), -1.0, 1.0);
        float3 output_cd = clamp(
            map_combiner_output(product_cd, color_flags & 0x38), -1.0, 1.0);
        float3 output_sum = clamp(
            map_combiner_output(sum, color_flags & 0x38), -1.0, 1.0);
        float alpha_product_ab = alpha_a * alpha_b;
        float alpha_product_cd = alpha_c * alpha_d;
        float alpha_sum = (alpha_flags & 4) != 0
                        ? (mux ? alpha_product_cd : alpha_product_ab)
                        : alpha_product_ab + alpha_product_cd;
        float output_alpha_ab = clamp(
            map_combiner_output(alpha_product_ab.xxx, alpha_flags & 0x38).x,
            -1.0, 1.0);
        float output_alpha_cd = clamp(
            map_combiner_output(alpha_product_cd.xxx, alpha_flags & 0x38).x,
            -1.0, 1.0);
        float output_alpha_sum = clamp(
            map_combiner_output(alpha_sum.xxx, alpha_flags & 0x38).x,
            -1.0, 1.0);
        uint destination;

        destination = (color_ocw >> 4) & 0xF;
        if (destination != 0) {
            registers[destination].rgb = output_ab;
            if ((color_flags & 0x80) != 0)
                registers[destination].a = output_ab.b;
        }
        destination = color_ocw & 0xF;
        if (destination != 0) {
            registers[destination].rgb = output_cd;
            if ((color_flags & 0x40) != 0)
                registers[destination].a = output_cd.b;
        }
        destination = (color_ocw >> 8) & 0xF;
        if (destination != 0)
            registers[destination].rgb = output_sum;
        destination = (alpha_ocw >> 4) & 0xF;
        if (destination != 0)
            registers[destination].a = output_alpha_ab;
        destination = alpha_ocw & 0xF;
        if (destination != 0)
            registers[destination].a = output_alpha_cd;
        destination = (alpha_ocw >> 8) & 0xF;
        if (destination != 0)
            registers[destination].a = output_alpha_sum;
        registers[0] = 0.0;
    }

    float4 color;
    if (x_final_control.x != 0 || x_final_control.y != 0) {
        uint final0 = x_final_control.x;
        uint final1 = x_final_control.y;
        uint final_flags = final1 & 0xFF;
        registers[1] = x_final_c0;
        registers[2] = x_final_c1;
        float3 sum_a = (final_flags & 0x40) != 0
                     ? 1.0 - registers[5].rgb : registers[5].rgb;
        float3 sum_b = (final_flags & 0x20) != 0
                     ? 1.0 - registers[12].rgb : registers[12].rgb;
        registers[14] = float4((final_flags & 0x80) != 0
                              ? saturate(sum_a + sum_b) : sum_a + sum_b, 0.0);
        float3 e = read_combiner_rgb(registers, final1 >> 24);
        float3 f = read_combiner_rgb(registers, final1 >> 16);
        registers[15] = float4(e * f, 0.0);
        float3 a = read_combiner_rgb(registers, final0 >> 24);
        float3 b = read_combiner_rgb(registers, final0 >> 16);
        float3 c = read_combiner_rgb(registers, final0 >> 8);
        float3 d = read_combiner_rgb(registers, final0);
        float alpha = read_combiner_alpha(registers, final1 >> 8);
        color = saturate(float4(d + c * (1.0 - a) + b * a, alpha));
    } else {
        color = saturate(registers[12]);
    }

    if (x_alpha.z > 0.5) {
        int value = (int)(color.a * 255.0 + 0.5);
        int reference = (int)x_alpha.y;
        int function = (int)x_alpha.x;
        bool pass = function == 7
            || (function == 1 && value < reference)
            || (function == 2 && value == reference)
            || (function == 3 && value <= reference)
            || (function == 4 && value > reference)
            || (function == 5 && value != reference)
            || (function == 6 && value >= reference);
        if (!pass)
            discard;
    }
    return color;
}
