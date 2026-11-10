add_executable(eden_tests
  test_main.cpp
  test_common.cpp
  test_json.cpp
  test_inflate.cpp
  test_xml.cpp
  test_readers.cpp
  test_segmenter.cpp
  test_importer.cpp
  test_process.cpp
  test_sidecar.cpp
  test_card_service.cpp
  test_app.cpp
  test_reader_model.cpp
  test_card_panel.cpp
  test_export.cpp
  test_eden_format.cpp
  test_database.cpp
  test_repositories.cpp
)
target_link_libraries(eden_tests PRIVATE eden_core)
target_compile_definitions(eden_tests PRIVATE EDEN_FAKE_SIDECAR="${CMAKE_CURRENT_SOURCE_DIR}/fake_sidecar.py")
target_include_directories(eden_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME eden_tests COMMAND eden_tests)
