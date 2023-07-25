#include "vulkan_renderer.h"
#include <iostream>
#include <fstream>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <tiny_obj_loader.h>
#include <meshoptimizer.h>

void VulkanRenderer::startUp(GLFWwindow* window) {
	vkb::InstanceBuilder instanceBuilder{};
	instanceBuilder.require_api_version(VK_API_VERSION_1_2);
	if (enableValidationLayers) {
		instanceBuilder.enable_validation_layers();
		instanceBuilder.set_debug_messenger_severity(VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT);
		instanceBuilder.set_debug_messenger_type(VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
			VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT);
		for (unsigned int i = 0; i < numValidationFeatures; i++) {
			instanceBuilder.add_validation_feature_enable(validationFeatures[i]);
		}
		instanceBuilder.set_debug_callback(debugCallback);
	}
	auto instanceBuilderRet = instanceBuilder.build();
	if (!instanceBuilderRet) {
		std::cerr << "Failed to create Vulkan instance. Error: " << instanceBuilderRet.error().message() << std::endl;
		exit(EXIT_FAILURE);
	}
	instance = instanceBuilderRet.value();

	if (glfwCreateWindowSurface(instance, window, nullptr, &surface) != VK_SUCCESS) {
		std::cerr << "Failed to create window surface" << std::endl;
		exit(EXIT_FAILURE);
	}

	vkb::PhysicalDeviceSelector physDeviceSelector{ instance };
	physDeviceSelector.set_surface(surface);
	physDeviceSelector.prefer_gpu_device_type(vkb::PreferredDeviceType::discrete);
	physDeviceSelector.require_dedicated_transfer_queue();
	physDeviceSelector.require_present(true);
	for (unsigned int i = 0; i < numDeviceExtensions; i++) {
		physDeviceSelector.add_required_extension(deviceExtensions[i]);
	}
	VkPhysicalDeviceFeatures deviceFeatures{};
	deviceFeatures.fullDrawIndexUint32 = VK_TRUE;
	deviceFeatures.samplerAnisotropy = VK_TRUE;
	deviceFeatures.shaderUniformBufferArrayDynamicIndexing = VK_TRUE;
	deviceFeatures.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
	deviceFeatures.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
	VkPhysicalDeviceVulkan12Features deviceFeatures12{};
	deviceFeatures12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
	deviceFeatures12.runtimeDescriptorArray = VK_TRUE;
	deviceFeatures12.descriptorBindingVariableDescriptorCount = VK_TRUE;
	deviceFeatures12.descriptorBindingPartiallyBound = VK_TRUE;
	deviceFeatures12.samplerFilterMinmax = VK_TRUE;
	deviceFeatures12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE; //ray tracing shaders need to sample textures from any object
	VkPhysicalDeviceExtendedDynamicStateFeaturesEXT extendedDynamicStateFeatures{};
	extendedDynamicStateFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTENDED_DYNAMIC_STATE_FEATURES_EXT;
	extendedDynamicStateFeatures.extendedDynamicState = VK_TRUE;
	VkPhysicalDeviceMeshShaderFeaturesEXT meshShaderFeatures{};
	meshShaderFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT;
	meshShaderFeatures.meshShader = VK_TRUE;
	meshShaderFeatures.taskShader = VK_TRUE;
	VkPhysicalDevicePipelineCreationCacheControlFeaturesEXT cacheControlFeatures{
	VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PIPELINE_CREATION_CACHE_CONTROL_FEATURES_EXT };
	cacheControlFeatures.pipelineCreationCacheControl = VK_TRUE;
	physDeviceSelector.set_required_features(deviceFeatures);
	physDeviceSelector.set_minimum_version(1, 2);
	physDeviceSelector.set_required_features_12(deviceFeatures12);
	physDeviceSelector.add_required_extension_features(extendedDynamicStateFeatures);
	physDeviceSelector.add_required_extension_features(meshShaderFeatures);
	physDeviceSelector.add_required_extension_features(cacheControlFeatures);
	auto physDeviceSelectorRet = physDeviceSelector.select();
	if (!physDeviceSelectorRet) {
		std::cerr << "Unable to find suitable device. Error: " << physDeviceSelectorRet.error().message() << std::endl;
		exit(EXIT_FAILURE);
	}
	physicalDevice = physDeviceSelectorRet.value();
	VkPhysicalDeviceProperties2 physProp{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2 };
	VkPhysicalDeviceMeshShaderPropertiesEXT meshShaderProp{ VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT };
	physProp.pNext = &meshShaderProp;
	vkGetPhysicalDeviceProperties2(physicalDevice, &physProp);
	maxPossibleMeshlets = meshShaderProp.maxMeshWorkGroupCount[0];
	maxPossibleInstances = meshShaderProp.maxMeshWorkGroupCount[1];
	vkb::DeviceBuilder deviceBuilder{ physicalDevice };
	auto deviceBuilderRet = deviceBuilder.build();
	if (!deviceBuilderRet) {
		std::cerr << "Unable to create logical device. Error: " << deviceBuilderRet.error().message() << std::endl;
		exit(EXIT_FAILURE);
	}
	device = deviceBuilderRet.value();
	transferQueue = device.get_dedicated_queue(vkb::QueueType::transfer).value();
	transferQueueIndex = device.get_dedicated_queue_index(vkb::QueueType::transfer).value();
	graphicsQueue = device.get_queue(vkb::QueueType::graphics).value(); //assume compute and present as well
	graphicsQueueIndex = device.get_queue_index(vkb::QueueType::graphics).value();
	vkCmdDrawMeshTasks = reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(vkGetDeviceProcAddr(device, "vkCmdDrawMeshTasksEXT"));
	assert(vkCmdDrawMeshTasks);

	if (device.queue_families.at(graphicsQueueIndex).timestampValidBits >= 64) timestampBitsValid = true;
	timestampPeriod = physicalDevice.properties.limits.timestampPeriod;
	VkQueryPoolCreateInfo queryPoolCreateInfo{ VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
	queryPoolCreateInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
	queryPoolCreateInfo.queryCount = 2*FRAME_QUEUE_LENGTH;
	VK_CHECK(vkCreateQueryPool(device, &queryPoolCreateInfo, nullptr, &queryPool));

	VmaAllocatorCreateInfo allocatorInfo{};
	allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
	allocatorInfo.instance = instance.instance;
	allocatorInfo.physicalDevice = physicalDevice.physical_device;
	allocatorInfo.device = device.device;
	VK_CHECK(vmaCreateAllocator(&allocatorInfo, &allocator));

	VkCommandPoolCreateInfo commandPoolCreateInfo{};
	commandPoolCreateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	commandPoolCreateInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	commandPoolCreateInfo.queueFamilyIndex = graphicsQueueIndex;
	graphicsCommandPool.create(device, commandPoolCreateInfo, VK_COMMAND_BUFFER_LEVEL_PRIMARY);
	commandPoolCreateInfo.queueFamilyIndex = transferQueueIndex;
	transferCommandPool.create(device, commandPoolCreateInfo, VK_COMMAND_BUFFER_LEVEL_PRIMARY);

	std::vector<VkDescriptorPoolSize> defaultPoolSizes = {
		{
			VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
			10
		},
		{
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, //storage buffer contains meshlets for each mesh
			1000
		}
	};
	defaultDescriptorAllocator.create(device, 0, 1000, defaultPoolSizes);

	std::vector<VkDescriptorPoolSize> bindlessPoolSizes = {
		{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, FRAME_QUEUE_LENGTH * MAX_TEXTURES }
	};
	bindlessDescriptorAllocator.create(device, VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT, 
		FRAME_QUEUE_LENGTH, bindlessPoolSizes);

	chooseDefaultDepthFormat();

	createSwapchain(window);

	createRenderPasses();

	createDefaultFramebuffer();

	createDescriptorLayouts();

	buildPipelines(window);

	VkSemaphoreCreateInfo semaphoreCreateInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
	VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &transferSemaphore));
	VkFenceCreateInfo fenceCreateInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
	VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &stagingBufferFence));

	initStaticDescriptorSets();

	uploadTestCube();

	fenceCreateInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &acquireSemaphores[i]));
		VK_CHECK(vkCreateSemaphore(device, &semaphoreCreateInfo, nullptr, &presentSemaphores[i]));
		VK_CHECK(vkCreateFence(device, &fenceCreateInfo, nullptr, &frameQueueFences[i]));
	}
}

