#pragma once

#include <unordered_set>
#include <unordered_map>
#include "util.h"

class TransformComponent {
public:
	glm::vec3 position{ 0.0f };
	glm::vec3 scale{ 1.0f };
	glm::vec3 ypr{ 0.0f }; //yaw, pitch, roll
};

class MeshInstanceComponent {
	friend class EntityManager;
	uint32_t meshIndex = UINT32_MAX; //index into meshes map
	uint32_t modelIndex = UINT32_MAX; //index into model storage buffer
public:
	inline uint32_t getMeshIndex() const { return meshIndex; }
	inline uint32_t getModelIndex() const { return modelIndex; }
};

class EntityManager {
	std::unordered_set<uint32_t> entities;
	std::unordered_map<uint32_t, MeshInstanceComponent> meshInstanceComponents; //indexed by entityIndex
	std::unordered_map<uint32_t, TransformComponent> transformComponents; //indexed by entityIndex
	uint32_t numEntitiesCreated = 0;
public:
	inline bool isEmpty() {
		return entities.empty();
	}
	uint32_t createEntity() {
		entities.insert(numEntitiesCreated);
		return numEntitiesCreated++;
	}
	bool destroyEntity(uint32_t entity) {
		auto it = entities.find(entity);
		if (it == entities.end()) return false;
		else {
			entities.erase(it);
			auto meshInstanceIt = meshInstanceComponents.find(entity);
			if (meshInstanceIt != meshInstanceComponents.end()) {
				meshInstanceComponents.erase(meshInstanceIt);
			}
			auto transformIt = transformComponents.find(entity);
			if (transformIt != transformComponents.end()) {
				transformComponents.erase(transformIt);
			}
			return true;
		}
	}
	bool isValidEntity(uint32_t entity) {
		return (entities.find(entity) != entities.end());
	}
	bool setTransform(uint32_t entity, glm::vec3 position, glm::vec3 scale, glm::vec3 ypr) {
		if (entities.find(entity) == entities.end()) return false;
		auto it = transformComponents.find(entity);
		if (it == transformComponents.end()) {
			TransformComponent transform;
			transform.position = position;
			transform.scale = scale;
			transform.ypr = ypr;
			transformComponents.emplace(entity, transform);
		}
		else {
			TransformComponent& transform = it->second;
			transform.position = position;
			transform.scale = scale;
			transform.ypr = ypr;
		}
		return true;
	}
	bool getTransform(uint32_t entity, TransformComponent& transform) {
		if (entities.find(entity) == entities.end()) return false;
		auto it = transformComponents.find(entity);
		if (it == transformComponents.end()) return false;
		else {
			transform = it->second;
			return true;
		}
	}
	bool setMeshInstance(uint32_t entity, uint32_t meshIndex, uint32_t modelIndex) {
		if (entities.find(entity) == entities.end()) return false;
		auto it = meshInstanceComponents.find(entity);
		if (it == meshInstanceComponents.end()) {
			MeshInstanceComponent meshInstance;
			meshInstance.meshIndex = meshIndex;
			meshInstance.modelIndex = modelIndex;
			meshInstanceComponents.emplace(entity, meshInstance);
		}
		else {
			MeshInstanceComponent& meshInstance = it->second;
			meshInstance.meshIndex = meshIndex;
			meshInstance.modelIndex = modelIndex;
		}
		return true;
	}
	bool getMeshInstance(uint32_t entity, MeshInstanceComponent& meshInstance) {
		if (entities.find(entity) == entities.end()) return false;
		auto it = meshInstanceComponents.find(entity);
		if (it == meshInstanceComponents.end()) return false;
		else {
			meshInstance = it->second;
			return true;
		}
	}
	inline auto meshInstancesBegin() {
		return meshInstanceComponents.begin();
	}
	inline auto meshInstancesEnd() {
		return meshInstanceComponents.end();
	}
	inline auto transformsBegin() {
		return transformComponents.begin();
	}
	inline auto transformsEnd() {
		return transformComponents.end();
	}
	inline auto getMeshInstanceIterator(uint32_t entity) {
		if (entities.find(entity) == entities.end()) return meshInstanceComponents.end();
		auto it = meshInstanceComponents.find(entity);
		return it;
	}
	inline auto getTransformIterator(uint32_t entity) {
		if (entities.find(entity) == entities.end()) return transformComponents.end();
		auto it = transformComponents.find(entity);
		return it;
	}
};
