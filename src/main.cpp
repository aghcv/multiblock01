#include "fastvessels/io_utils.hpp"
#include <vtkPolyData.h>
#include <cassert>

int main() {
    try {
        auto dataset = fastvessels::ReadDataSet("data/example.vtp");
        auto poly = vtkPolyData::SafeDownCast(dataset);
        if (!poly) return 1;
        assert(poly->GetNumberOfPoints() > 0);
    } catch (...) {
        return 1;
    }
    return 0;
}
