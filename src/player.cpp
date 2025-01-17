#include "player.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/transform.hpp>
#include <glm/gtx/quaternion.hpp>

glm::mat4 Player::getViewMatrix()
{
    glm::mat4 cameraTranslation = glm::translate(glm::mat4(1.f), _position);
    glm::mat4 cameraRotation = getRotationMatrix();
    return glm::inverse(cameraTranslation * cameraRotation);
}

glm::mat4 Player::getRotationMatrix()
{
    glm::quat pitchRotation = glm::angleAxis(_pitch, glm::vec3(1.f, 0.f, 0.f));
    glm::quat yawRotation = glm::angleAxis(_yaw, glm::vec3(0.f, -1.f, 0.f));

    return glm::toMat4(yawRotation) * glm::toMat4(pitchRotation);
}

void Player::processSDLEvent(SDL_Event& e, float deltaTime)
{
    if (e.type == SDL_KEYDOWN)
    {
        //really fast "bandaid" movement system
        switch (e.key.keysym.sym)
        {
        case(SDLK_w):
            _velocity.z = -_movementSpeed;
            break;
        case(SDLK_a):
            _velocity.x = -_movementSpeed;
            break;
        case(SDLK_s):
            _velocity.z = _movementSpeed;
            break;
        case(SDLK_d):
            _velocity.x = _movementSpeed;
            break;
        case(SDLK_LCTRL):
            _velocity.y = -_movementSpeed;
            break;
        case(SDLK_SPACE):
            _velocity.y = _movementSpeed;
            break;
        default:
            break;
        }
    }
    if (e.type == SDL_KEYUP)
    {
        switch (e.key.keysym.sym)
        {
        case(SDLK_w):
            _velocity.z = 0;
            break;
        case(SDLK_a):
            _velocity.x = 0;
            break;
        case(SDLK_s):
            _velocity.z = 0;
            break;
        case(SDLK_d):
            _velocity.x = 0;
            break;
        case(SDLK_LCTRL):
            _velocity.y = 0;
            break;
        case(SDLK_SPACE):
            _velocity.y = 0;
            break;
        case(SDLK_LALT):
            if (SDL_GetRelativeMouseMode() == SDL_TRUE)
                SDL_SetRelativeMouseMode(SDL_FALSE);
            else
                SDL_SetRelativeMouseMode(SDL_TRUE);
            break;
        default:
            break;
        }
    }

    if (e.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode() == SDL_TRUE)
    {
        _yaw += (float)e.motion.xrel * deltaTime * _sensitivity;
        _pitch -= (float)e.motion.yrel * deltaTime * _sensitivity;
    }
}

void Player::update(float deltaTime)
{
    move(deltaTime);
}

void Player::move(float deltaTime)
{
    glm::mat4 cameraRotation = getRotationMatrix();
    glm::vec3 v = _velocity;
    v.y = 0;
    glm::vec3 d = glm::vec3(cameraRotation * glm::vec4(v * deltaTime, 0.f));
    d.y = _velocity.y * deltaTime;
    _position += d;
}

void Player::handleCollisions()
{

}
