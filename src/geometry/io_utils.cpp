#include "fastvessels/io_utils.hpp"

#include <vtkMultiBlockDataSet.h>
#include <vtkXMLMultiBlockDataWriter.h>
#include <stdexcept>

namespace fastvessels {

void WriteMultiBlock(const vtkMultiBlockDataSet* dataset, const std::string& filename) {
    if (!dataset) {
        throw std::runtime_error("WriteMultiBlock: dataset is null");
    }
    if (filename.empty()) {
        throw std::runtime_error("WriteMultiBlock: filename is empty");
    }
    auto writer = vtkSmartPointer<vtkXMLMultiBlockDataWriter>::New();
    writer->SetFileName(filename.c_str());
    writer->SetInputData(const_cast<vtkMultiBlockDataSet*>(dataset));
    if (writer->Write() != 1) {
        throw std::runtime_error("WriteMultiBlock: failed to write " + filename);
    }
}

}  // namespace fastvessels
