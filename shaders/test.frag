#version 460
 
layout (location = 0) in VertexInput {
  vec3 pos;
} vertexInput;

layout(location = 0) out vec4 FragColor;
 

void main()
{
	FragColor = vec4(normalize(vertexInput.pos), 1.0f);
}