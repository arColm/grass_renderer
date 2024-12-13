#pragma once

#include <glm/ext/vector_float3.hpp>
#include <glm/mat4x4.hpp>

#include <SDL/SDL_events.h>


class Player
{
public:
	glm::vec3 _position;
	glm::vec3 _velocity;

	float _sensitivity = 1.f;
	float _movementSpeed = 10.f;

	glm::mat4 getViewMatrix();
	glm::mat4 getRotationMatrix();

	void processSDLEvent(SDL_Event& e, float deltaTime);

	void update(float deltaTime);
private:

	float _pitch{ 0.f };
	float _yaw{ 0.f };

	float _gravity = -9.81f;


	void move(float deltaTime);

	void handleCollisions();
};