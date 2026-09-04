#pragma once

#include <glm/glm.hpp>
#include <vector>

class Timeline;

// Pure position solver: turns a Timeline + time into red/blue planet positions
// (or trail sample points). No GL/audio dependency.
class PositionSolver {
public:
    static void positionAt(const Timeline& timeline, double t,
                           glm::dvec2& redOut, glm::dvec2& blueOut);
    static void positionAtTile(const Timeline& timeline, double t, int tileIdx,
                               glm::dvec2& redOut, glm::dvec2& blueOut);

    static void sampleTrail(const Timeline& timeline, double endTime,
                            float trailDuration, float sampleRate,
                            const glm::dvec2& redHead, const glm::dvec2& blueHead,
                            std::vector<glm::dvec2>& redOut,
                            std::vector<glm::dvec2>& blueOut);
};
