const uint MESHLET_MAX_VERTICES = 32;
const uint MESHLET_MAX_PRIMITIVES = 32;
const uint MESHLET_MAX_INDICES = MESHLET_MAX_PRIMITIVES * 3;

struct Vertex {
	vec4 position;
	vec4 normal;
	vec2 texCoord;
	vec2 pad;
};

struct Meshlet
{
	uint vertexCount;
	uint indexCount;
	int diffuseIndex;
	int specularIndex;
	vec4 diffuseColor;
	vec4 specularColor;
	uint vertices[MESHLET_MAX_VERTICES];
	uint indices[MESHLET_MAX_INDICES];
};

struct MeshletInfo {
	vec4 boundingSphere;
};

struct Payload {
	uint baseID;
	uint modelIndex;
	uint subIDs[32];
};