#pragma once

namespace RE4HT {

void OnPreBeginRendering();
void OnPostBeginRendering();
bool OnPreGuiDrawElement(void* element, void* context);

struct CrosshairProjection {
    float tanRight = 0.0f;
    float tanUp = 0.0f;
    float fovDegrees = 75.0f;
    float rollDegrees = 0.0f;
    bool valid = false;
};

} // namespace RE4HT
