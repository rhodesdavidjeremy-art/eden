# SQLite: normally the system library. Android has no SQLite development files, so the build can compile the
# official amalgamation in (it must include FTS5, which the library schema uses).
set(EDEN_SQLITE_AMALGAMATION_DIR "" CACHE PATH "Folder with sqlite3.c and sqlite3.h to compile into the app")
if(EDEN_SQLITE_AMALGAMATION_DIR)
  add_library(eden_sqlite STATIC ${EDEN_SQLITE_AMALGAMATION_DIR}/sqlite3.c)
  target_include_directories(eden_sqlite PUBLIC ${EDEN_SQLITE_AMALGAMATION_DIR})
  target_compile_definitions(eden_sqlite PRIVATE SQLITE_THREADSAFE=1 SQLITE_ENABLE_FTS5 SQLITE_ENABLE_JSON1
                                                  SQLITE_DQS=0 SQLITE_OMIT_LOAD_EXTENSION)
  set_target_properties(eden_sqlite PROPERTIES C_VISIBILITY_PRESET hidden POSITION_INDEPENDENT_CODE ON)
  add_library(SQLite::SQLite3 ALIAS eden_sqlite)
else()
  find_package(SQLite3 REQUIRED)
endif()
find_package(Threads REQUIRED)

# Embedded CPython (Android): prefix of a CPython build holding include/python3.x and lib/libpython3.x.so
set(EDEN_EMBED_PYTHON_PREFIX "" CACHE PATH "CPython prefix to embed in-process instead of launching python")

set(EDEN_CORE_SOURCES
  ai/CardService.cpp
  ai/ChildProcess.cpp
  ai/ChildProcessWin.cpp
  ai/EmbeddedPython.cpp
  ai/SidecarClient.cpp
  app/AppController.cpp
  app/AuthorTools.cpp
  app/BackgroundSimulation.cpp
  app/BundleExtractor.cpp
  app/CardPanelModel.cpp
  app/CommentService.cpp
  app/FunctionDirectory.cpp
  app/RenderBlocks.cpp
  app/AppPaths.cpp
  app/KeyStore.cpp
  app/LibraryModel.cpp
  app/ReaderModel.cpp
  app/TaskRunner.cpp
  app/ThemeManager.cpp
  export/DocxExporter.cpp
  export/ExportModel.cpp
  export/ExportService.cpp
  export/ImageInfo.cpp
  export/PdfExporter.cpp
  export/PdfWriter.cpp
  export/PptxExporter.cpp
  export/SimpleExporters.cpp
  export/SlideDeck.cpp
  common/Clock.cpp
  common/Hash.cpp
  common/Ids.cpp
  common/Json.cpp
  common/Log.cpp
  common/Strings.cpp
  doc/DocumentImporter.cpp
  doc/EdenFormat.cpp
  doc/DocumentLoader.cpp
  doc/DocxReader.cpp
  doc/EpubReader.cpp
  doc/ExtractedDocument.cpp
  doc/HtmlText.cpp
  doc/Inflate.cpp
  doc/PackSegmenter.cpp
  doc/PdfDocument.cpp
  doc/PdfFont.cpp
  doc/PdfObjects.cpp
  doc/PdfReader.cpp
  doc/PdfText.cpp
  doc/TextUtil.cpp
  doc/TxtReader.cpp
  doc/Xml.cpp
  doc/Zip.cpp
  db/Database.cpp
  db/Migrations.cpp
  db/Statement.cpp
  model/Models.cpp
  repo/AiSessionRepository.cpp
  repo/BookmarkRepository.cpp
  repo/CardRepository.cpp
  repo/CollectionRepository.cpp
  repo/CommentRepository.cpp
  repo/DocumentRepository.cpp
  repo/DocumentTextRepository.cpp
  repo/PackRepository.cpp
  repo/SettingsRepository.cpp
  repo/SourceRepository.cpp
  repo/UserRepository.cpp
)

add_library(eden_core STATIC ${EDEN_CORE_SOURCES})
target_include_directories(eden_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(eden_core PUBLIC SQLite::SQLite3 Threads::Threads)
if(EDEN_EMBED_PYTHON_PREFIX)
  file(GLOB EDEN_PYTHON_LIBRARIES "${EDEN_EMBED_PYTHON_PREFIX}/lib/libpython3*.so")
  file(GLOB EDEN_PYTHON_INCLUDES "${EDEN_EMBED_PYTHON_PREFIX}/include/python3*")
  if(NOT EDEN_PYTHON_LIBRARIES OR NOT EDEN_PYTHON_INCLUDES)
    message(FATAL_ERROR "EDEN_EMBED_PYTHON_PREFIX=${EDEN_EMBED_PYTHON_PREFIX} has no lib/libpython3*.so or include/python3*")
  endif()
  list(GET EDEN_PYTHON_LIBRARIES 0 EDEN_PYTHON_LIBRARY)
  list(GET EDEN_PYTHON_INCLUDES 0 EDEN_PYTHON_INCLUDE)
  add_library(eden_python SHARED IMPORTED)
  set_target_properties(eden_python PROPERTIES IMPORTED_LOCATION "${EDEN_PYTHON_LIBRARY}"
                                                INTERFACE_INCLUDE_DIRECTORIES "${EDEN_PYTHON_INCLUDE}")
  target_link_libraries(eden_core PUBLIC eden_python)
  target_compile_definitions(eden_core PUBLIC EDEN_EMBED_PYTHON=1)
endif()
eden_enable_warnings(eden_core)
