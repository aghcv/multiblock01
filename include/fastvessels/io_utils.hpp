#pragma once
#include <string>
#include <vtkSmartPointer.h>

class vtkMultiBlockDataSet;

namespace fastvessels {

/**
 * @brief Write a VTK multiblock dataset to a .vtm file for visualization.
 *
 * @param dataset Multi-block dataset to write.
 * @param filename Output file path (should end with .vtm).
 * @throws std::runtime_error if writing fails.
 */
void WriteMultiBlock(const vtkMultiBlockDataSet* dataset, const std::string& filename);

}  // namespace fastvessels
