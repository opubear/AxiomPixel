add_library(PixelFormulaEditing STATIC FormulaDraft.cpp)
target_include_directories(PixelFormulaEditing PUBLIC "${PIXEL_SOURCE_DIR}")
target_link_libraries(PixelFormulaEditing PUBLIC PixelFormulaValidation)
