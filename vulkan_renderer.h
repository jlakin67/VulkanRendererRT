#pragma once
#include "VkBootstrap.h"
#include "vk_mem_alloc.h"
#include <GLFW/glfw3.h>
#include "scene.h"
#include "job_system.h"
#include "rendering_util.h"
#include <imgui.h>
#include "scene.h"

enum RenderPassNames {
	RenderPass_Default, //just one subpass with swapchain color attachment and depth-stencil attachment only
	numRenderPasses
};

enum FramebufferNames {
	Framebuffer_Default,
	numFramebuffers
};

enum PipelineNames {
	Graphics_Default,
	numPipelines
};

constexpr uint32_t numPipelineCaches = numPipelines + 1; //one included for ImGui

struct Meshlet
{
	uint32_t vertices[MESHLET_MAX_VERTICES];
	uint32_t indices[MESHLET_MAX_INDICES]; // up to 126 triangles
	uint32_t vertexCount;
	uint32_t indexCount;
};

struct Mesh {
	Mesh() {
		for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
			descriptorSet[i] = VK_NULL_HANDLE;
			modelMatrixBuffers[i] = VK_NULL_HANDLE;
			modelMatrixBufferAllocations[i] = nullptr;
			modelMatrixBufferAllocationInfos[i] = VmaAllocationInfo{};
		}
	}
	uint32_t numMeshlets = 0;
	VkBuffer vertexBuffer = VK_NULL_HANDLE;
	VmaAllocation vertexBufferAllocation = nullptr;
	VkBuffer meshletBuffer = VK_NULL_HANDLE;
	VmaAllocation meshletBufferAllocation = nullptr;
	VkImage diffuseTexture = VK_NULL_HANDLE;
	VmaAllocation diffuseTextureAllocation = nullptr;
	VkImage specularTexture = VK_NULL_HANDLE;
	VmaAllocation specularTextureAllocation = nullptr;
	VkBuffer modelMatrixBuffers[FRAME_QUEUE_LENGTH];
	VmaAllocation modelMatrixBufferAllocations[FRAME_QUEUE_LENGTH];
	VmaAllocationInfo modelMatrixBufferAllocationInfos[FRAME_QUEUE_LENGTH];
	std::vector<glm::mat4> models;
	std::vector<uint32_t> entities; //corresponds to the exact same entry in models and should be same length
	VkDescriptorSet descriptorSet[FRAME_QUEUE_LENGTH]; //should be set number 1, representing models, vertexBuffer, and meshletBuffer
	void destroy(VmaAllocator allocator) {
		vmaDestroyBuffer(allocator, vertexBuffer, vertexBufferAllocation);
		vmaDestroyBuffer(allocator, meshletBuffer, meshletBufferAllocation);
		vmaDestroyImage(allocator, diffuseTexture, diffuseTextureAllocation);
		vmaDestroyImage(allocator, specularTexture, specularTextureAllocation);
		for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
			vmaDestroyBuffer(allocator, modelMatrixBuffers[i], modelMatrixBufferAllocations[i]);
		}
	}
	friend bool operator ==(const Mesh& first, const Mesh& second) {
		bool cond = true;
		cond = cond && (first.vertexBuffer == second.vertexBuffer);
		cond = cond && (first.vertexBufferAllocation == second.vertexBufferAllocation);
		cond = cond && (first.diffuseTexture == second.diffuseTexture);
		cond = cond && (first.specularTexture == second.specularTexture);
		return cond;
	}
};