void VulkanRenderer::render(GLFWwindow* window, Camera& camera, UIState& uiState, EntityManager& entityManager) {
	static uint32_t currentMeshInstanceEntity = 0;
	static std::unordered_map<uint8_t, DynamicBufferUploadJobArgs> dynamicUploadJobArgs;
	if (uiState.showUI) {
		ImGuiIO& io = ImGui::GetIO();
		uiState.uiHovered = io.WantCaptureMouse;
		ImGui_ImplVulkan_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();
		if (ImGui::Begin("Settings", &uiState.showUI, 0)) {
			if (!entityManager.isEmpty()) {
				if (!entityManager.isValidEntity(currentMeshInstanceEntity)) {
					auto it = entityManager.meshInstancesBegin();
					currentMeshInstanceEntity = it->first;
				}
				MeshInstanceComponent currentMeshInstance;
				entityManager.getMeshInstance(currentMeshInstanceEntity, currentMeshInstance);
				ImGui::AlignTextToFramePadding();
				ImGui::Text("Current entity: %d", currentMeshInstanceEntity);
				ImGui::SameLine();
				float spacing = ImGui::GetStyle().ItemInnerSpacing.x;
				ImGui::PushButtonRepeat(true);
				TransformComponent currentTransform;
				if (ImGui::ArrowButton("##left", ImGuiDir_Left)) {
					auto it = entityManager.getMeshInstanceIterator(currentMeshInstanceEntity);
					if (it != entityManager.meshInstancesBegin()) {
						it--;
						currentMeshInstanceEntity = it->first;
					}
				}
				ImGui::SameLine(0.0f, spacing);
				if (ImGui::ArrowButton("##right", ImGuiDir_Right)) {
					auto it = entityManager.getMeshInstanceIterator(currentMeshInstanceEntity);
					it++;
					if (it == entityManager.meshInstancesEnd()) it--;
					currentMeshInstanceEntity = it->first;
				}
				ImGui::PopButtonRepeat();
				entityManager.getTransform(currentMeshInstanceEntity, currentTransform);
				ImGui::DragFloat3(" Pos xyz", glm::value_ptr(currentTransform.position), 0.1f, -1000.0f, 1000.0f);
				bool transformEdited = false;
				if (ImGui::IsItemEdited()) transformEdited = true;
				ImGui::DragFloat3(" Scale xyz", glm::value_ptr(currentTransform.scale), 0.1f, 0.1f, 1000.0f);
				if (ImGui::IsItemEdited()) transformEdited = true;
				ImGui::DragFloat3(" Yaw, pitch, roll", glm::value_ptr(currentTransform.ypr), 0.01f, 0.0f, TWO_PI);
				if (ImGui::IsItemEdited()) transformEdited = true;
				uint32_t currentMeshIndex = currentMeshInstance.getMeshIndex();
				uint32_t currentModelIndex = currentMeshInstance.getModelIndex();
				Mesh* currentMesh = meshes.at(currentMeshIndex);
				if (transformEdited) {
					entityManager.setTransform(currentMeshInstanceEntity, currentTransform.position, currentTransform.scale, currentTransform.ypr);
					glm::mat4 model = createModelMatrix(currentTransform.position, currentTransform.scale, currentTransform.ypr);
					currentMesh->models.at(currentModelIndex) = model;
					for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
						DynamicBufferUploadJobArgs jobArgs{
							currentMesh->modelMatrixBuffers[i],
							currentMesh->modelMatrixBufferAllocations[i],
							currentMesh->modelMatrixBufferAllocationInfos[i],
							sizeof(glm::mat4) * currentMesh->models.size(),
							currentMesh->models.data()
						};
						dynamicUploadJobArgs.emplace(i, jobArgs);
					}
				}
				if (ImGui::Button("Duplicate current mesh") && dynamicUploadJobArgs.empty() && static_cast<uint32_t>(currentMesh->models.size()) < MAX_MODELS) {
					currentMesh->models.push_back(currentMesh->models.at(currentModelIndex));
					uint32_t entity = entityManager.createEntity();
					entityManager.setMeshInstance(entity, currentMeshIndex, static_cast<uint32_t>(currentMesh->models.size() - 1));
					entityManager.setTransform(entity, currentTransform.position, currentTransform.scale, currentTransform.ypr);
					currentMesh->entities.push_back(entity);
					for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
						DynamicBufferUploadJobArgs jobArgs{
							currentMesh->modelMatrixBuffers[i],
							currentMesh->modelMatrixBufferAllocations[i],
							currentMesh->modelMatrixBufferAllocationInfos[i],
							sizeof(glm::mat4) * currentMesh->models.size(),
							currentMesh->models.data()
						};
						dynamicUploadJobArgs.emplace(i, jobArgs);
					}
				}
				if (ImGui::Button("Delete current mesh") && dynamicUploadJobArgs.empty()) {
					entityManager.destroyEntity(currentMeshInstanceEntity);
					std::swap(currentMesh->models.at(currentModelIndex), currentMesh->models.back());
					std::swap(currentMesh->entities.at(currentModelIndex), currentMesh->entities.back());
					entityManager.setMeshInstance(currentMesh->entities.at(currentModelIndex), currentMeshIndex, currentModelIndex);
					currentMesh->models.pop_back();
					currentMesh->entities.pop_back();
					if (currentMesh->models.empty()) {
						meshes.erase(currentMeshIndex);
						deletionQueue.push_back(std::make_pair(0, currentMesh));
					}
					else {
						for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
							DynamicBufferUploadJobArgs jobArgs{
								currentMesh->modelMatrixBuffers[i],
								currentMesh->modelMatrixBufferAllocations[i],
								currentMesh->modelMatrixBufferAllocationInfos[i],
								sizeof(glm::mat4) * currentMesh->models.size(),
								currentMesh->models.data()
							};
							dynamicUploadJobArgs.emplace(i, jobArgs);
						}
					}
				}
				if (timestampBitsValid) {
					if (ImGui::Button("Measure average frame time") && !measureFrameTime) {
						measureFrameTime = true;
						frameTimeAvailable = false;
					}
				}
				if (frameTimeAvailable) {
					ImGui::SameLine();
					ImGui::Text("%.8f ms", static_cast<double>(averageFrameTime) * timestampPeriod * 1e-6);
				}
			}
			else {
				if (ImGui::Button("Upload cube") && !meshUploadCounter.isBusy()) {
					uploadTestCube();
				}
			}
			static char fileName[1000] = "";
			ImGui::InputText("Path to load mesh", fileName, IM_ARRAYSIZE(fileName));
			uiState.editingText = ImGui::IsItemActive();
			if (ImGui::Button("Upload mesh") && !meshUploadCounter.isBusy()) {
				loadMesh(fileName);
			}
		}
		
		ImGui::End();

		ImGui::Render();
	}

	vkWaitForFences(device, 1, &frameQueueFences[currentFrame], VK_TRUE, UINT64_MAX);
	VK_CHECK(vkResetFences(device, 1, &frameQueueFences[currentFrame]));
	uint32_t swapchainImageIndex = 0;
	VkResult swapchainResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSemaphores[currentFrame],
		VK_NULL_HANDLE, &swapchainImageIndex);
	while (swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || swapchainResult == VK_SUBOPTIMAL_KHR) {
		glfwGetFramebufferSize(window, &currentWidth, &currentHeight);
		while (currentWidth == 0 || currentHeight == 0) {
			glfwGetFramebufferSize(window, &currentWidth, &currentHeight);
			glfwWaitEvents();
		}
		createSwapchain(window);
		swapchainResult = vkAcquireNextImageKHR(device, swapchain, UINT64_MAX, acquireSemaphores[currentFrame],
			VK_NULL_HANDLE, &swapchainImageIndex);
	}
	VK_CHECK(swapchainResult);
	
	//once deletionCounter == FRAME_QUEUE_LENGTH, it's safe to free all the resources since it's no longer being used
	for (auto it = deletionQueue.begin(); it != deletionQueue.end();) {
		it->first++;
		if (it->first == FRAME_QUEUE_LENGTH) {
			it->second->destroy(allocator);
			delete it->second;
			it = deletionQueue.erase(it);
		}
		else {
			it++;
		}
	}

	uniformMatrices[currentFrame].view = camera.getView();
	float currentAspectRatio = (float)currentWidth / (float)currentHeight;
	uniformMatrices[currentFrame].projection = glm::perspective(CAMERA_FOV_Y, currentAspectRatio, Z_NEAR, Z_FAR);
	uniformMatrices[currentFrame].projection[1][1] *= -1;
	DynamicBufferUploadJobArgs jobArgs{
		transformsBuffers[currentFrame],
		transformsBufferAllocations[currentFrame],
		transformsBufferAllocationInfos[currentFrame],
		sizeof(UniformMatrices),
		&uniformMatrices[currentFrame]
	};
	updateDynamicBuffer(jobArgs);

	auto it = dynamicUploadJobArgs.find(currentFrame);
	if (it != dynamicUploadJobArgs.end()) {
		updateDynamicBuffer(it->second);
		dynamicUploadJobArgs.erase(it);
	}

	std::vector<VkCommandBuffer>& commandBuffers = graphicsCommandPool.getCommandBuffers(FRAME_QUEUE_LENGTH);
	VkCommandBuffer commandBuffer = commandBuffers.at(currentFrame);
	VkCommandBufferBeginInfo commandBufferBeginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	commandBufferBeginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &commandBufferBeginInfo));
	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	static std::vector<VkPipelineStageFlags> waitStages;
	static std::vector<VkSemaphore> waitSemaphores;
	static std::vector<VkBufferMemoryBarrier> bufferMemoryBarriers;
	waitStages.clear();
	waitSemaphores.clear();
	bufferMemoryBarriers.clear();
	waitSemaphores.push_back(acquireSemaphores[currentFrame]);
	waitStages.push_back(VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

	bool pushedTransferSemaphore = false;
	if (uploadedMeshes) {
		if (!meshUploadCounter.isBusy()) {
			uploadedMeshes = false;
			uint32_t numSuccessfulUploads = 0;
			for (Mesh* mesh : pendingMeshes) {
				if (mesh->vertexBuffer == VK_NULL_HANDLE) { //upload failed
					delete mesh;
					continue;
				}
				VkBufferMemoryBarrier bufferMemoryBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
				bufferMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
				bufferMemoryBarrier.srcQueueFamilyIndex = transferQueueIndex;
				bufferMemoryBarrier.dstQueueFamilyIndex = graphicsQueueIndex;
				bufferMemoryBarrier.buffer = mesh->vertexBuffer;
				bufferMemoryBarrier.size = VK_WHOLE_SIZE;
				bufferMemoryBarriers.push_back(bufferMemoryBarrier);
				bufferMemoryBarrier.buffer = mesh->meshletBuffer;
				bufferMemoryBarriers.push_back(bufferMemoryBarrier);
				mesh->models.push_back(glm::mat4{ 1.0f });
				for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
					VkBuffer modelMatricesBuffer = VK_NULL_HANDLE;
					VmaAllocation modelMatricesBufferAllocation = nullptr;
					VmaAllocationInfo modelMatricesBufferAllocationInfo{};
					VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
					bufCreateInfo.size = MAX_MODELS * sizeof(glm::mat4);
					bufCreateInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
					bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
					VmaAllocationCreateInfo allocCreateInfo{};
					allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
					allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
						VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
						VMA_ALLOCATION_CREATE_MAPPED_BIT;
					VK_CHECK(vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &modelMatricesBuffer,
						&modelMatricesBufferAllocation, &modelMatricesBufferAllocationInfo));
					mesh->modelMatrixBuffers[i] = modelMatricesBuffer;
					mesh->modelMatrixBufferAllocations[i] = modelMatricesBufferAllocation;
					mesh->modelMatrixBufferAllocationInfos[i] = modelMatricesBufferAllocationInfo;

					defaultDescriptorAllocator.allocateSet(meshLayout, nullptr, mesh->descriptorSet[i]);
					VkWriteDescriptorSet descriptorWrite[3];
					VkDescriptorBufferInfo descriptorBufInfo[3];
					descriptorWrite[0] = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					descriptorWrite[0].dstSet = mesh->descriptorSet[i];
					descriptorWrite[0].dstBinding = 0;
					descriptorWrite[0].dstArrayElement = 0;
					descriptorWrite[0].descriptorCount = 1;
					descriptorWrite[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					descriptorBufInfo[0] = {
						modelMatricesBuffer,
						0,
						VK_WHOLE_SIZE
					};
					descriptorWrite[0].pBufferInfo = &descriptorBufInfo[0];

					descriptorWrite[1] = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					descriptorWrite[1].dstSet = mesh->descriptorSet[i];
					descriptorWrite[1].dstBinding = 1;
					descriptorWrite[1].dstArrayElement = 0;
					descriptorWrite[1].descriptorCount = 1;
					descriptorWrite[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					descriptorBufInfo[1] = {
						mesh->vertexBuffer,
						0,
						VK_WHOLE_SIZE
					};
					descriptorWrite[1].pBufferInfo = &descriptorBufInfo[1];

					descriptorWrite[2] = VkWriteDescriptorSet{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
					descriptorWrite[2].dstSet = mesh->descriptorSet[i];
					descriptorWrite[2].dstBinding = 2;
					descriptorWrite[2].dstArrayElement = 0;
					descriptorWrite[2].descriptorCount = 1;
					descriptorWrite[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
					descriptorBufInfo[2] = {
						mesh->meshletBuffer,
						0,
						VK_WHOLE_SIZE
					};
					descriptorWrite[2].pBufferInfo = &descriptorBufInfo[2];
					vkUpdateDescriptorSets(device, 3, descriptorWrite, 0, nullptr);
					DynamicBufferUploadJobArgs jobArgs{
						mesh->modelMatrixBuffers[i],
						mesh->modelMatrixBufferAllocations[i],
						mesh->modelMatrixBufferAllocationInfos[i],
						sizeof(glm::mat4)*mesh->models.size(),
						mesh->models.data()
					};
					updateDynamicBuffer(jobArgs);
				}
				meshes.emplace(numMeshesCreated, mesh);
				uint32_t entity = entityManager.createEntity();
				mesh->entities.push_back(entity);
				entityManager.setMeshInstance(entity, numMeshesCreated, 0);
				entityManager.setTransform(entity, glm::vec3{ 0.0f }, glm::vec3{ 1.0f }, glm::vec3{ 0.0f });
				numMeshesCreated++;
				numSuccessfulUploads++;
			}
			if (numSuccessfulUploads > 0) {
				waitSemaphores.push_back(transferSemaphore);
				waitStages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
				pushedTransferSemaphore = true;
			}
			pendingMeshes.clear();
		}
	}
	if (uploadedDynamicBuffers) {
		dynamicBufferUploadCounter.wait();
		if (!pushedTransferSemaphore) {
			waitSemaphores.push_back(transferSemaphore);
			waitStages.push_back(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT);
		}
		bufferMemoryBarriers.insert(bufferMemoryBarriers.end(), dynamicBufferBarriers.begin(), dynamicBufferBarriers.end());
		dynamicBufferBarriers.clear();
		uploadedDynamicBuffers = false;
	}
	if (bufferMemoryBarriers.empty()) {
	}
	else {
		vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_MESH_SHADER_BIT_EXT,
			0, 0, nullptr, static_cast<uint32_t>(bufferMemoryBarriers.size()), bufferMemoryBarriers.data(), 0, nullptr);
	}
		
	submitInfo.waitSemaphoreCount = static_cast<uint32_t>(waitSemaphores.size());
	submitInfo.pWaitSemaphores = waitSemaphores.data();
	submitInfo.pWaitDstStageMask = waitStages.data();

	VkRenderPassBeginInfo renderPassBeginInfo{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
	renderPassBeginInfo.renderPass = renderPasses[RenderPass_Default];
	renderPassBeginInfo.renderArea.extent = swapchain.extent;
	renderPassBeginInfo.framebuffer = framebuffer[Framebuffer_Default].at(swapchainImageIndex);
	renderPassBeginInfo.clearValueCount = 2;
	VkClearValue clearValues[2]{};
	memcpy(clearValues[0].color.float32, clearColor, sizeof(clearColor));
	clearValues[1].depthStencil.depth = 1.0f;
	renderPassBeginInfo.pClearValues = clearValues;

	if (measureFrameTime && !frameInMeasurement[currentFrame]) {
		vkCmdResetQueryPool(commandBuffer, queryPool, 2*currentFrame, 2);
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queryPool, 2 * currentFrame);
	}

	vkCmdBeginRenderPass(commandBuffer, &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);
	vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelines[Graphics_Default]);
	VkViewport viewport{ 0.0f, 0.0f, static_cast<float>(swapchain.extent.width) ,
	static_cast<float>(swapchain.extent.height) , 0.0f, 1.0f };
	vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
	VkRect2D scissor{};
	scissor.extent = swapchain.extent;
	vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts[Graphics_Default],
		0, 1, &transformsDescriptorSets[currentFrame], 0, nullptr);
	vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts[Graphics_Default],
		1, 1, &materialsDescriptorSets[currentFrame], 0, nullptr);
	for (auto& pair : meshes) {
		Mesh* mesh = pair.second;
		vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayouts[Graphics_Default],
			2, 1, &(mesh->descriptorSet[currentFrame]), 0, nullptr);
		vkCmdDrawMeshTasks(commandBuffer, mesh->numMeshlets, static_cast<uint32_t>(mesh->models.size()), 1);
	}
	
	if (uiState.showUI) {
		ImDrawData* drawData = ImGui::GetDrawData();
		ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
	}

	vkCmdEndRenderPass(commandBuffer);

	if (measureFrameTime && !frameInMeasurement[currentFrame]) {
		vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queryPool, 2 * currentFrame + 1);
		frameInMeasurement[currentFrame] = true;
	}

	VK_CHECK(vkEndCommandBuffer(commandBuffer));

	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &presentSemaphores[currentFrame];
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	vkQueueSubmit(graphicsQueue, 1, &submitInfo, frameQueueFences[currentFrame]);

	if (measureFrameTime) {
		uint64_t frameTime = 0;
		frameTimeAvailable = getFrameTime(currentFrame, frameTime);
		if (frameTimeAvailable) {
			frameInMeasurement[currentFrame] = false;
			sumFrameTime += frameTime;
			numFrameTimeSampled++;
			if (numFrameTimeSampled == NUM_FRAME_TIME_SAMPLES) {
				measureFrameTime = false;
				numFrameTimeSampled = 0;
				averageFrameTime = static_cast<double>(sumFrameTime) / NUM_FRAME_TIME_SAMPLES;
				sumFrameTime = 0;
			}
		}
	}

	VkPresentInfoKHR presentInfo{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
	presentInfo.pImageIndices = &swapchainImageIndex;
	presentInfo.swapchainCount = 1;
	presentInfo.pSwapchains = &swapchain.swapchain;
	presentInfo.waitSemaphoreCount = 1;
	presentInfo.pWaitSemaphores = &presentSemaphores[currentFrame];
	swapchainResult = vkQueuePresentKHR(graphicsQueue, &presentInfo);
	if (swapchainResult == VK_ERROR_OUT_OF_DATE_KHR || swapchainResult == VK_SUBOPTIMAL_KHR) {
		glfwGetFramebufferSize(window, &currentWidth, &currentHeight);
		while (currentWidth == 0 || currentHeight == 0) {
			glfwGetFramebufferSize(window, &currentWidth, &currentHeight);
			glfwWaitEvents();
		}
		createSwapchain(window);
	}
	currentFrame = (currentFrame + 1) % FRAME_QUEUE_LENGTH;
	numFramesProcessed++;
}

