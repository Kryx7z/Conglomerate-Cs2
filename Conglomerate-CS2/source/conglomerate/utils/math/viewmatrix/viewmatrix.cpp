#include "viewmatrix.h"

#include "../../../../../external/imgui/imgui.h"
#include <cmath>
#include <iostream>

bool ViewMatrix::WorldToScreen(const Vector_t& position, Vector_t& out) const {
    // viewMatrix is filled by a pattern scan in Visuals::init(). If that scan
    // fails it stays null, and this used to dereference it every frame.
    if (viewMatrix == nullptr)
        return false;

    const float w = viewMatrix->matrix[3][0] * position.x + viewMatrix->matrix[3][1] * position.y + viewMatrix->matrix[3][2] * position.z + viewMatrix->matrix[3][3];
    if (!std::isfinite(w) || w <= 0.001f)
        return false;

    const float invW = 1.0f / w;

    ImVec2 wS = ImGui::GetIO().DisplaySize;

    const float centerX = static_cast<float>(wS.x) / 2;
    const float centerY = static_cast<float>(wS.y) / 2;

    out.x = centerX + ((viewMatrix->matrix[0][0] * position.x + viewMatrix->matrix[0][1] * position.y + viewMatrix->matrix[0][2] * position.z + viewMatrix->matrix[0][3]) * invW * centerX);
    out.y = centerY - ((viewMatrix->matrix[1][0] * position.x + viewMatrix->matrix[1][1] * position.y + viewMatrix->matrix[1][2] * position.z + viewMatrix->matrix[1][3]) * invW * centerY);

    // A NaN here would be handed straight to ImGui and end up as NaN vertices
    // in the draw list.
    if (!std::isfinite(out.x) || !std::isfinite(out.y))
        return false;

    return true;
}
