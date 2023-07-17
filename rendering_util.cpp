#include "rendering_util.h"
#include "util.h"

void DescriptorAllocator::create(VkDevice device, VkDescriptorPoolCreateFlags flags, uint32_t maxSets, std::vector<VkDescriptorPoolSize> poolSizes) {
	this->device = device;
	createInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	createInfo.flags = flags;
	createInfo.maxSets = maxSets;
	createInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
	createInfo.pPoolSizes = poolSizes.data();
	VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
	VK_CHECK(vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool));
	descriptorPools.push_back(descriptorPool);
}

void DescriptorAllocator::destroy() {
	for (VkDescriptorPool descriptorPool : descriptorPools) {
		vkDestroyDescriptorPool(device, descriptorPool, nullptr);
	}
}

void DescriptorAllocator::resetDescriptorPool() {
	for (VkDescriptorPool descriptorPool : descriptorPools) {
		vkResetDescriptorPool(device, descriptorPool, 0);
	}
	currentPool = 0;
}

void DescriptorAllocator::allocateSet(VkDescriptorSetLayout descriptorSetLayout, VkDescriptorSetVariableDescriptorCountAllocateInfo* variableDescriptorAllocInfo,
	VkDescriptorSet& dstSet) {
	VkDescriptorSetAllocateInfo allocateInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
	allocateInfo.descriptorPool = descriptorPools.at(currentPool);
	allocateInfo.descriptorSetCount = 1;
	allocateInfo.pSetLayouts = &descriptorSetLayout;
	allocateInfo.pNext = variableDescriptorAllocInfo;
	VkResult result = vkAllocateDescriptorSets(device, &allocateInfo, &dstSet);
	if (result == VK_ERROR_FRAGMENTED_POOL || result == VK_ERROR_OUT_OF_POOL_MEMORY) {
		if (currentPool == descriptorPools.size() - 1) { //on last pool
			VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
			VK_CHECK(vkCreateDescriptorPool(device, &createInfo, nullptr, &descriptorPool));
			descriptorPools.push_back(descriptorPool);
		}
		currentPool++;
		allocateInfo.descriptorPool = descriptorPools.at(currentPool);
		VK_CHECK(vkAllocateDescriptorSets(device, &allocateInfo, &dstSet));
		assert(currentPool < descriptorPools.size());
		
	}

}

std::vector<VkCommandBuffer>& CommandPool::getCommandBuffers(uint32_t numCommandBuffers) {
	VkCommandBufferAllocateInfo allocInfo{};
	allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocInfo.level = level;
	if (commandBuffers.empty()) {
		commandBuffers.resize(numCommandBuffers, nullptr);
		allocInfo.commandBufferCount = numCommandBuffers;
		allocInfo.commandPool = commandPool;
		VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, commandBuffers.data()));
	}
	else if (commandBuffers.size() < static_cast<size_t>(numCommandBuffers)) {
		allocInfo.commandBufferCount = 1;
		allocInfo.commandPool = commandPool;
		size_t curSize = commandBuffers.size();
		for (size_t i = 0; i < static_cast<size_t>(numCommandBuffers) - curSize; i++) {
			VkCommandBuffer commandBuffer = nullptr;
			VK_CHECK(vkAllocateCommandBuffers(device, &allocInfo, &commandBuffer));
			commandBuffers.push_back(commandBuffer);
		}
	}
	return commandBuffers;
}

void GraphicsPipelineBuilder::build(VkPipelineCache pipelineCache, VkPipeline& pipeline) {
	VkGraphicsPipelineCreateInfo graphicsPipelineCreateInfo{ VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
	graphicsPipelineCreateInfo.flags = flags;
	graphicsPipelineCreateInfo.stageCount = static_cast<uint32_t>(shaderStageCreateInfos.size());
	if (!shaderStageCreateInfos.empty()) graphicsPipelineCreateInfo.pStages = shaderStageCreateInfos.data();
	if (!containsMeshShader) {
		VkPipelineVertexInputStateCreateInfo vertexInputCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
		vertexInputCreateInfo.vertexBindingDescriptionCount = static_cast<uint32_t>(vertexInputBindingDescriptions.size());
		if (!vertexInputBindingDescriptions.empty()) vertexInputCreateInfo.pVertexBindingDescriptions = vertexInputBindingDescriptions.data();
		vertexInputCreateInfo.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributeDescriptions.size());
		if (!vertexAttributeDescriptions.empty()) vertexInputCreateInfo.pVertexAttributeDescriptions = vertexAttributeDescriptions.data();
		graphicsPipelineCreateInfo.pVertexInputState = &vertexInputCreateInfo;
		graphicsPipelineCreateInfo.pInputAssemblyState = &inputAssemblyStateCreateInfo;
	}
	graphicsPipelineCreateInfo.pTessellationState = nullptr;
	graphicsPipelineCreateInfo.pViewportState = &viewportStateCreateInfo;
	graphicsPipelineCreateInfo.pRasterizationState = &rasterizationStateCreateInfo;
	graphicsPipelineCreateInfo.pMultisampleState = &multisampleStateCreateInfo;
	graphicsPipelineCreateInfo.pDepthStencilState = &depthStencilStateCreateInfo;
	colorBlendStateCreateInfo.attachmentCount = static_cast<uint32_t>(colorBlendAttachments.size());
	if (!colorBlendAttachments.empty()) colorBlendStateCreateInfo.pAttachments = colorBlendAttachments.data();
	graphicsPipelineCreateInfo.pColorBlendState = &colorBlendStateCreateInfo;
	VkPipelineDynamicStateCreateInfo dynamicStateCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
	dynamicStateCreateInfo.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	if (!dynamicStates.empty()) dynamicStateCreateInfo.pDynamicStates = dynamicStates.data();
	graphicsPipelineCreateInfo.pDynamicState = &dynamicStateCreateInfo;
	graphicsPipelineCreateInfo.layout = pipelineLayout;
	graphicsPipelineCreateInfo.renderPass = renderPass;
	graphicsPipelineCreateInfo.subpass = subpass;
	VK_CHECK(vkCreateGraphicsPipelines(device, pipelineCache, 1, &graphicsPipelineCreateInfo, nullptr, &pipeline));
	for (VkShaderModule shaderModule : shaderModules) {
		vkDestroyShaderModule(device, shaderModule, nullptr);
	}
}

const float cubeVertices[32] =
{
	-0.5f, -0.5f, -0.5f, 1.0, //lower left
	0.5f, -0.5f, -0.5f, 1.0, //lower right
	0.5f, 0.5f, -0.5f, 1.0, //upper right
	-0.5f, 0.5f, -0.5f, 1.0, //upper left
	-0.5f, -0.5f, 0.5f, 1.0,
	0.5f, -0.5f, 0.5f, 1.0,
	0.5f, 0.5f, 0.5f, 1.0,
	-0.5f, 0.5f, 0.5f, 1.0
};

//clockwise
const uint32_t cubeIndices[36] = { 0, 1, 3, 3, 1, 2,
								  1, 5, 2, 2, 5, 6,
								  5, 4, 6, 6, 4, 7,
								  4, 0, 7, 7, 0, 3,
								  3, 2, 7, 7, 2, 6,
								  4, 5, 0, 0, 5, 1 };