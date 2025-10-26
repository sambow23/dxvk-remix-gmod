#version 450

layout(constant_id = 1) const bool s_gamma_bound = true;
layout(constant_id = 1225) const uint c_samples = 0;

layout(binding = 0) uniform sampler2DMS s_image;
layout(binding = 1) uniform sampler1D s_gamma;

layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform present_info_t {
  ivec2 src_offset;
  ivec2 dst_offset;
  uint flip_horizontal;
  uint flip_vertical;
};

void main() {
  ivec2 coord = ivec2(gl_FragCoord.xy) + src_offset - dst_offset;
  
  // Apply image flipping if enabled
  if (flip_horizontal != 0u || flip_vertical != 0u) {
    ivec2 imageSize = textureSize(s_image);
    if (flip_horizontal != 0u) {
      coord.x = imageSize.x - 1 - coord.x;
    }
    if (flip_vertical != 0u) {
      coord.y = imageSize.y - 1 - coord.y;
    }
  }
  
  o_color = texelFetch(s_image, coord, 0);
  
  for (uint i = 1; i < c_samples; i++)
    o_color += texelFetch(s_image, coord, int(i));

  o_color /= float(c_samples);

  if (s_gamma_bound) {
    o_color = vec4(
      texture(s_gamma, o_color.r).r,
      texture(s_gamma, o_color.g).g,
      texture(s_gamma, o_color.b).b,
      o_color.a);
  }
}