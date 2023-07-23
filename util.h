#pragma once
#include <vector>
#include <string>
#include "config.h"
#include "camera.h"
#include <GLFW/glfw3.h>

struct UIState {
	bool editingText = false;
	bool uiHovered = false;
	bool showUI = true;
};

struct GLFWCallbackData {
	Camera* camera = nullptr;
	double deltaTime = 0;
	UIState* uiState = nullptr;
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
	VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
	VkDebugUtilsMessageTypeFlagsEXT messageType,
	const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
	void* pUserData);

void processKeyboard(GLFWwindow* window, Camera& camera, double deltaTime, UIState& uiState);

void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos);

inline void VK_CHECK(VkResult result) {
	assert(result == VK_SUCCESS);
}

std::vector<char> readSourceFile(const std::string& filename);
VkShaderModule createShaderModule(VkDevice logicalDevice, const std::vector<char>& code);

inline void* offsetPointer(void* ptr, size_t offset) {
	uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
	addr += offset;
	return reinterpret_cast<void*>(addr);
}

template <typename T>
inline T alignAddress(T addr, size_t align) {
	const std::size_t mask = align - 1;
	assert((align & mask) == 0); // power of 2
	return (addr + mask) & ~mask;
}

template <typename T>
inline T* alignPointer(T* ptr, size_t align) {
	const uintptr_t addr = reinterpret_cast<uintptr_t>(ptr);
	const uintptr_t addrAligned = alignAddress(addr, align);
	return reinterpret_cast<T*>(addrAligned);
}