//zeux.io/2023/01/12/approximate-projected-bounds/
// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
//c is view space center
//aabb = (minx, miny, maxx, maxy), [0,1]
bool projectSphere(vec3 c, float r, float znear, float P00, float P11, out vec4 aabb)
{
    if (c.z < r + znear) return false;

    vec3 cr = c * r;
    float czr2 = c.z * c.z - r * r;

    float vx = sqrt(c.x * c.x + czr2);
    float minx = (vx * c.x - cr.z) / (vx * c.z + cr.x);
    float maxx = (vx * c.x + cr.z) / (vx * c.z - cr.x);

    float vy = sqrt(c.y * c.y + czr2);
    float miny = (vy * c.y - cr.z) / (vy * c.z + cr.y);
    float maxy = (vy * c.y + cr.z) / (vy * c.z - cr.y);

    aabb = vec4(minx * P00, miny * P11, maxx * P00, maxy * P11);
    // clip space -> uv space
    aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f);

    return true;
}

//aabb = (minx, miny, maxx, maxy), [0,1]
bool intersect(vec4 a, vec4 b) {
    if (a[2] < b[0] || a[0] > b[2]) return false;
    if (a[3] < b[1] || a[1] > b[3]) return false;
    return true;
}


bool intervalIntersect(vec2 a, vec2 b) {
	if (a[1] < b[0] || a[0] > b[1]) return false;
	else return true;
}

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
void getBoundsForAxis(vec3 a, vec3 C, float r, float nearZ, out vec3 L, out vec3 U) {
	vec2 c = vec2(dot(a, C), C.z); // C in the a-z frame
	vec2 bounds[2]; // In the a-z reference frame
	const float tSquared = dot(c, c) - r * r;
	const bool cameraInsideSphere = (tSquared <= 0);

	// (cos, sin) of angle theta between c and a tangent vector
	vec2 v = cameraInsideSphere ?
		vec2(0.0f, 0.0f) :
		vec2(sqrt(tSquared), r) / c.length();

	const bool clipSphere = (c.y + r >= nearZ);
	float temp = nearZ - c.y;
	float k = sqrt(r * r - temp * temp);

	for (int i = 0; i < 2; ++i) {
		if (!cameraInsideSphere)
			bounds[i] = mat2(v.x, -v.y,
				v.y, v.x) * c * v.x;
		const bool clipBound =
			cameraInsideSphere || (bounds[i].y > nearZ);

		if (clipSphere && clipBound)
			bounds[i] = vec2(c.x + k, nearZ);

		v.y = -v.y; k = -k;
	}

	// Transform back to camera space
	L = bounds[1].x * a; L.z = bounds[1].y;
	U = bounds[0].x * a; U.z = bounds[0].y;
}

// 2D Polyhedral Bounds of a Clipped, Perspective-Projected 3D Sphere. Michael Mara, Morgan McGuire. 2013
void getSphereBoundsMara(vec3 C, float r, float nearZ, mat4 proj, out vec4 aabb) {
	vec3 x1, x2;
	vec3 y1, y2;
	getBoundsForAxis(vec3(1.f, 0.f, 0.f), C, r, nearZ, x1, x2);
	getBoundsForAxis(vec3(0.f, 1.f, 0.f), C, r, nearZ, y1, y2);
	vec4 x1_proj = proj * vec4(x1, 1.0f);
	x1_proj /= x1_proj.w;
	vec4 x2_proj = proj * vec4(x2, 1.0f);
	x2_proj /= x2_proj.w;
	vec4 y1_proj = proj * vec4(y1, 1.0f);
	y1_proj /= y1_proj.w;
	vec4 y2_proj = proj * vec4(y2, 1.0f);
	y2_proj /= y2_proj.w;
	aabb = vec4(x1_proj.x, y1_proj.y, x2_proj.x, y2_proj.y);
	aabb = aabb.xwzy * vec4(0.5f, -0.5f, 0.5f, -0.5f) + vec4(0.5f);
}

/*
void getSphereBounds() {

	vec3 points[8];
	points[0] = c + vec3(r, r, r);
	points[1] = c + vec3(-r, r, r);
	points[2] = c + vec3(-r, r, -r);
	points[3] = c + vec3(r, r, -r);
	points[4] = c + vec3(r, -r, r);
	points[5] = c + vec3(-r, -r, r);
	points[6] = c + vec3(r, -r, -r);
	points[7] = c + vec3(-r, -r, -r);
	vec4 p = viewProj * vec4(points[0], 1.0f);
	p /= p.w;
	float minX = p.x;
	float maxX = p.x;
	float minY = p.y;
	float maxY = p.y;
	float maxZ = p.z;
	float minZ = p.z;
	for (int i = 1; i < 8; i++) {
		p = viewProj * vec4(points[i], 1.0f);
		p /= p.w;
		minX = min(minX, p.x);
		maxX = max(maxX, p.x);
		minY = min(minY, p.y);
		maxY = max(maxY, p.y);
		minZ = min(minZ, p.z);
		maxZ = max(maxZ, p.z);
	}
	render = render && intervalIntersect(vec2(minX, maxX), vec2(-1.f, 1.f));
	render = render && intervalIntersect(vec2(minY, maxY), vec2(-1.f, 1.f));
	render = render && intervalIntersect(vec2(minZ, maxZ), vec2(0.f, 1.f));
}
*/

//Real-time collision detection
//aabb = [minx, maxx, miny, maxy, minz, maxz]
void closestPtPointAABB(vec3 p, float aabb[6], out vec3 q) {
	for (int i = 0; i < 3; i++) {
		float v = p[i];
		if (v < aabb[2*i]) v = aabb[2 * i]; // v = max(v, b.min[i])
		if (v > aabb[2*i+1]) v = aabb[2 * i + 1]; // v = min(v, b.max[i])
		q[i] = v;
	}
}

//Real-time collision detection
//aabb = [minx, maxx, miny, maxy, minz, maxz]
float sqDistPointAABB(vec3 p, float aabb[6]) {
	float sqDist = 0.0f;
	for (int i = 0; i < 3; i++) {
		float v = p[i];
		if (v < aabb[2 * i]) sqDist += (aabb[2 * i] - v) * (aabb[2 * i] - v);
		if (v > aabb[2 * i + 1]) sqDist += (v - aabb[2 * i + 1]) * (v - aabb[2 * i + 1]);
	}
	return sqDist;
}

//Real-time collision detection
//aabb = [minx, maxx, miny, maxy, minz, maxz]
bool sphereAABBIntersect(vec4 sphere, float aabb[6]) {
	float sqDist = sqDistPointAABB(sphere.xyz, aabb);
	return sqDist <= sphere.w * sphere.w;
}