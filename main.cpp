#include <iostream>
#include "vulkan_renderer.h"
#include <chrono>

JobManager jobManager;
IOThread ioThread;
UIState uiState{};
Camera camera;
EntityManager entityManager;
VulkanRenderer renderer{ jobManager, ioThread };

int main() {
	jobManager.initialize(JobManager::getMaxPossibleThreads());
    ioThread.initialize();

    GLFWCallbackData callbackData;
    callbackData.camera = &camera;
    callbackData.uiState = &uiState;
    
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    //glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(INIT_SCR_WIDTH, INIT_SCR_HEIGHT, "Vulkan", nullptr, nullptr);
    glfwSetWindowUserPointer(window, &callbackData);
    glfwSetCursorPosCallback(window, cursorPositionCallback);
    renderer.startUp(window);

    double deltaTime = 0, previousTime = 0;
    glfwSetTime(0);
    while (!glfwWindowShouldClose(window)) {
        processKeyboard(window, camera, deltaTime, uiState); //updates camera and window objects

        //command building and swapchain acquisition, queue submission, and presentation happens here
        renderer.render(window, camera, uiState, entityManager);

        double currentTime = glfwGetTime();
        deltaTime = currentTime - previousTime; //measures time taken to process and submit work for a frame to be shown
        previousTime = currentTime;
        callbackData.deltaTime = deltaTime;
        glfwPollEvents();
    }
    renderer.wait();

    renderer.shutDown();
    glfwDestroyWindow(window);
    glfwTerminate();

	return EXIT_SUCCESS;
}