struct LoadMeshJobArgs {
	std::string path;
	Mesh* mesh = nullptr;
	VulkanRenderer* renderer = nullptr;
};

void loadMeshJob(void* jobArgs) {
	assert(jobArgs);
	LoadMeshJobArgs* args = reinterpret_cast<LoadMeshJobArgs*>(jobArgs);
	Mesh* mesh = args->mesh;
	assert(mesh);
	VulkanRenderer* renderer = args->renderer;
	assert(renderer);
	std::string directory;
	size_t pos = args->path.find_last_of('/');
	if (pos != std::string::npos)
		directory = args->path.substr(0, pos); //for searching textures
	tinyobj::ObjReaderConfig readerConfig;
	readerConfig.vertex_color = false;
	readerConfig.triangulate = true;
	tinyobj::ObjReader reader;
	if (!reader.ParseFromFile(args->path, readerConfig)) {
		if (!reader.Error().empty()) {
			std::cout << "TinyObjReader: " << reader.Error();
			std::cout.flush();
		}
		delete args;
		return;
	}

	if (!reader.Warning().empty()) {
		std::cout << "TinyObjReader: " << reader.Warning();
	}

	std::cout << "Loading mesh at " << args->path << std::endl;

	const tinyobj::attrib_t& attrib = reader.GetAttrib();
	const std::vector<tinyobj::shape_t>& shapes = reader.GetShapes();
	const std::vector<tinyobj::material_t>& materials = reader.GetMaterials();

	std::vector<Vertex> unindexedVertices;
	std::vector<uint32_t> remap;
	std::vector<Vertex> vertices;
	std::vector<uint32_t> indices;
	std::vector<Meshlet> meshlets;

	uint32_t numIndices = 0;
	for (const tinyobj::shape_t& shape : shapes) {
		const tinyobj::mesh_t& mesh = shape.mesh;
		size_t indexOffset = 0;
		for (int f = 0; f < mesh.num_face_vertices.size(); f++) {
			Vertex faceVertices[3];
			bool useFaceNormal = false;
			for (int v = 0; v < 3; v++) {
				tinyobj::index_t idx = mesh.indices[indexOffset + v];
				Vertex vertex;
				vertex.pos.x = attrib.vertices[3 * size_t(idx.vertex_index)];
				vertex.pos.y = attrib.vertices[3 * size_t(idx.vertex_index) + 1];
				vertex.pos.z = attrib.vertices[3 * size_t(idx.vertex_index) + 2];
				vertex.pos.w = 1.0f;

				// Check if `normal_index` is zero or positive. negative = no normal data
				if (idx.normal_index >= 0) {
					vertex.normal.x = attrib.normals[3 * size_t(idx.normal_index)];
					vertex.normal.y = attrib.normals[3 * size_t(idx.normal_index) + 1];
					vertex.normal.z = attrib.normals[3 * size_t(idx.normal_index) + 2];
					vertex.normal.w = 0.0f;
				}
				else useFaceNormal = true;

				// Check if `texcoord_index` is zero or positive. negative = no texcoord data
				if (idx.texcoord_index >= 0) {
					vertex.texCoord.x = attrib.texcoords[2 * size_t(idx.texcoord_index)];
					vertex.texCoord.y = attrib.texcoords[2 * size_t(idx.texcoord_index) + 1];
				}

				faceVertices[v] = vertex;
			}
			if (useFaceNormal) {
				glm::vec3 v1 = faceVertices[1].pos - faceVertices[0].pos;
				glm::vec3 v2 = faceVertices[2].pos - faceVertices[0].pos;
				glm::vec3 faceNormal = glm::cross(v1, v2);
				faceNormal = glm::normalize(faceNormal);
				faceVertices[0].normal = glm::vec4(faceNormal, 0.0f);
				faceVertices[1].normal = glm::vec4(faceNormal, 0.0f);
				faceVertices[2].normal = glm::vec4(faceNormal, 0.0f);
			}
			unindexedVertices.push_back(faceVertices[0]);
			unindexedVertices.push_back(faceVertices[1]);
			unindexedVertices.push_back(faceVertices[2]);
			indexOffset += 3;
		}
	}

	size_t indexCount = unindexedVertices.size();
	remap.resize(indexCount);
	size_t vertexCount = meshopt_generateVertexRemap(remap.data(), nullptr, indexCount, 
		unindexedVertices.data(), indexCount, sizeof(Vertex));
	indices.resize(indexCount);
	vertices.resize(vertexCount);
	meshopt_remapIndexBuffer(indices.data(), nullptr, indexCount, remap.data());
	meshopt_remapVertexBuffer(vertices.data(), unindexedVertices.data(), indexCount, sizeof(Vertex), remap.data());
	
	size_t maxMeshlets = static_cast<size_t>(renderer->maxPossibleMeshlets);
	meshopt_Meshlet* meshOptMeshlets = new meshopt_Meshlet[maxMeshlets];
	uint32_t* meshletVertices = new uint32_t[MESHLET_MAX_VERTICES * maxMeshlets];
	unsigned char* meshletTriangles = new unsigned char[MESHLET_MAX_INDICES * maxMeshlets];
	uint32_t numMeshletsGenerated = meshopt_buildMeshlets(meshOptMeshlets, meshletVertices, meshletTriangles,
		indices.data(), static_cast<uint32_t>(indexCount), &vertices[0].pos.x, 
		static_cast<uint32_t>(vertexCount), sizeof(Vertex), MESHLET_MAX_VERTICES, MESHLET_MAX_INDICES / 3, 0.0f);
	for (uint32_t i = 0; i < numMeshletsGenerated && i < maxMeshlets; i++) {
		meshopt_Meshlet& meshOptMeshlet = meshOptMeshlets[i];
		Meshlet meshlet;
		meshlet.vertexCount = meshOptMeshlet.vertex_count;
		meshlet.indexCount = meshOptMeshlet.triangle_count*3;
		for (uint32_t j = 0; j < meshlet.vertexCount; j++) {
			meshlet.vertices[j] = meshletVertices[j + meshOptMeshlet.vertex_offset];
		}
		for (uint32_t j = 0; j < meshOptMeshlet.triangle_count; j++) {
			meshlet.indices[3*j] = meshletTriangles[meshOptMeshlet.triangle_offset + 3*j];
			meshlet.indices[3 * j + 1] = meshletTriangles[meshOptMeshlet.triangle_offset + 3*j + 1];
			meshlet.indices[3 * j + 2] = meshletTriangles[meshOptMeshlet.triangle_offset + 3*j + 2];
		}
		meshlets.push_back(meshlet);
	}
	delete[] meshOptMeshlets;
	delete[] meshletVertices;
	delete[] meshletTriangles;
	mesh->numMeshlets = numMeshletsGenerated;

	VkBuffer vertexBuffer = VK_NULL_HANDLE, meshletBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = nullptr, meshletBufferAllocation = nullptr;
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.size = vertices.size() * sizeof(Vertex);
	VmaAllocationCreateInfo bufferAllocInfo{};
	bufferAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	VK_CHECK(vmaCreateBuffer(renderer->allocator, &bufferCreateInfo, &bufferAllocInfo, &vertexBuffer, &vertexBufferAllocation, nullptr));
	mesh->vertexBuffer = vertexBuffer;
	mesh->vertexBufferAllocation = vertexBufferAllocation;
	bufferCreateInfo.size = sizeof(Meshlet) * meshlets.size();
	VK_CHECK(vmaCreateBuffer(renderer->allocator, &bufferCreateInfo, &bufferAllocInfo, &meshletBuffer, &meshletBufferAllocation, nullptr));
	mesh->meshletBuffer = meshletBuffer;
	mesh->meshletBufferAllocation = meshletBufferAllocation;

	renderer->stageBuffer(vertices.data(), vertices.size()*sizeof(Vertex), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
		mesh->vertexBuffer, mesh->vertexBufferAllocation);
	renderer->stageBuffer(meshlets.data(), sizeof(Meshlet) * meshlets.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		mesh->meshletBuffer, mesh->meshletBufferAllocation);
	renderer->uploadBuffers();
	renderer->finalizeBufferUpload();

	delete args;

	std::cout << "Finished loading mesh" << std::endl;
}

