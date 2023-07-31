#pragma once
#include <cstdint>
#include <cstddef>
#include <vulkan/vulkan.h>
#include <array>

//#define SYNC_VALIDATION

#ifdef NDEBUG
const bool enableValidationLayers = false;
#else
const bool enableValidationLayers = true;
#endif

constexpr VkValidationFeatureEnableEXT validationFeatures[] = { VK_VALIDATION_FEATURE_ENABLE_BEST_PRACTICES_EXT,
														   #ifdef SYNC_VALIDATION
														   VK_VALIDATION_FEATURE_ENABLE_SYNCHRONIZATION_VALIDATION_EXT
														   #endif
};
constexpr uint32_t numValidationFeatures = static_cast<uint32_t>(std::size(validationFeatures));

constexpr const char* deviceExtensions[] = { VK_EXT_EXTENDED_DYNAMIC_STATE_EXTENSION_NAME,
											 VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
											 VK_EXT_MESH_SHADER_EXTENSION_NAME };
constexpr uint32_t numDeviceExtensions = static_cast<uint32_t>(std::size(deviceExtensions));

constexpr const uint32_t MAX_JOBS = 1000;

constexpr const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

constexpr uint32_t INIT_SCR_WIDTH = 1600;
constexpr uint32_t INIT_SCR_HEIGHT = 900;
constexpr const float Z_NEAR = 0.01f;
constexpr const float Z_FAR = 300.0f;
constexpr const float INIT_ASPECT_RATIO = (float)INIT_SCR_WIDTH / (float)INIT_SCR_HEIGHT;

constexpr const float PI = 3.14159265358979323846f;
constexpr const float TWO_PI = 2.0f * PI;
constexpr float CAMERA_FOV_Y = 45.0f;
constexpr double CAMERA_SENSITIVITY = 0.8;
constexpr double CAMERA_SPEED = 8.0f;
constexpr double MOUSE_SMOOTHING_FACTOR = 0.5;

constexpr uint8_t FRAME_QUEUE_LENGTH = 2;

constexpr uint32_t MAX_MESHES = 1000;
constexpr uint32_t MAX_MESH_INSTANCES = 5000;

constexpr uint32_t MESHLET_MAX_VERTICES = 32;
constexpr uint32_t MESHLET_MAX_INDICES = MESHLET_MAX_VERTICES * 3;

constexpr uint32_t MAX_MODELS = 500; //allocate this many model matrices at a time
constexpr uint32_t MAX_TEXTURES = 5000;

constexpr uint32_t NUM_FRAME_TIME_SAMPLES = 100;