class VulkanRenderer {
public:
	VulkanRenderer(JobManager& jobManager, IOThread& ioThread) : jobManager{ jobManager }, ioThread{ ioThread } {
		for (int i = 0; i < numRenderPasses; i++) {
			renderPasses[i] = VK_NULL_HANDLE;
		}
		for (int i = 0; i < numPipelines; i++) {
			pipelines[i] = VK_NULL_HANDLE;
			pipelineLayouts[i] = VK_NULL_HANDLE;
		}
		for (int i = 0; i < numPipelineCaches; i++) {
			pipelineCaches[i] = VK_NULL_HANDLE;
		}
		for (int i = 0; i < FRAME_QUEUE_LENGTH; i++) {
			transformsBuffers[i] = VK_NULL_HANDLE;
			transformsBufferAllocations[i] = nullptr;
			transformsBufferAllocationInfos[i] = VmaAllocationInfo{};
			acquireSemaphores[i] = VK_NULL_HANDLE;
			presentSemaphores[i] = VK_NULL_HANDLE;
			frameQueueFences[i] = VK_NULL_HANDLE;
			transformsDescriptorSets[i] = VK_NULL_HANDLE;
			uniformMatrices[i] = UniformMatrices{
				glm::mat4{ 1.0f },
				glm::perspective(CAMERA_FOV_Y, INIT_ASPECT_RATIO, Z_NEAR, Z_FAR)
			};
			uniformMatrices[i].projection[1][1] *= -1;
		}
	}

	void startUp(GLFWwindow* window);
	void render(GLFWwindow* window, Camera& camera, UIState& uiState, EntityManager& entityManager);
	void wait() { if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device); }
	void shutDown(); //control destruction order explicitly
