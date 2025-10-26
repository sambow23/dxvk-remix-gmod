#version 450

layout(constant_id = 1) const bool s_gamma_bound = true;

layout(binding = 0) uniform sampler2D s_image;
layout(binding = 1) uniform sampler1D s_gamma;

layout(location = 0) in  vec2 i_coord;
layout(location = 0) out vec4 o_color;

layout(push_constant)
uniform present_info_t {
  ivec2 src_offset;
  uvec2 src_extent;
  uint flip_horizontal;
  uint flip_vertical;
};

void main() {
  vec2 coord = i_coord;
  
  // Apply image flipping if enabled
  if (flip_horizontal != 0u) {
    coord.x = 1.0 - coord.x;
  }
  if (flip_vertical != 0u) {
    coord.y = 1.0 - coord.y;
  }
  
  coord = vec2(src_offset) + vec2(src_extent) * coord;
  o_color = textureLod(s_image, coord, 0.0f);
  
  if (s_gamma_bound) {
    o_color = vec4(
      texture(s_gamma, o_color.r).r,
      texture(s_gamma, o_color.g).g,
      texture(s_gamma, o_color.b).b,
      o_color.a);
  }
}