void VulkanRenderer::loadMesh(std::string path) {
	std::string newPath(path);
	std::replace(newPath.begin(), newPath.end(), '\\', '/');

	Mesh* mesh = new Mesh;
	pendingMeshes.push_back(mesh);

	Job job;
	LoadMeshJobArgs* jobArgs = new LoadMeshJobArgs{ newPath, mesh, this};
	job.func = loadMeshJob;
	job.jobArgs = jobArgs;
	job.counter = &meshUploadCounter;
	uploadedMeshes = true;
	ioThread.jobQueue.pushJob(job);
}

void VulkanRenderer::shutDown() {
	size_t dstPipelineCacheSize = 0;
	vkGetPipelineCacheData(device, dstPipelineCache, &dstPipelineCacheSize, nullptr);
	std::vector<char> pipelineCacheData(dstPipelineCacheSize);
	VK_CHECK(vkGetPipelineCacheData(device, dstPipelineCache, &dstPipelineCacheSize, pipelineCacheData.data()));
	std::ofstream file("pipeline_cache.bin", std::ios::binary | std::ios::trunc);
	if (file.is_open()) {
		file.write(pipelineCacheData.data(), pipelineCacheData.size());
		file.close();
	}
	else {
		std::cerr << "Could not write to file 'pipeline_cache.bin'\n";
	}
	vkDestroyPipelineCache(device, dstPipelineCache, nullptr);
	for (int i = 0; i < numPipelines; i++) {
		vkDestroyPipeline(device, pipelines[i], nullptr);
		vkDestroyPipelineLayout(device, pipelineLayouts[i], nullptr);
	}
	destroyImgui();
	vkDestroyFence(device, stagingBufferFence, nullptr);
	for (Mesh* mesh : pendingMeshes) { //should be empty
		mesh->destroy(allocator);
		delete mesh;
	}
	for (auto& pair : deletionQueue) { //may or may not be empty
		pair.second->destroy(allocator);
		delete pair.second;
	}
	for (auto& pair : meshes) {
		pair.second->destroy(allocator);
		delete pair.second;
	}
	vkDestroySemaphore(device, transferSemaphore, nullptr);
	for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
		vmaDestroyBuffer(allocator, transformsBuffers[i], transformsBufferAllocations[i]);
		vkDestroySemaphore(device, acquireSemaphores[i], nullptr);
		vkDestroySemaphore(device, presentSemaphores[i], nullptr);
		vkDestroyFence(device, frameQueueFences[i], nullptr);
	}
	vkDestroyDescriptorSetLayout(device, meshLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, transformsLayout, nullptr);
	vkDestroyDescriptorSetLayout(device, materialsLayout, nullptr);
	graphicsCommandPool.destroy();
	transferCommandPool.destroy();
	defaultDescriptorAllocator.destroy();
	bindlessDescriptorAllocator.destroy();
	for (int i = 0; i < numRenderPasses; i++) {
		vkDestroyRenderPass(device, renderPasses[i], nullptr);
	}
	vkDestroyQueryPool(device, queryPool, nullptr);
	destroySwapchain();
	vmaDestroyAllocator(allocator);
	vkb::destroy_device(device);
	vkDestroySurfaceKHR(instance, surface, nullptr);
	vkb::destroy_instance(instance);
}

