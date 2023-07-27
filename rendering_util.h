#pragma once
#include <vulkan/vulkan.h>
#include <vector>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/euler_angles.hpp>
#include "util.h"
#include <mutex>
#include <set>

struct UniformMatrices {
	glm::mat4 view;
	glm::mat4 projection;
};

inline glm::mat4 createModelMatrix(glm::vec3 pos, glm::vec3 scale, glm::vec3 ypr) {
	glm::mat4 orientationMatrix = glm::eulerAngleYXZ(ypr[0], ypr[1], ypr[2]);
	glm::mat4 translateMatrix = glm::translate(glm::mat4(1.0f), pos);
	glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), scale);
	return translateMatrix * orientationMatrix * scaleMatrix;
}

class DescriptorAllocator {
public:
	//for bindless use VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT in flags
	void create(VkDevice device, VkDescriptorPoolCreateFlags flags, uint32_t maxSets, std::vector<VkDescriptorPoolSize> poolSizes);
	void destroy();
	void resetDescriptorPool();
	//If you have a variable descriptor array (bindless) then include variableDescriptorAllocInfo for each descriptor set 
	//being allocated that contains one.
	//The variable descriptor array must occupy the furthest binding number in the set.
	//The descriptor layout must have been created with VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT flag and pNext
	//containing a VkDescriptorSetLayoutBindingFlagsCreateInfo for the variable array with flags equal to VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT 
	//| VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT.
	void allocateSet(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetVariableDescriptorCountAllocateInfo* variableDescriptorAllocInfo,
		VkDescriptorSet& dstSet);
private:
	VkDevice device = VK_NULL_HANDLE;
	VkDescriptorPoolCreateInfo createInfo{};
	std::vector<VkDescriptorPool> descriptorPools;
	size_t currentPool = 0;
};

class CommandPool {
public:
	void create(VkDevice device, VkCommandPoolCreateInfo& createInfo, VkCommandBufferLevel level) {
		VK_CHECK(vkCreateCommandPool(device, &createInfo, nullptr, &commandPool));
		this->level = level;
		this->device = device;
	}
	std::vector<VkCommandBuffer>& getCommandBuffers(uint32_t numCommandBuffers);
	void destroy() {
		vkDestroyCommandPool(device, commandPool, nullptr);
		commandBuffers.clear();
	}
private:
	VkDevice device = nullptr;
	VkCommandBufferLevel level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	VkCommandPool commandPool = VK_NULL_HANDLE;
	std::vector<VkCommandBuffer> commandBuffers;
};

