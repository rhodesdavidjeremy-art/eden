find_package(Qt6 COMPONENTS Widgets QUIET)
if(NOT Qt6_FOUND)
  message(STATUS "Qt6 Widgets not found; the eden_reader GUI target is skipped")
  return()
endif()

set(CMAKE_AUTOMOC ON)

set(EDEN_UI_SOURCES
  main.cpp
  MainWindow.cpp
  LibraryView.cpp
  ReaderView.cpp
  SettingsDialog.cpp
  CardPanel.cpp
  ExportDialog.cpp
  MediaCapture.cpp
  BackgroundWidget.cpp
  AndroidBootstrap.cpp
)
if(ANDROID)
  # On Android the app is a shared library loaded by Qt's Java activity; qt_add_executable sets that up.
  qt_add_executable(eden_reader ${EDEN_UI_SOURCES})
else()
  add_executable(eden_reader ${EDEN_UI_SOURCES})
endif()
target_compile_definitions(eden_reader PRIVATE EDEN_PYTHON_DIR="${CMAKE_SOURCE_DIR}/python")
find_package(Qt6 COMPONENTS Multimedia QUIET)
if(Qt6Multimedia_FOUND)
  target_compile_definitions(eden_reader PRIVATE EDEN_HAS_MULTIMEDIA)
  target_link_libraries(eden_reader PRIVATE Qt6::Multimedia)
endif()
target_link_libraries(eden_reader PRIVATE eden_core Qt6::Widgets)
if(WIN32)
  # GUI subsystem: no console window behind the app
  set_target_properties(eden_reader PROPERTIES WIN32_EXECUTABLE ON)
endif()

if(ANDROID)
  # Folder with AndroidManifest.xml (permissions) and assets/python-bundle.zip, prepared by the CI workflow
  set(EDEN_ANDROID_PACKAGE_DIR "" CACHE PATH "Android package source dir (manifest + assets)")
  if(EDEN_ANDROID_PACKAGE_DIR)
    set_property(TARGET eden_reader PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${EDEN_ANDROID_PACKAGE_DIR}")
  endif()
  # Ship libpython and its companion libraries (libssl, libcrypto, libsqlite3 ...) inside the APK
  if(EDEN_EMBED_PYTHON_PREFIX)
    file(GLOB EDEN_PYTHON_SHARED_LIBS "${EDEN_EMBED_PYTHON_PREFIX}/lib/*.so")
    set_property(TARGET eden_reader PROPERTY QT_ANDROID_EXTRA_LIBS ${EDEN_PYTHON_SHARED_LIBS})
  endif()
endif()