bool VulkanRenderer::getFrameTime(uint8_t currentFrame, uint64_t& time) {
	uint64_t buffer[2];
	VkResult result = vkGetQueryPoolResults(device, queryPool, 2*currentFrame, 2, 2*sizeof(uint64_t),
		buffer, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
	if (result == VK_SUCCESS) {
		time = buffer[1] - buffer[0];
		return true;
	}
	else return false;
}

void VulkanRenderer::chooseDefaultDepthFormat() {
	VkFormatProperties formatProperties{};
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D24_UNORM_S8_UINT, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		D24_UNORM_S8_UINT_Supported = false;
		defaultDepthFormat = VK_FORMAT_D32_SFLOAT_S8_UINT;
	}
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT_S8_UINT, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		D32_SFLOAT_S8_UINT_Supported = false;
		if (defaultDepthFormat == VK_FORMAT_D32_SFLOAT_S8_UINT) {
			defaultDepthFormat = VK_FORMAT_D32_SFLOAT;
			defaultDepthIsStencilFormat = false;
		}
	}
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D32_SFLOAT, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		D32_SFLOAT_Supported = false;
		if (defaultDepthFormat == VK_FORMAT_D32_SFLOAT) {
			defaultDepthFormat = VK_FORMAT_D16_UNORM_S8_UINT;
			defaultDepthIsStencilFormat = true;
		}
	}
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D16_UNORM_S8_UINT, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		D16_UNORM_S8_UINT_Supported = false;
		if (defaultDepthFormat == VK_FORMAT_D16_UNORM_S8_UINT) {
			defaultDepthFormat = VK_FORMAT_D16_UNORM;
			defaultDepthIsStencilFormat = false;
		}
	}
	vkGetPhysicalDeviceFormatProperties(physicalDevice, VK_FORMAT_D16_UNORM, &formatProperties);
	if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) == 0) {
		D16_UNORM_Supported = false;
		if (defaultDepthFormat == VK_FORMAT_D16_UNORM) {
			std::cerr << "Unable to find compatible depth format" << std::endl;
			exit(EXIT_FAILURE);
		}
	}
}

void VulkanRenderer::createSwapchainDepthImages() {
	swapchainDepthImages.resize(swapchain.image_count, VK_NULL_HANDLE);
	swapchainDepthAllocs.resize(swapchain.image_count, nullptr);
	swapchainDepthImageViews.resize(swapchain.image_count, VK_NULL_HANDLE);
	VkImageCreateInfo depthImageInfo{};
	depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
	depthImageInfo.format = defaultDepthFormat;
	VkExtent3D swapchainExtent{};
	swapchainExtent.width = swapchain.extent.width;
	swapchainExtent.height = swapchain.extent.height;
	swapchainExtent.depth = 1;
	depthImageInfo.extent = swapchainExtent;
	depthImageInfo.mipLevels = 1;
	depthImageInfo.arrayLayers = 1;
	depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
	depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
	depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
	depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	VmaAllocationCreateInfo depthAllocInfo{};
	depthAllocInfo.flags = VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
	depthAllocInfo.usage = VMA_MEMORY_USAGE_AUTO;
	for (int i = 0; i < swapchainDepthImages.size(); i++) {
		VK_CHECK(vmaCreateImage(allocator, &depthImageInfo, &depthAllocInfo, &swapchainDepthImages[i], &swapchainDepthAllocs[i], nullptr));
	}
	for (unsigned int i = 0; i < swapchainDepthImageViews.size(); i++) {
		VkImageViewCreateInfo depthViewInfo{};
		depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		depthViewInfo.image = swapchainDepthImages[i];
		depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		depthViewInfo.format = defaultDepthFormat;
		VkImageSubresourceRange subresourceRange{};
		subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
		subresourceRange.baseMipLevel = 0;
		subresourceRange.levelCount = 1;
		subresourceRange.baseArrayLayer = 0;
		subresourceRange.layerCount = 1;
		depthViewInfo.subresourceRange = subresourceRange;
		VK_CHECK(vkCreateImageView(device, &depthViewInfo, nullptr, &swapchainDepthImageViews[i]));
	}
}

