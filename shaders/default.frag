#version 460

#extension GL_EXT_nonuniform_qualifier : enable
 
layout (location = 0) in VertexInput {
  vec3 pos;
  vec3 normal;
  vec2 texCoord;
  flat int diffuseIndex;
  flat int specularIndex;
  flat vec4 diffuseColor;
  flat vec4 specularColor;
} vertexInput;

layout (set = 1, binding = 0) uniform sampler2D textures[];

layout(location = 0) out vec4 FragColor;
 
void main()
{
	
	vec4 outColor;
	if (vertexInput.diffuseIndex >= 0) {
		outColor = texture( textures[vertexInput.diffuseIndex], vertexInput.texCoord );
	} else outColor = vertexInput.diffuseColor;
	if (outColor.a < 0.1) discard;
	FragColor = outColor;
	//FragColor = vertexInput.diffuseColor;
	
	//FragColor = vec4(normalize(vertexInput.pos), 1.0f);
}