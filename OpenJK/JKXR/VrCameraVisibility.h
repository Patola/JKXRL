/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cmath>
#include <cstring>

// Camera bases were saved as independent entities without an owner link.
inline bool VR_HideActiveCameraPart(bool activeCamera, bool sameEntity,
    const char* model, const float* origin, const float* cameraOrigin)
{
    if (!activeCamera) return false;
    if (sameEntity) return true;
    if (!model || std::strcmp(model, "models/map_objects/kejim/impcam_base.md3")) return false;
    return std::fabs(origin[0] - cameraOrigin[0]) < 0.25f &&
        std::fabs(origin[1] - cameraOrigin[1]) < 0.25f &&
        std::fabs(origin[2] - cameraOrigin[2] - 16.0f) < 0.25f;
}