void VulkanRenderer::createDefaultFramebuffer() {
	framebuffer[Framebuffer_Default].resize(swapchain.image_count, VK_NULL_HANDLE);
	VkFramebufferCreateInfo defaultFramebufferCreateInfo{};
	defaultFramebufferCreateInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
	defaultFramebufferCreateInfo.renderPass = renderPasses[RenderPass_Default];
	defaultFramebufferCreateInfo.width = swapchain.extent.width;
	defaultFramebufferCreateInfo.height = swapchain.extent.height;
	defaultFramebufferCreateInfo.layers = 1;
	defaultFramebufferCreateInfo.attachmentCount = 2;
	for (unsigned int i = 0; i < swapchain.image_count; i++) {
		VkImageView defaultAttachments[2] = { swapchainImageViews[i], swapchainDepthImageViews[i] };
		defaultFramebufferCreateInfo.pAttachments = defaultAttachments;
		VK_CHECK(vkCreateFramebuffer(device, &defaultFramebufferCreateInfo, nullptr, &framebuffer[Framebuffer_Default][i]));
	}
}

void VulkanRenderer::createRenderPasses() {
	VkRenderPassCreateInfo defaultRenderPassInfo{};
	defaultRenderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	VkAttachmentDescription defaultAttachmentDescriptions[2];
	defaultAttachmentDescriptions[0].flags = 0;
	defaultAttachmentDescriptions[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	defaultAttachmentDescriptions[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	defaultAttachmentDescriptions[0].format = swapchain.image_format;
	defaultAttachmentDescriptions[0].samples = VK_SAMPLE_COUNT_1_BIT;
	defaultAttachmentDescriptions[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	defaultAttachmentDescriptions[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	defaultAttachmentDescriptions[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	defaultAttachmentDescriptions[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	defaultAttachmentDescriptions[1].flags = 0;
	defaultAttachmentDescriptions[1].format = defaultDepthFormat;
	defaultAttachmentDescriptions[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	defaultAttachmentDescriptions[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	defaultAttachmentDescriptions[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	defaultAttachmentDescriptions[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	defaultAttachmentDescriptions[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	defaultAttachmentDescriptions[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	defaultAttachmentDescriptions[1].samples = VK_SAMPLE_COUNT_1_BIT;
	defaultRenderPassInfo.attachmentCount = 2;
	defaultRenderPassInfo.pAttachments = defaultAttachmentDescriptions;
	VkSubpassDescription defaultSubpassDescription{};
	defaultSubpassDescription.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	VkAttachmentReference defaultColorAttachmentReference{};
	defaultColorAttachmentReference.attachment = 0;
	defaultColorAttachmentReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkAttachmentReference defaultDepthAttachmentReference{};
	defaultDepthAttachmentReference.attachment = 1;
	defaultDepthAttachmentReference.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	defaultSubpassDescription.colorAttachmentCount = 1;
	defaultSubpassDescription.pColorAttachments = &defaultColorAttachmentReference;
	defaultSubpassDescription.pDepthStencilAttachment = &defaultDepthAttachmentReference;
	defaultRenderPassInfo.subpassCount = 1;
	defaultRenderPassInfo.pSubpasses = &defaultSubpassDescription;
	VkSubpassDependency defaultSubpassDependency{};
	defaultSubpassDependency.dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT;
	defaultSubpassDependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	defaultSubpassDependency.dstSubpass = 0;
	defaultSubpassDependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	defaultSubpassDependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	defaultSubpassDependency.srcAccessMask = 0;
	defaultSubpassDependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	defaultRenderPassInfo.dependencyCount = 1;
	defaultRenderPassInfo.pDependencies = &defaultSubpassDependency;
	VK_CHECK(vkCreateRenderPass(device, &defaultRenderPassInfo, nullptr, &renderPasses[RenderPass_Default]));
}

void VulkanRenderer::createDescriptorLayouts() {
	VkDescriptorSetLayoutCreateInfo transformsDescriptorLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayoutBinding transformsBinding{};
	transformsBinding.binding = 0;
	transformsBinding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	transformsBinding.stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	transformsBinding.descriptorCount = 1;
	transformsDescriptorLayoutInfo.bindingCount = 1;
	transformsDescriptorLayoutInfo.pBindings = &transformsBinding;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &transformsDescriptorLayoutInfo, nullptr, &transformsLayout));
	VkDescriptorSetLayoutCreateInfo meshDescriptorLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayoutBinding meshBindings[3];
	meshBindings[0].binding = 0;
	meshBindings[0].descriptorCount = 1;
	meshBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	meshBindings[0].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	meshBindings[0].pImmutableSamplers = nullptr;
	meshBindings[1].binding = 1;
	meshBindings[1].descriptorCount = 1;
	meshBindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	meshBindings[1].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	meshBindings[1].pImmutableSamplers = nullptr;
	meshBindings[2].binding = 2;
	meshBindings[2].descriptorCount = 1;
	meshBindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
	meshBindings[2].stageFlags = VK_SHADER_STAGE_MESH_BIT_EXT;
	meshBindings[2].pImmutableSamplers = nullptr;
	meshDescriptorLayoutInfo.bindingCount = 3;
	meshDescriptorLayoutInfo.pBindings = meshBindings;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &meshDescriptorLayoutInfo, nullptr, &meshLayout));
	VkDescriptorSetLayoutCreateInfo materialDescriptorLayoutInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayoutBinding materialBindings[1];
	materialBindings[0].binding = 0;
	materialBindings[0].descriptorCount = MAX_TEXTURES;
	materialBindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
	materialBindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
	materialBindings[0].pImmutableSamplers = nullptr;
	materialDescriptorLayoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
	materialDescriptorLayoutInfo.bindingCount = 1;
	materialDescriptorLayoutInfo.pBindings = materialBindings;
	VkDescriptorSetLayoutBindingFlagsCreateInfo 
		bindingFlagsInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO };
	VkDescriptorBindingFlags bindingFlags[1];
	bindingFlags[0] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT
		| VK_DESCRIPTOR_BINDING_VARIABLE_DESCRIPTOR_COUNT_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
	bindingFlagsInfo.bindingCount = 1;
	bindingFlagsInfo.pBindingFlags = bindingFlags;
	materialDescriptorLayoutInfo.pNext = &bindingFlagsInfo;
	VK_CHECK(vkCreateDescriptorSetLayout(device, &materialDescriptorLayoutInfo, nullptr, &materialsLayout));
	VkPipelineLayoutCreateInfo defaultPipelineLayoutInfo{ VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
	VkDescriptorSetLayout setLayouts[3] = { transformsLayout, materialsLayout, meshLayout };
	defaultPipelineLayoutInfo.setLayoutCount = 3;
	defaultPipelineLayoutInfo.pSetLayouts = setLayouts;
	VK_CHECK(vkCreatePipelineLayout(device, &defaultPipelineLayoutInfo, nullptr, &pipelineLayouts[Graphics_Default]));
}

void VulkanRenderer::createSwapchain(GLFWwindow* window) {
	vkb::SwapchainBuilder swapchainBuilder{ physicalDevice, device, surface, graphicsQueueIndex, graphicsQueueIndex };
	swapchainBuilder.use_default_format_selection();
	swapchainBuilder.use_default_present_mode_selection();
	swapchainBuilder.use_default_image_usage_flags();
	swapchainBuilder.set_clipped(true);
	swapchainBuilder.set_desired_min_image_count(FRAME_QUEUE_LENGTH);
	if (swapchain.swapchain != VK_NULL_HANDLE) swapchainBuilder.set_old_swapchain(swapchain);
	int desiredSwapchainWidth = INIT_SCR_WIDTH, desiredSwapchainHeight = INIT_SCR_HEIGHT;
	glfwGetFramebufferSize(window, &desiredSwapchainWidth, &desiredSwapchainHeight);
	swapchainBuilder.set_desired_extent(desiredSwapchainWidth, desiredSwapchainHeight);
	auto swapchainBuilderRet = swapchainBuilder.build();
	if (!swapchainBuilderRet) {
		std::cerr << "Unable to create swapchain. Error: " << swapchainBuilderRet.error().message() << std::endl;
		exit(EXIT_FAILURE);
	}
	if (swapchain.swapchain != VK_NULL_HANDLE) { //swapchain recreation
		vkDeviceWaitIdle(device);
		destroySwapchain();
		swapchain = swapchainBuilderRet.value();
		swapchainImageViews = swapchain.get_image_views().value();
		createSwapchainDepthImages();
		createDefaultFramebuffer();
	}
	else {
		swapchain = swapchainBuilderRet.value();
		swapchainImageViews = swapchain.get_image_views().value();
		createSwapchainDepthImages();
	}
}

void VulkanRenderer::destroySwapchain() {
	for (unsigned int i = 0; i < numFramebuffers; i++) {
		for (unsigned int j = 0; j < swapchain.image_count; j++) {
			vkDestroyFramebuffer(device, framebuffer[i][j], nullptr);
		}
	}
	for (int i = 0; i < swapchainDepthImageViews.size(); i++) {
		vkDestroyImageView(device, swapchainDepthImageViews.at(i), nullptr);
		vmaDestroyImage(allocator, swapchainDepthImages.at(i), swapchainDepthAllocs.at(i));
	}
	swapchain.destroy_image_views(swapchainImageViews);
	vkb::destroy_swapchain(swapchain);
}

struct BuildPipelineArgs {
	void* initialData = nullptr;
	size_t initialDataSize = 0;
	VkPipelineCache* pipelineCache = nullptr;
};

void VulkanRenderer::buildPipelines(GLFWwindow* window) {
	std::ifstream file("pipeline_cache.bin", std::ios::ate | std::ios::binary);
	size_t initialDataSize = 0;
	std::vector<char> buffer;
	void* initialData = nullptr;
	if (file.is_open()) {
		size_t initialDataSize = static_cast<size_t>(file.tellg());
		buffer.resize(initialDataSize);
		file.seekg(0);
		file.read(buffer.data(), initialDataSize);
		file.close();
		initialData = buffer.data();
	}
	JobCounter pipelineJobCounter;
	Job buildDefaultPipelineJob{};
	buildDefaultPipelineJob.counter = &pipelineJobCounter;
	BuildPipelineArgs defaultPipelineArgs{ initialData, initialDataSize, &pipelineCaches[Graphics_Default] };
	buildDefaultPipelineJob.jobArgs = &defaultPipelineArgs;
	buildDefaultPipelineJob.func = [this](void* jobArgs) {
		assert(jobArgs);
		BuildPipelineArgs* args = reinterpret_cast<BuildPipelineArgs*>(jobArgs);
		buildDefaultPipeline(args->initialData, args->initialDataSize, *(args->pipelineCache));
	};
	jobManager.jobQueue.pushJob(buildDefaultPipelineJob);
	initImgui(window, initialData, initialDataSize, pipelineCaches[numPipelines]);
	VkPipelineCacheCreateInfo pipelineCacheInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	pipelineCacheInfo.flags = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT;
	VK_CHECK(vkCreatePipelineCache(device, &pipelineCacheInfo, nullptr, &dstPipelineCache));
	pipelineJobCounter.wait();
	VK_CHECK(vkMergePipelineCaches(device, dstPipelineCache, numPipelineCaches, pipelineCaches));
	for (int i = 0; i < numPipelineCaches; i++) {
		vkDestroyPipelineCache(device, pipelineCaches[i], nullptr);
		pipelineCaches[i] = VK_NULL_HANDLE;
	}
}

void VulkanRenderer::buildDefaultPipeline(const void* initialData, size_t initialDataSize, VkPipelineCache& pipelineCache) {
	VkPipelineCacheCreateInfo cacheCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	cacheCreateInfo.flags = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT;
	cacheCreateInfo.initialDataSize = initialDataSize;
	cacheCreateInfo.pInitialData = initialData;
	VK_CHECK(vkCreatePipelineCache(device, &cacheCreateInfo, nullptr, &pipelineCache));
	GraphicsPipelineBuilder defaultPipelineBuilder{ device };
	defaultPipelineBuilder.addShaderStage(VK_SHADER_STAGE_MESH_BIT_EXT, "shaders/default.mesh.spv");
	defaultPipelineBuilder.addShaderStage(VK_SHADER_STAGE_FRAGMENT_BIT, "shaders/default.frag.spv");
	defaultPipelineBuilder.addDynamicState(VK_DYNAMIC_STATE_VIEWPORT);
	defaultPipelineBuilder.addDynamicState(VK_DYNAMIC_STATE_SCISSOR);
	defaultPipelineBuilder.setViewportScissors(1, nullptr, 1, nullptr);
	VkPipelineColorBlendAttachmentState colorBlendState{};
	colorBlendState.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
	defaultPipelineBuilder.addColorBlendAttachmentState(colorBlendState);
	defaultPipelineBuilder.setPipelineLayout(pipelineLayouts[Graphics_Default]);
	defaultPipelineBuilder.setRenderPass(renderPasses[RenderPass_Default]);
	defaultPipelineBuilder.setSubpass(0);
	defaultPipelineBuilder.build(pipelineCache, pipelines[Graphics_Default]);
}

void VulkanRenderer::initImgui(GLFWwindow* window, const void* initialData, size_t initialDataSize, VkPipelineCache& pipelineCache) {
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::GetIO();
	ImGui::StyleColorsDark();
	ImGui_ImplGlfw_InitForVulkan(window, true);
	if (imguiDescriptorPool == VK_NULL_HANDLE) {
		VkDescriptorPoolSize pool_sizes[] =
		{
			{ VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
			{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
		};
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000 * IM_ARRAYSIZE(pool_sizes);
		pool_info.poolSizeCount = (uint32_t)IM_ARRAYSIZE(pool_sizes);
		pool_info.pPoolSizes = pool_sizes;
		VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &imguiDescriptorPool));
	}
	ImGui_ImplVulkan_InitInfo imguiInitInfo{};
	imguiInitInfo.Device = device;
	imguiInitInfo.DescriptorPool = imguiDescriptorPool;
	imguiInitInfo.CheckVkResultFn = [](VkResult err) { assert(err == VK_SUCCESS); };
	imguiInitInfo.ImageCount = swapchain.image_count;
	imguiInitInfo.Instance = instance;
	imguiInitInfo.MinImageCount = 2;
	imguiInitInfo.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
	imguiInitInfo.PhysicalDevice = physicalDevice;
	VkPipelineCacheCreateInfo cacheCreateInfo{ VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO };
	cacheCreateInfo.flags = VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT;
	cacheCreateInfo.initialDataSize = initialDataSize;
	cacheCreateInfo.pInitialData = initialData;
	VK_CHECK(vkCreatePipelineCache(device, &cacheCreateInfo, nullptr, &pipelineCache));
	imguiInitInfo.PipelineCache = pipelineCache;
	imguiInitInfo.Queue = graphicsQueue;
	imguiInitInfo.QueueFamily = graphicsQueueIndex;
	imguiInitInfo.Subpass = 0;
	ImGui_ImplVulkan_Init(&imguiInitInfo, renderPasses[RenderPass_Default]);
	std::vector<VkCommandBuffer>& commandBuffers = graphicsCommandPool.getCommandBuffers(1);
	VkCommandBuffer commandBuffer = commandBuffers.at(0);
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
	ImGui_ImplVulkan_CreateFontsTexture(commandBuffer);
	vkEndCommandBuffer(commandBuffer);
	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VK_CHECK(vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE));
	VK_CHECK(vkQueueWaitIdle(graphicsQueue));
	ImGui_ImplVulkan_DestroyFontUploadObjects();
}

void VulkanRenderer::destroyImgui() {
	ImGui_ImplVulkan_Shutdown();
	ImGui_ImplGlfw_Shutdown();
	ImGui::DestroyContext();
	vkDestroyDescriptorPool(device, imguiDescriptorPool, nullptr);
}

void VulkanRenderer::initStaticDescriptorSets() {
	VkBufferCreateInfo bufCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufCreateInfo.size = sizeof(UniformMatrices);
	bufCreateInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
	bufCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VmaAllocationCreateInfo allocCreateInfo = {};
	allocCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
		VMA_ALLOCATION_CREATE_HOST_ACCESS_ALLOW_TRANSFER_INSTEAD_BIT |
		VMA_ALLOCATION_CREATE_MAPPED_BIT;
	VkDescriptorSetVariableDescriptorCountAllocateInfo 
		variableDescriptorAllocInfo{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_VARIABLE_DESCRIPTOR_COUNT_ALLOCATE_INFO };
	uint32_t descriptorCount = MAX_TEXTURES;
	variableDescriptorAllocInfo.descriptorSetCount = 1;
	variableDescriptorAllocInfo.pDescriptorCounts = &descriptorCount;
	for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
		VK_CHECK(vmaCreateBuffer(allocator, &bufCreateInfo, &allocCreateInfo, &transformsBuffers[i],
			&transformsBufferAllocations[i], &transformsBufferAllocationInfos[i]));
		defaultDescriptorAllocator.allocateSet(transformsLayout, nullptr, transformsDescriptorSets[i]);
		bindlessDescriptorAllocator.allocateSet(materialsLayout, &variableDescriptorAllocInfo, materialsDescriptorSets[i]);
		VkWriteDescriptorSet descriptorWrite{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
		descriptorWrite.dstSet = transformsDescriptorSets[i];
		descriptorWrite.dstBinding = 0;
		descriptorWrite.dstArrayElement = 0;
		descriptorWrite.descriptorCount = 1;
		descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		VkDescriptorBufferInfo descriptorBufInfo{
			transformsBuffers[i],
			0,
			VK_WHOLE_SIZE
		};
		descriptorWrite.pBufferInfo = &descriptorBufInfo;
		vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
	}
	
}

void VulkanRenderer::stageBuffer(const void* srcData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer outBuffer, VmaAllocation outAllocation) {
	VkBufferCreateInfo bufferCreateInfo{ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferCreateInfo.size = size;
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	VmaAllocationCreateInfo allocationCreateInfo{};
	allocationCreateInfo.usage = VMA_MEMORY_USAGE_AUTO;
	allocationCreateInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	VkBuffer stagingBuffer = VK_NULL_HANDLE;
	VmaAllocation stagingBufferAllocation = nullptr;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &allocationCreateInfo, &stagingBuffer,
		&stagingBufferAllocation, nullptr));
	void* stagingBufferPtr = nullptr;
	VK_CHECK(vmaMapMemory(allocator, stagingBufferAllocation, &stagingBufferPtr));
	memcpy(stagingBufferPtr, srcData, size);
	vmaUnmapMemory(allocator, stagingBufferAllocation);
	BufferInfo bufferInfo{ outBuffer, usage };
	bufferInfos.push_back(bufferInfo);
	stagingBuffers.push_back(stagingBuffer);
	stagingBufferAllocations.push_back(stagingBufferAllocation);
	stagingBufferSizes.push_back(size);
}

void VulkanRenderer::uploadBuffers() {
	//do all the copy commands and queue transfer memory barriers and then submit using fence and semaphore
	static std::vector<VkBufferMemoryBarrier> bufferMemoryBarriers;
	bufferMemoryBarriers.clear();
	std::vector<VkCommandBuffer>& commandBuffers = transferCommandPool.getCommandBuffers(1);
	VkCommandBuffer commandBuffer = commandBuffers.at(0);
	VkCommandBufferBeginInfo beginInfo{ VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
	beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	VK_CHECK(vkBeginCommandBuffer(commandBuffer, &beginInfo));
	for (int i = 0; i < stagingBuffers.size(); i++) {
		VkBufferCopy bufferCopy{};
		bufferCopy.size = stagingBufferSizes.at(i);
		vkCmdCopyBuffer(commandBuffer, stagingBuffers.at(i), bufferInfos.at(i).buffer, 1, &bufferCopy);
		VkBufferMemoryBarrier bufferMemoryBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		bufferMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		bufferMemoryBarrier.srcQueueFamilyIndex = transferQueueIndex;
		bufferMemoryBarrier.dstQueueFamilyIndex = graphicsQueueIndex;
		bufferMemoryBarrier.buffer = bufferInfos.at(i).buffer;
		bufferMemoryBarrier.size = VK_WHOLE_SIZE;
		bufferMemoryBarriers.push_back(bufferMemoryBarrier);
	}
	vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr,
		static_cast<uint32_t>(bufferMemoryBarriers.size()), bufferMemoryBarriers.data(), 0, nullptr);
	VK_CHECK(vkEndCommandBuffer(commandBuffer));
	VkSubmitInfo submitInfo{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
	//std::cout << "Transfer semaphore signalled\n";
	submitInfo.signalSemaphoreCount = 1;
	submitInfo.pSignalSemaphores = &transferSemaphore;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
	VK_CHECK(vkQueueSubmit(transferQueue, 1, &submitInfo, stagingBufferFence));
}

void VulkanRenderer::finalizeBufferUpload() {
	vkWaitForFences(device, 1, &stagingBufferFence, VK_TRUE, UINT64_MAX);
	VK_CHECK(vkResetFences(device, 1, &stagingBufferFence));
	for (int i = 0; i < stagingBuffers.size(); i++) {
		vmaDestroyBuffer(allocator, stagingBuffers.at(i), stagingBufferAllocations.at(i));
	}
	stagingBuffers.clear();
	stagingBufferAllocations.clear();
	stagingBufferSizes.clear();
	bufferInfos.clear();
}

void VulkanRenderer::updateDynamicBuffer(DynamicBufferUploadJobArgs jobArgs) {
	VkMemoryPropertyFlags flags = 0;
	vmaGetAllocationMemoryProperties(allocator, jobArgs.bufferAlloc, &flags);
	if (flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
		memcpy(jobArgs.bufferAllocInfo.pMappedData, jobArgs.data, jobArgs.size);
	}
	else {
		VkBufferMemoryBarrier bufferMemoryBarrier{ VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
		bufferMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
		bufferMemoryBarrier.srcQueueFamilyIndex = transferQueueIndex;
		bufferMemoryBarrier.dstQueueFamilyIndex = graphicsQueueIndex;
		bufferMemoryBarrier.buffer = jobArgs.buffer;
		bufferMemoryBarrier.size = VK_WHOLE_SIZE;
		dynamicBufferBarriers.push_back(bufferMemoryBarrier);
		Job bufferUploadJob{};
		bufferUploadJob.counter = &dynamicBufferUploadCounter;
		bufferUploadJob.jobArgs = &jobArgs;
		bufferUploadJob.func = [this](void* jobArgs) {
			DynamicBufferUploadJobArgs* uploadJobArgs = reinterpret_cast<DynamicBufferUploadJobArgs*>(jobArgs);
			stageBuffer(uploadJobArgs->data, uploadJobArgs->size, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, uploadJobArgs->buffer,
				uploadJobArgs->bufferAlloc);
			uploadBuffers();
			finalizeBufferUpload();
		};
		uploadedDynamicBuffers = true;
		ioThread.jobQueue.pushJob(bufferUploadJob);
	}
}

void VulkanRenderer::uploadTestCube() {
	Mesh* mesh = new Mesh();
	VkBuffer vertexBuffer = VK_NULL_HANDLE, meshletBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = nullptr, meshletBufferAllocation = nullptr;
	VkBufferCreateInfo bufferCreateInfo = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
	bufferCreateInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
	bufferCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	bufferCreateInfo.size = 8*sizeof(Vertex);
	VmaAllocationCreateInfo bufferAllocInfo{};
	bufferAllocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &bufferAllocInfo, &vertexBuffer, &vertexBufferAllocation, nullptr));
	mesh->vertexBuffer = vertexBuffer;
	mesh->vertexBufferAllocation = vertexBufferAllocation;
	bufferCreateInfo.size = sizeof(Meshlet);
	VK_CHECK(vmaCreateBuffer(allocator, &bufferCreateInfo, &bufferAllocInfo, &meshletBuffer, &meshletBufferAllocation, nullptr));
	mesh->meshletBuffer = meshletBuffer;
	mesh->meshletBufferAllocation = meshletBufferAllocation;
	pendingMeshes.push_back(mesh);
	
	Job uploadTestCubeJob{};
	uploadTestCubeJob.jobArgs = mesh;
	uploadTestCubeJob.counter = &meshUploadCounter;
	uploadTestCubeJob.func = [this](void* jobArgs) {
		Mesh* mesh = reinterpret_cast<Mesh*>(jobArgs);
		mesh->numMeshlets = 1;
		std::vector<Vertex> vertices;
		for (int i = 0; i < 8; i++) {
			Vertex vertex;
			vertex.pos.x = cubeVertices[4 * i];
			vertex.pos.y = cubeVertices[4 * i + 1];
			vertex.pos.z = cubeVertices[4 * i + 2];
			vertex.pos.w = cubeVertices[4 * i + 3];
			vertices.push_back(vertex);
		}
		stageBuffer(vertices.data(), sizeof(Vertex)*vertices.size(), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 
			mesh->vertexBuffer, mesh->vertexBufferAllocation);
		Meshlet meshlet;
		meshlet.vertexCount = 8;
		for (int i = 0; i < 8; i++) {
			meshlet.vertices[i] = i;
		}
		for (int i = 8; i < MESHLET_MAX_VERTICES; i++) {
			meshlet.vertices[i] = 0;
		}
		meshlet.indexCount = 36;
		for (int i = 0; i < 36; i++) {
			meshlet.indices[i] = cubeIndices[i];
		}
		for (int i = 36; i < MESHLET_MAX_INDICES; i++) {
			meshlet.indices[i] = 0;
		}
		stageBuffer(&meshlet, sizeof(meshlet), VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, mesh->meshletBuffer, mesh->meshletBufferAllocation);
		uploadBuffers();
		finalizeBufferUpload();
	};
	uploadedMeshes = true;
	ioThread.jobQueue.pushJob(uploadTestCubeJob);
}
