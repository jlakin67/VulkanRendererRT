#include "util.h"
#include <iostream>
#include <fstream>

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    //Severity:
    //VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT: Diagnostic message
    //VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT : Informational message like the creation of a resource
    //VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT : Message about behavior that is not necessarily an error, but very likely a bug in your application
    //VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT : Message about behavior that is invalid and may cause crashes
    if (messageSeverity <= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) return VK_FALSE;
    if (pCallbackData->messageIdNumber == 0) return VK_FALSE; //loader info about manifest

    std::cerr << "\nValidation layer:\n";
    std::cerr << "Severity: ";
    switch (messageSeverity) {
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT:
        std::cerr << "Diagnostic/Verbose\n";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT:
        std::cerr << "Informational/Resource Creation\n";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT:
        std::cerr << "Warning\n";
        break;
    case VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT:
        std::cerr << "Error\n";
        break;
    }

    // The messageType parameter can have the following values :
    //VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT: Some event has happened that is unrelated to the specification or performance
    //VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT : Something has happened that violates the specification or indicates a possible mistake
    //VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT : Potential non - optimal use of Vulkan
    std::cerr << "Message type: ";
    switch (messageType) {
    case VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT:
        std::cerr << "General\n";
        break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT:
        std::cerr << "Validation\n";
        break;
    case VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT:
        std::cerr << "Performance\n";
        break;
    }

    std::cerr << "Message: " << pCallbackData->pMessage << std::endl;

    std::cerr << "\n----------------\n";

    return VK_FALSE;
}

void processKeyboard(GLFWwindow* window, Camera& camera, double deltaTime, UIState& uiState) {
    static bool spaceKeyPressed = false;
    if (!uiState.editingText) {
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS) glfwSetWindowShouldClose(window, true);
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
        {
            camera.processKeyboard(GLFW_KEY_W, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) {
            camera.processKeyboard(GLFW_KEY_S, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) {
            camera.processKeyboard(GLFW_KEY_D, deltaTime);
        }
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) {
            camera.processKeyboard(GLFW_KEY_A, deltaTime);
        }
        if (!spaceKeyPressed) spaceKeyPressed = (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS);
        if (spaceKeyPressed && glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_RELEASE) {
            uiState.showUI = !uiState.showUI;
            spaceKeyPressed = false;
        }
    }
}

void cursorPositionCallback(GLFWwindow* window, double xpos, double ypos) {
    static bool firstMove = true;
    static double lastX = 0, lastY = 0, finalDeltaX = 0, finalDeltaY = 0;

    GLFWCallbackData* callbackData = reinterpret_cast<GLFWCallbackData*>(glfwGetWindowUserPointer(window));
    assert(callbackData);
    if (callbackData->uiState->uiHovered) return;

    double deltaX = 0, deltaY = 0;
    if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
        if (firstMove) {
            lastX = xpos;
            lastY = ypos;
            firstMove = false;
        }
        else {
            deltaX = xpos - lastX;
            deltaY = ypos - lastY;
            finalDeltaX = MOUSE_SMOOTHING_FACTOR * deltaX + (1.0 - MOUSE_SMOOTHING_FACTOR) * finalDeltaX;
            finalDeltaY = MOUSE_SMOOTHING_FACTOR * deltaY + (1.0 - MOUSE_SMOOTHING_FACTOR) * finalDeltaY;
        }
    }
    else {
        finalDeltaX = 0;
        finalDeltaY = 0;
    }

    lastX = xpos; lastY = ypos;
    callbackData->camera->processMouse(-finalDeltaX, -finalDeltaY, callbackData->deltaTime);
}

std::vector<char> readSourceFile(const std::string& filename) {
    std::ifstream file(filename, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "Failed to open file: " << filename << std::endl;
        exit(EXIT_FAILURE);
    }
    size_t fileSize = static_cast<size_t>(file.tellg());
    std::vector<char> buffer(fileSize);
    file.seekg(0);
    file.read(buffer.data(), fileSize);
    file.close();
    return buffer;
}

VkShaderModule createShaderModule(VkDevice logicalDevice, const std::vector<char>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());
    VkShaderModule shaderModule;
    VK_CHECK(vkCreateShaderModule(logicalDevice, &createInfo, nullptr, &shaderModule));
    return shaderModule;
}