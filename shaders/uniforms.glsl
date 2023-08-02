const uint MESHLET_MAX_VERTICES = 32;
const uint MESHLET_MAX_PRIMITIVES = 32;
const uint MESHLET_MAX_INDICES = MESHLET_MAX_PRIMITIVES * 3;

struct Vertex {
	vec4 position;
	vec4 normal;
	vec2 texCoord;
	int materialIndex;
	int pad;
};

struct Material {
	vec4 diffuseColor;
	vec4 specularColor;
	int diffuseIndex;
	int specularIndex;
	int pad1;
	int pad2;
};

struct Meshlet
{
	vec4 boundingSphere;
	uint vertexCount;
	uint indexCount;
	uint vertices[MESHLET_MAX_VERTICES];
	uint indices[MESHLET_MAX_INDICES];
	uint pad1;
	uint pad2;
};

struct Payload {
	uint baseID;
	uint modelIndex;
	uint subIDs[32];
};