private:
	JobManager& jobManager;
	IOThread& ioThread;
	vkb::Instance instance;
	VkSurfaceKHR surface = VK_NULL_HANDLE;
	vkb::PhysicalDevice physicalDevice;
	vkb::Device device;
	VkQueue transferQueue = nullptr, graphicsQueue = nullptr;
	uint32_t transferQueueIndex = 0, graphicsQueueIndex = 0;
	VmaAllocator allocator = nullptr;
	CommandPool graphicsCommandPool;
	//CommandPool auxGraphicsCommandPool[numAuxGraphicsCommandPools];
	CommandPool transferCommandPool; //used by exactly one worker thread
	DescriptorAllocator defaultDescriptorAllocator;
	VkSemaphore acquireSemaphores[FRAME_QUEUE_LENGTH];
	VkSemaphore presentSemaphores[FRAME_QUEUE_LENGTH];
	VkFence frameQueueFences[FRAME_QUEUE_LENGTH];

	uint32_t maxPossibleInstances = MAX_MESH_INSTANCES;

	int currentWidth = INIT_SCR_WIDTH, currentHeight = INIT_SCR_HEIGHT;
	PFN_vkCmdDrawMeshTasksEXT vkCmdDrawMeshTasks = nullptr;

	bool D24_UNORM_S8_UINT_Supported = true;
	bool D32_SFLOAT_S8_UINT_Supported = true;
	bool D32_SFLOAT_Supported = true;
	bool D16_UNORM_S8_UINT_Supported = true;
	bool D16_UNORM_Supported = true;
	VkFormat defaultDepthFormat = VK_FORMAT_D24_UNORM_S8_UINT;
	bool defaultDepthIsStencilFormat = true;
	void chooseDefaultDepthFormat();

	std::vector<VkImage> swapchainDepthImages;
	std::vector<VmaAllocation> swapchainDepthAllocs;
	std::vector<VkImageView> swapchainDepthImageViews;
	void createSwapchainDepthImages();

	VkRenderPass renderPasses[numRenderPasses];
	void createRenderPasses();

	//vector of framebuffers for each swapchain image, for each type of framebuffer used
	std::vector<VkFramebuffer> framebuffer[numFramebuffers];
	void createDefaultFramebuffer();

	VkDescriptorSetLayout transformsLayout = VK_NULL_HANDLE;
	VkDescriptorSetLayout meshLayout = VK_NULL_HANDLE;
	VkPipelineLayout pipelineLayouts[numPipelines];
	void createDescriptorLayouts();

	vkb::Swapchain swapchain;
	std::vector<VkImageView> swapchainImageViews;
	void createSwapchain(GLFWwindow* window);
	void destroySwapchain();

	VkPipeline pipelines[numPipelines];
	void buildPipelines(GLFWwindow* window);

	void buildDefaultPipeline(const void* initialData, size_t initialDataSize, VkPipelineCache& pipelineCache);

	VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;
	void initImgui(GLFWwindow* window, const void* initialData, size_t initialDataSize, VkPipelineCache& pipelineCache);
	void destroyImgui();

	UniformMatrices uniformMatrices[FRAME_QUEUE_LENGTH];
	VkBuffer transformsBuffers[FRAME_QUEUE_LENGTH];
	VmaAllocation transformsBufferAllocations[FRAME_QUEUE_LENGTH];
	VmaAllocationInfo transformsBufferAllocationInfos[FRAME_QUEUE_LENGTH];
	VkDescriptorSet transformsDescriptorSets[FRAME_QUEUE_LENGTH];
	void initTransforms();

	struct BufferInfo {
		VkBuffer buffer = VK_NULL_HANDLE;
		VkBufferUsageFlags flags = 0;
	};
	bool uploadedMeshes = false;
	bool uploadedDynamicBuffers = false;
	VkSemaphore transferSemaphore = VK_NULL_HANDLE;
	// vvv USED BY IO THREAD vvv
	VkFence stagingBufferFence = VK_NULL_HANDLE;
	std::vector<VkBuffer> stagingBuffers;
	std::vector<VmaAllocation> stagingBufferAllocations;
	std::vector<VkDeviceSize> stagingBufferSizes;
	std::vector<BufferInfo> bufferInfos; //the backing buffers that are in the process of being transferred to from staging buffers
	// ^^^ USED BY IO THREAD ^^^
	JobCounter meshUploadCounter;
	JobCounter dynamicBufferUploadCounter;
	std::vector<VkBufferMemoryBarrier> meshBufferBarriers; //don't use these until mesh counter is finished
	std::vector<VkBufferMemoryBarrier> dynamicBufferBarriers; //don't use these until dynamic buffer counter is finished
	std::vector<Mesh*> pendingMeshes; //once mesh counter is complete add these to the meshes map and clear
	uint32_t numMeshesCreated = 0;
	std::unordered_map<uint32_t, Mesh*> meshes; //maps meshIndex to Mesh
	std::vector<std::pair<uint64_t, Mesh*>> deletionQueue;

	//These three functions below are meant to be used in the IOThread:
	
	//Creates a staging buffer and uploads to it and adds it to stagingBuffers,
	//then batches the actual buffer for transfer in bufferInfos.
	void stageBuffer(const void* srcData, VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer outBuffer, VmaAllocation outAllocation);
	//Transfers all the batched, staged data into their respective buffers along with their memory barriers using the transfer queue.
	//Memory barriers release transfer queue's ownership of the backing buffers. Signals transferSemaphore and stagingBufferFence.
	void uploadBuffers();
	//This function waits on stagingBufferFence then destroys all of staging buffers.
	void finalizeBufferUpload();

	struct DynamicBufferUploadJobArgs {
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation bufferAlloc = nullptr;
		VmaAllocationInfo bufferAllocInfo{};
		VkDeviceSize size;
		void* data;
	};
	//If the memory is GPU and CPU visible then a simple memcpy is performed,
	//but if the memory is GPU visible only then the upload will have to be performed on the IO thread.
	//You have to reupload the entire buffer and not parts of it, since in the latter case taking ownership of 
	//a VK_SHARING_MODE_EXCLUSIVE buffer in transferQueue will make the contents undefined.
	void updateDynamicBuffer(DynamicBufferUploadJobArgs jobArgs);

	// vvv USED BY MULTIPLE WORKER THREADS vvv
	VkPipelineCache pipelineCaches[numPipelineCaches];
	// ^^^ USED BY MULTIPLE WORKER THREADS ^^^
	VkPipelineCache dstPipelineCache = VK_NULL_HANDLE;

	void uploadTestCube();

	uint8_t currentFrame = 0;
	uint64_t numFramesProcessed = 0;
};