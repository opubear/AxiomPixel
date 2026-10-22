#include "Scenarios.h"
#include <string_view>
#include <stdexcept>

int main(int argc, char **argv) {
    using namespace pixel::formula::experiment;
    if (argc == 1) Verify();
    else if (argc == 2 && std::string_view(argv[1]) == "--benchmark") Benchmark();
    else throw std::invalid_argument("usage: PixelFormulaSemantics [--benchmark]");
}
