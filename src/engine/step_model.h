// Turns parsed STEP entities into a renderable triangle mesh.
#pragma once

#include <cstddef>
#include <string>

#include "mesh.h"
#include "step_file.h"

namespace stp {

struct LoadStats {
    std::string schema;
    std::string name;
    int solids = 0;
    int faces = 0;
    int facesFailed = 0;
    std::size_t triangles = 0;
    double deflection = 0.0;
};

// quality is the chord deflection expressed as a fraction of the model's
// bounding-box diagonal. 0.002 looks good for thumbnails, 0.0008 for the viewer.
bool buildMesh(const StepFile& file, Mesh* mesh, double quality, std::string* error,
               LoadStats* stats = nullptr);

bool loadStepFile(const std::string& path, Mesh* mesh, std::string* error,
                  LoadStats* stats = nullptr, double quality = 0.0015);

bool loadStepMemory(const char* data, std::size_t len, Mesh* mesh, std::string* error,
                    LoadStats* stats = nullptr, double quality = 0.0015);

}  // namespace stp