class GraphicsPipelineBuilder {
public:
	GraphicsPipelineBuilder(VkDevice device) : device{ device } {
		inputAssemblyStateCreateInfo.primitiveRestartEnable = VK_FALSE;
		inputAssemblyStateCreateInfo.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
		rasterizationStateCreateInfo.cullMode = VK_CULL_MODE_NONE;
		rasterizationStateCreateInfo.depthBiasEnable = VK_FALSE;
		rasterizationStateCreateInfo.depthClampEnable = VK_FALSE;
		rasterizationStateCreateInfo.lineWidth = 1.0f;
		rasterizationStateCreateInfo.rasterizerDiscardEnable = VK_FALSE;
		rasterizationStateCreateInfo.polygonMode = VK_POLYGON_MODE_FILL;
		rasterizationStateCreateInfo.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
		multisampleStateCreateInfo.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
		multisampleStateCreateInfo.sampleShadingEnable = VK_FALSE;
		multisampleStateCreateInfo.minSampleShading = 1.0f;
		multisampleStateCreateInfo.alphaToOneEnable = VK_FALSE;
		multisampleStateCreateInfo.alphaToCoverageEnable = VK_FALSE;
		depthStencilStateCreateInfo.depthTestEnable = VK_TRUE;
		depthStencilStateCreateInfo.depthWriteEnable = VK_TRUE;
		depthStencilStateCreateInfo.depthCompareOp = VK_COMPARE_OP_LESS;
		depthStencilStateCreateInfo.minDepthBounds = 0.0f;
		depthStencilStateCreateInfo.maxDepthBounds = 1.0f;
	}
	//default flags 0
	void setFlags(VkPipelineCreateFlags flags) { this->flags = flags; }
	void addShaderStage(VkShaderStageFlagBits stage, const char* sourceFileName,
		const char* entryPointName = "main", const VkSpecializationInfo* specializationInfo = nullptr) {
		VkPipelineShaderStageCreateInfo shaderStageCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
		shaderStageCreateInfo.stage = stage;
		if (stage == VK_SHADER_STAGE_MESH_BIT_EXT || stage == VK_SHADER_STAGE_TASK_BIT_EXT) containsMeshShader = true;
		std::vector<char> shaderSource = readSourceFile(sourceFileName);
		VkShaderModule shaderModule = createShaderModule(device, shaderSource);
		shaderModules.push_back(shaderModule);
		shaderStageCreateInfo.module = shaderModule;
		shaderStageCreateInfo.pName = entryPointName;
		shaderStageCreateInfo.pSpecializationInfo = specializationInfo;
		shaderStageCreateInfos.push_back(shaderStageCreateInfo);
	}
	void addVertexInputBinding(uint32_t binding, uint32_t stride, VkVertexInputRate inputRate) {
		vertexInputBindingDescriptions.push_back({ binding, stride, inputRate });
	}
	void addVertexAttribute(uint32_t location, uint32_t binding, VkFormat format, uint32_t offset) {
		vertexAttributeDescriptions.push_back({ location, binding, format, offset });
	}
	//default is VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST and VK_FALSE
	void setPrimitiveTopology(VkPrimitiveTopology primitive, VkBool32 primitiveRestartEnable) {
		inputAssemblyStateCreateInfo.topology = primitive;
		inputAssemblyStateCreateInfo.primitiveRestartEnable = primitiveRestartEnable;
	}
	void setViewportScissors(uint32_t viewportCount, const VkViewport* viewports, uint32_t scissorCount, const VkRect2D* scissors) {
		viewportStateCreateInfo.viewportCount = viewportCount;
		viewportStateCreateInfo.pViewports = viewports;
		viewportStateCreateInfo.scissorCount = scissorCount;
		viewportStateCreateInfo.pScissors = scissors;
	}
	//default VK_FALSE
	void enableRasterizationDiscard(VkBool32 value) { rasterizationStateCreateInfo.rasterizerDiscardEnable = value; }
	//default VK_FALSE
	void enableDepthBias(VkBool32 value) { rasterizationStateCreateInfo.depthBiasEnable = value; }
	//default VK_FALSE
	void enableDepthClamp(VkBool32 value) { rasterizationStateCreateInfo.depthClampEnable = value; }
	//default VK_CULL_MODE_NONE
	void setCullMode(VkCullModeFlags flags) { rasterizationStateCreateInfo.cullMode = flags; }
	//default VK_FRONT_FACE_COUNTER_CLOCKWISE
	void setFrontFace(VkFrontFace face) { rasterizationStateCreateInfo.frontFace = face; }
	//default VK_POLYGON_MODE_FILL
	void setPolygonMode(VkPolygonMode polygonMode) { rasterizationStateCreateInfo.polygonMode = polygonMode; }
	//default 1.0
	void setLineWidth(float width) { rasterizationStateCreateInfo.lineWidth = width; }
	//default all 0
	void setDepthBiasFactors(float constantFactor, float slopeFactor, float clamp) {
		rasterizationStateCreateInfo.depthBiasConstantFactor = constantFactor;
		rasterizationStateCreateInfo.depthBiasSlopeFactor = slopeFactor;
		rasterizationStateCreateInfo.depthBiasClamp = clamp;
	}
	//default VK_SAMPLE_COUNT_1_BIT
	void setSamples(VkSampleCountFlagBits rasterizationSamples) { multisampleStateCreateInfo.rasterizationSamples = rasterizationSamples; }
	//default VK_FALSE
	void enableSampleRateShading(VkBool32 value) { multisampleStateCreateInfo.sampleShadingEnable = value; }
	//default 1.0
	void setMinSampleShadingRate(float value) { multisampleStateCreateInfo.minSampleShading = value; }
	//default nullptr
	void setSampleMask(const VkSampleMask* sampleMask) { multisampleStateCreateInfo.pSampleMask = sampleMask; }
	//default VK_FALSE
	void enableAlphaToCoverage(VkBool32 value) { multisampleStateCreateInfo.alphaToCoverageEnable = value; }
	//default VK_FALSE
	void enableAlphaToOne(VkBool32 value) { multisampleStateCreateInfo.alphaToOneEnable = value; }
	//default VK_TRUE
	void enableDepthTest(VkBool32 value) { depthStencilStateCreateInfo.depthTestEnable = value; }
	//default VK_TRUE
	void enableDepthWrite(VkBool32 value) { depthStencilStateCreateInfo.depthWriteEnable = value; }
	//default VK_FALSE
	void enableDepthBoundsTest(VkBool32 value) { depthStencilStateCreateInfo.depthBoundsTestEnable = value; }
	//default VK_FALSE
	void enableStencilTest(VkBool32 value) { depthStencilStateCreateInfo.stencilTestEnable = value; }
	//default VK_COMPARE_OP_LESS
	void setDepthCompareOp(VkCompareOp op) { depthStencilStateCreateInfo.depthCompareOp = op; }
	//default value
	void setStencilOpState(VkStencilOpState front, VkStencilOpState back) {
		depthStencilStateCreateInfo.front = front;
		depthStencilStateCreateInfo.back = back;
	}
	//default 0.0, 1.0
	void setDepthBounds(float min, float max) {
		depthStencilStateCreateInfo.minDepthBounds = min;
		depthStencilStateCreateInfo.maxDepthBounds = max;
	}
	void addColorBlendAttachmentState(VkPipelineColorBlendAttachmentState& attachment) { colorBlendAttachments.push_back(attachment); }
	//default 0.0 0.0 0.0 0.0
	void setColorBlendConstants(float blendConstants[4]) { memcpy(colorBlendStateCreateInfo.blendConstants, blendConstants, sizeof(float[4])); }
	//default false
	void enableLogicOp(VkBool32 value) { colorBlendStateCreateInfo.logicOpEnable = value; }
	void setLogicOp(VkLogicOp op) { colorBlendStateCreateInfo.logicOp = op; }
	void addDynamicState(VkDynamicState state) { dynamicStates.push_back(state); }
	void setPipelineLayout(VkPipelineLayout pipelineLayout) { this->pipelineLayout = pipelineLayout; }
	void setRenderPass(VkRenderPass renderPass) { this->renderPass = renderPass; }
	void setSubpass(uint32_t subpass) { this->subpass = subpass; }
	/*
	void setDynamicRenderPass(VkPipelineRenderingCreateInfoKHR renderingCreateInfo) {
		assert(renderPass == VK_NULL_HANDLE);
		usesDynamicRendering = true;
		pipelineRenderingCreateInfo = renderingCreateInfo;
	}
	*/
	void build(VkPipelineCache pipelineCache, VkPipeline& pipeline);
private:
	bool containsMeshShader = false;
	VkDevice device = nullptr;
	VkPipelineCreateFlags flags = 0;
	VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
	VkRenderPass renderPass = VK_NULL_HANDLE;
	uint32_t subpass = 0;
	VkPipelineRenderingCreateInfoKHR pipelineRenderingCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR };
	//bool usesDynamicRendering = false;
	std::vector<VkShaderModule> shaderModules;
	std::vector<VkPipelineShaderStageCreateInfo> shaderStageCreateInfos;
	std::vector<VkVertexInputBindingDescription> vertexInputBindingDescriptions;
	std::vector<VkVertexInputAttributeDescription> vertexAttributeDescriptions;
	std::vector<VkPipelineColorBlendAttachmentState> colorBlendAttachments;
	std::vector<VkDynamicState> dynamicStates;
	VkPipelineInputAssemblyStateCreateInfo inputAssemblyStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
	VkPipelineViewportStateCreateInfo viewportStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
	VkPipelineRasterizationStateCreateInfo rasterizationStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
	VkPipelineMultisampleStateCreateInfo multisampleStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
	VkPipelineDepthStencilStateCreateInfo depthStencilStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO };
	VkPipelineColorBlendStateCreateInfo colorBlendStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
};

class TextureIndexPool {
	std::mutex lock;
	std::set<uint32_t> freeTextureIndices; //indices that are unused in the bindless texture descriptor set
public:
	void init() {
		for (uint32_t i = 0; i < MAX_TEXTURES; i++) {
			freeTextureIndices.insert(i);
		}
	}
	int32_t getTextureIndex() {
		std::unique_lock<std::mutex> acquiredLock(lock);
		auto it = freeTextureIndices.begin();
		if (it == freeTextureIndices.end()) return -1;
		else {
			int32_t ret = *it;
			freeTextureIndices.erase(it);
			return ret;
		}
	}
	void freeTextureIndex(int32_t index) {
		std::unique_lock<std::mutex> acquiredLock(lock);
		if (index >= 0 && index < MAX_TEXTURES) freeTextureIndices.insert(index);
	}
};

extern const float cubeVertices[32];

extern const uint32_t cubeIndices[36];