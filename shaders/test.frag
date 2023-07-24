#version 460
 
layout (location = 0) in VertexInput {
  //vec3 pos;
  flat vec3 color;
} vertexInput;

layout(location = 0) out vec4 FragColor;
 

void main()
{
	//FragColor = vec4(normalize(vertexInput.pos), 1.0f);
	FragColor = vec4(vertexInput.color, 1.0f);
}