add_executable(PixelFormulaTests FormulaTests.cpp)
target_link_libraries(PixelFormulaTests PRIVATE PixelFormula)
add_test(NAME Pixel.Formula COMMAND PixelFormulaTests)
add_test(NAME Pixel.Formula.Demo COMMAND PixelFormulaDemo
    "${CMAKE_CURRENT_LIST_DIR}/../../Pixel/Formula/Examples/impact_burst.formula" --ticks 30 --hit-at 6)
set_tests_properties(Pixel.Formula.Demo PROPERTIES PASS_REGULAR_EXPRESSION "wind radius=8")
