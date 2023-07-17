#pragma once

#include "config.h"
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

constexpr double CAMERA_PRECISION = 0.05;

class Camera {
public:
	Camera() : phi{ glm::radians(90.0f) }, theta{ glm::radians(90.0f) }, pos(0.0f, 0.0f, 1.0f) {}
	Camera(double phi, double theta, glm::vec3 pos) : phi{ phi }, theta{ theta }, pos{ pos } {}
	glm::mat4 getView() { //right-handed, y up, -z forward
		glm::vec3 front(cos(phi) * sin(theta), cos(theta), -sin(phi) * sin(theta));
		return glm::lookAt(pos, pos + front, glm::vec3(0.0f, 1.0f, 0.0f));
	}
	void processMouse(double deltaX, double deltaY, double deltaTime) {
		phi += deltaX * CAMERA_SENSITIVITY * deltaTime;
		theta += deltaY * CAMERA_SENSITIVITY * deltaTime;
		if (theta < glm::radians(CAMERA_PRECISION)) theta = glm::radians(CAMERA_PRECISION);
		if (theta > glm::radians(180.0 - CAMERA_PRECISION)) theta = glm::radians(180.0 - CAMERA_PRECISION);
	}
	void processKeyboard(int key, double deltaTime) {
		float speed = static_cast<float>(CAMERA_SPEED * deltaTime);
		glm::vec3 front(cos(phi) * sin(theta), cos(theta), -sin(phi) * sin(theta));
		front = glm::normalize(front);
		glm::vec3 temp(0.0f, 1.0f, 0.0f);
		glm::vec3 right = glm::cross(front, temp);
		right = glm::normalize(right);
		switch (key) {
		case GLFW_KEY_W:
			pos += speed * front;
			break;
		case GLFW_KEY_S:
			pos -= speed * front;
			break;
		case GLFW_KEY_D:
			pos += speed * right;
			break;
		case GLFW_KEY_A:
			pos -= speed * right;
			break;
		}
	}
private:
	glm::vec3 pos;
	double phi; //azimuthal, phi = 0 on x-axis, phi = 90 theta = 90 on negative z-axis
	double theta; //polar, theta = 0 on y-axis
};