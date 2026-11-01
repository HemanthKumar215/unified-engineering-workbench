// Tiny SVG line/marker chart writer used for the labelled plots in results/.
#pragma once
#include <string>
#include <vector>

namespace wb {

struct Series {
    std::string label;
    std::vector<double> x, y;
    std::string color = "#2563eb";
    bool markers = false;   // draw points instead of a line
    bool dashed = false;
};

struct PlotSpec {
    std::string title;
    std::string xLabel;   // include units, e.g. "x [m]"
    std::string yLabel;
    bool logX = false, logY = false;
    int width = 760, height = 480;
};

std::string renderSvg(const PlotSpec& spec, const std::vector<Series>& series);

}  // namespace wb
