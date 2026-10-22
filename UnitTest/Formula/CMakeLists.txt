add_executable(PixelFormulaTests FormulaTests.cpp)
target_link_libraries(PixelFormulaTests PRIVATE PixelFormula)
add_test(NAME Pixel.Formula COMMAND PixelFormulaTests)
add_test(NAME Pixel.Formula.Demo COMMAND PixelFormulaDemo
    "${CMAKE_CURRENT_LIST_DIR}/../../Pixel/Formula/Examples/impact_burst.formula" --ticks 30 --hit-at 6)
set_tests_properties(Pixel.Formula.Demo PROPERTIES PASS_REGULAR_EXPRESSION "wind radius=8")

add_executable(PixelFormulaProcessTests ProcessTests.cpp)
target_link_libraries(PixelFormulaProcessTests PRIVATE PixelFormulaFakeHost)
target_compile_definitions(PixelFormulaProcessTests PRIVATE
    PROCESS_EXAMPLE_DIR="${CMAKE_CURRENT_LIST_DIR}/../../Pixel/Formula/Examples/Process")
add_test(NAME Pixel.Formula.Process COMMAND PixelFormulaProcessTests)
add_executable(PixelFormulaProcessStateTests ProcessStateTests.cpp)
target_link_libraries(PixelFormulaProcessStateTests PRIVATE PixelFormulaFakeHost)
add_test(NAME Pixel.Formula.Process.State COMMAND PixelFormulaProcessStateTests)
add_test(NAME Pixel.Formula.Process.Hit COMMAND PixelFormulaProcessDemo
    "${CMAKE_CURRENT_LIST_DIR}/../../Pixel/Formula/Examples/Process/hit.formula" --steps 12 --hit-at 3)
set_tests_properties(Pixel.Formula.Process.Hit PROPERTIES PASS_REGULAR_EXPRESSION "t=4 dig cell=7,0 applied")
add_test(NAME Pixel.Formula.Process.Rejected COMMAND PixelFormulaProcessDemo
    "${CMAKE_CURRENT_LIST_DIR}/../../Pixel/Formula/Examples/Process/receipts.formula" --steps 12 --reject)
set_tests_properties(Pixel.Formula.Process.Rejected PROPERTIES PASS_REGULAR_EXPRESSION "t=0 bolt cell=0,0 rejected")
