cmake_minimum_required(VERSION 3.25)
project(dio_voice VERSION 0.1.0 LANGUAGES C CXX RC)

if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "DIO Voice v1 supports 64-bit Windows with MSVC only")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "DIO Voice requires a 64-bit build")
endif()

include(CTest)

option(
  DIO_BOOTSTRAP_ONLY
  "Configure only the dependency-free portable bootstrap"
  OFF
)
set(
  DIO_BOOTSTRAP_MANIFEST_URL
  ""
  CACHE STRING
  "Pinned HTTPS release manifest URL embedded in the bootstrap"
)
set(
  DIO_BOOTSTRAP_MANIFEST_SHA256
  ""
  CACHE STRING
  "Pinned release manifest SHA-256 embedded in the bootstrap"
)

set(
  DIO_RUNTIME_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/runtime"
  CACHE PATH
  "Build-time native runtime prepared by scripts/bootstrap-runtime.ps1"
)
set(
  DIO_TEST_MODEL_DIR
  "${CMAKE_CURRENT_SOURCE_DIR}/models"
  CACHE PATH
  "Pinned baseline model directory used only by deterministic tests"
)
function(dio_configure_c17 target)
  target_compile_features(${target} PRIVATE c_std_17)
  target_compile_definitions(${target} PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
  )
  target_compile_options(${target} PRIVATE
    /W4
    /WX
    /utf-8
    /Zc:preprocessor
  )
  set_property(
    TARGET ${target}
    PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  target_link_options(${target} PRIVATE /Brepro)
  set_target_properties(${target} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
  )
endfunction()

add_library(dio_yyjson STATIC
  third_party/yyjson/yyjson.c
)
target_include_directories(dio_yyjson PUBLIC
  third_party/yyjson
)
target_compile_options(dio_yyjson PRIVATE /W0 /utf-8)
set_property(
  TARGET dio_yyjson
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_executable(dio_voice_bootstrap WIN32
  src/bootstrap/main.c
  src/bootstrap/bootstrap.rc
)
dio_configure_c17(dio_voice_bootstrap)
target_compile_definitions(dio_voice_bootstrap PRIVATE
  "DIO_BOOTSTRAP_MANIFEST_URL=L\"${DIO_BOOTSTRAP_MANIFEST_URL}\""
  "DIO_BOOTSTRAP_MANIFEST_SHA256=L\"${DIO_BOOTSTRAP_MANIFEST_SHA256}\""
)
target_link_libraries(dio_voice_bootstrap PRIVATE
  dio_yyjson
  bcrypt
  cabinet
  comctl32
  shell32
  winhttp
)
target_link_options(dio_voice_bootstrap PRIVATE /MANIFEST:NO)
set_target_properties(dio_voice_bootstrap PROPERTIES
  OUTPUT_NAME "dio-voice"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bootstrap"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bootstrap"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bootstrap"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bootstrap"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bootstrap"
)
add_test(
  NAME bootstrap-self-test
  COMMAND "$<TARGET_FILE:dio_voice_bootstrap>" --self-test
)
add_test(
  NAME bootstrap-ui-smoke
  COMMAND powershell.exe
    -NoProfile
    -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-bootstrap-ui.ps1"
    -BootstrapPath "$<TARGET_FILE:dio_voice_bootstrap>"
)
set_tests_properties(bootstrap-ui-smoke PROPERTIES TIMEOUT 10)
add_test(
  NAME bootstrap-download-self-test
  COMMAND powershell.exe
    -NoProfile
    -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/scripts/test-bootstrap-download.ps1"
    -BootstrapPath "$<TARGET_FILE:dio_voice_bootstrap>"
)
set_tests_properties(bootstrap-download-self-test PROPERTIES TIMEOUT 90)

if(DIO_BOOTSTRAP_ONLY)
  return()
endif()

set(
  DIO_CUI_PREFIX
  ""
  CACHE PATH
  "Installed CUI prefix for developer builds; never embedded in releases"
)
if(DIO_CUI_PREFIX)
  find_package(CUI CONFIG REQUIRED PATHS "${DIO_CUI_PREFIX}" NO_DEFAULT_PATH)
else()
  find_package(CUI CONFIG REQUIRED)
endif()

add_library(dio_miniaudio STATIC
  src/core/miniaudio_impl.c
)
target_compile_features(dio_miniaudio PRIVATE c_std_17)
target_compile_definitions(dio_miniaudio PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)
target_compile_options(dio_miniaudio PRIVATE /W0 /utf-8)
target_include_directories(dio_miniaudio PUBLIC
  third_party/miniaudio
)
set_property(
  TARGET dio_miniaudio
  PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_library(dio_agent STATIC src/agent/agent_harness.c)
dio_configure_c17(dio_agent)
target_include_directories(dio_agent PUBLIC include)
target_link_libraries(dio_agent PRIVATE dio_yyjson)
target_link_libraries(dio_agent PRIVATE advapi32 bcrypt)

add_executable(dio_agent_tests tests/test_agent_harness.c)
dio_configure_c17(dio_agent_tests)
target_link_libraries(dio_agent_tests PRIVATE dio_agent dio_yyjson)
add_test(NAME agent-harness COMMAND "$<TARGET_FILE:dio_agent_tests>")
set_tests_properties(agent-harness PROPERTIES TIMEOUT 30)

add_library(dio_voice_inference STATIC
  src/core/audio_wav.c
  src/core/ort_runtime.c
  src/core/vosk_runtime.c
  src/core/wake_onnx.c
)
dio_configure_c17(dio_voice_inference)
target_include_directories(dio_voice_inference
  PUBLIC include
  PRIVATE
    src/core
    third_party/onnxruntime/include
    third_party/vosk/include
)
target_link_libraries(dio_voice_inference PRIVATE dio_yyjson)

add_library(dio_voice_core STATIC
  src/core/audio_capture.c
  src/core/earcon.c
  src/core/pcm_stream.c
  src/core/tts.c
  src/core/voice_core.c
)
dio_configure_c17(dio_voice_core)
target_include_directories(dio_voice_core
  PUBLIC include
  PRIVATE
    src/core
    third_party/miniaudio
    third_party/onnxruntime/include
    third_party/vosk/include
)
target_link_libraries(dio_voice_core
  PUBLIC dio_voice_inference
  PRIVATE
    dio_miniaudio
    dio_yyjson
    iphlpapi
    ole32
    winhttp
)

add_executable(dio_voice_core_tests
  tests/test_voice_core.c
)
dio_configure_c17(dio_voice_core_tests)
target_include_directories(dio_voice_core_tests PRIVATE
  src/core
  third_party/miniaudio
  third_party/onnxruntime/include
  third_party/vosk/include
)
target_link_libraries(dio_voice_core_tests PRIVATE
  dio_voice_core
  dio_yyjson
)
add_test(
  NAME voice-core-goldens
  COMMAND
    "$<TARGET_FILE:dio_voice_core_tests>"
    "${DIO_RUNTIME_DIR}/onnxruntime/onnxruntime.dll"
    "${DIO_TEST_MODEL_DIR}/porcupine"
    "${DIO_TEST_MODEL_DIR}/silero_vad.onnx"
    "${DIO_RUNTIME_DIR}/vosk/libvosk.dll"
    "${DIO_TEST_MODEL_DIR}/vosk-model-fa-0.42"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wake"
)
set_tests_properties(voice-core-goldens PROPERTIES TIMEOUT 90)

add_executable(dio_pcm_stream_tests
  tests/test_pcm_stream.c
  src/core/earcon.c
  src/core/pcm_stream.c
  src/core/miniaudio_impl.c
)
dio_configure_c17(dio_pcm_stream_tests)
target_compile_definitions(dio_pcm_stream_tests PRIVATE
  DIO_MINIAUDIO_NULL_ONLY=1
)
target_include_directories(dio_pcm_stream_tests PRIVATE
  src/core
  third_party/miniaudio
)
add_test(NAME pcm-stream-null COMMAND "$<TARGET_FILE:dio_pcm_stream_tests>")
set_tests_properties(pcm-stream-null PROPERTIES TIMEOUT 10)

add_executable(dio_voice_probe
  tools/dio_voice_probe.c
)
dio_configure_c17(dio_voice_probe)
target_include_directories(dio_voice_probe PRIVATE include)
target_link_libraries(dio_voice_probe PRIVATE dio_voice_core)
add_test(
  NAME voice-probe-wav
  COMMAND
    "$<TARGET_FILE:dio_voice_probe>"
    --wav "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/wake/sapi-alexa-command-hava.wav"
    --root "${CMAKE_CURRENT_SOURCE_DIR}"
    --models "${DIO_TEST_MODEL_DIR}"
    --vosk-model "${DIO_TEST_MODEL_DIR}/vosk-model-fa-0.42"
    --expect "امروز هوا چطور است"
)
set_tests_properties(voice-probe-wav PROPERTIES TIMEOUT 90)
set_tests_properties(
  voice-core-goldens
  voice-probe-wav
  PROPERTIES RESOURCE_LOCK dio_voice_inference_models
)

add_library(dio_app_support STATIC
  src/app/announcement.c
  src/app/app_support.c
  src/app/sentence_buffer.c
  src/app/vault.c
)
dio_configure_c17(dio_app_support)
target_include_directories(dio_app_support PUBLIC src/app)
target_link_libraries(dio_app_support PUBLIC dio_yyjson)
target_link_libraries(dio_app_support PRIVATE
  bcrypt
  ole32
  pathcch
  shell32
  wer
)

add_executable(dio_sentence_buffer_tests
  tests/test_sentence_buffer.c
)
dio_configure_c17(dio_sentence_buffer_tests)
target_link_libraries(dio_sentence_buffer_tests PRIVATE dio_app_support)
add_test(NAME sentence-buffer COMMAND "$<TARGET_FILE:dio_sentence_buffer_tests>")

add_executable(dio_announcement_tests
  tests/test_announcement.c
)
dio_configure_c17(dio_announcement_tests)
target_link_libraries(dio_announcement_tests PRIVATE dio_app_support)
add_test(NAME announcement-inbox COMMAND "$<TARGET_FILE:dio_announcement_tests>")

add_executable(dio_app_support_tests
  tests/test_app_support.c
)
dio_configure_c17(dio_app_support_tests)
target_link_libraries(dio_app_support_tests PRIVATE dio_app_support)
add_test(NAME app-support COMMAND "$<TARGET_FILE:dio_app_support_tests>")

add_executable(dio_vault_tests
  tests/test_vault.c
)
dio_configure_c17(dio_vault_tests)
target_link_libraries(dio_vault_tests PRIVATE dio_app_support)
add_test(NAME portable-vault COMMAND "$<TARGET_FILE:dio_vault_tests>")
set_tests_properties(portable-vault PROPERTIES TIMEOUT 30)

add_library(dio_app_ui STATIC
  src/app/settings_view.c
  src/app/ui.c
  src/app/views.c
)
dio_configure_c17(dio_app_ui)
target_include_directories(dio_app_ui PUBLIC src/app)
target_link_libraries(dio_app_ui PUBLIC
  dio_app_support
  CUI::cui
  CUI::icons_lucide
)
target_link_libraries(dio_app_ui PRIVATE
  comctl32
  dwmapi
  oleacc
  ole32
  shell32
  uiautomationcore
  uxtheme
)

add_executable(dio_tool_approval_tests tests/test_tool_approval.c)
dio_configure_c17(dio_tool_approval_tests)
target_include_directories(dio_tool_approval_tests PRIVATE src/app)
add_test(
  NAME ui-tool-approval
  COMMAND "$<TARGET_FILE:dio_tool_approval_tests>"
)

add_executable(dio_voice_app WIN32
  src/app/dio_voice.rc
  src/app/main.c
)
file(GLOB DIO_TRAY_ICON_ASSETS CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/app.ico"
  "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/tray-*.ico"
)
set_property(SOURCE src/app/dio_voice.rc APPEND PROPERTY
  OBJECT_DEPENDS
    "${DIO_TRAY_ICON_ASSETS}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/app/dio_voice.manifest"
)
set_target_properties(dio_voice_app PROPERTIES OUTPUT_NAME "dio-voice")
dio_configure_c17(dio_voice_app)
target_include_directories(dio_voice_app PRIVATE
  include
  src/app
  src/core
)
target_link_libraries(dio_voice_app PRIVATE
  dio_agent
  dio_app_ui
  dio_voice_core
  ole32
  shell32
  wer
)
target_link_options(dio_voice_app PRIVATE /MANIFEST:NO)
add_custom_command(
  TARGET dio_voice_app
  POST_BUILD
  COMMAND
    ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts/Vazirmatn-Variable.ttf"
    "$<TARGET_FILE_DIR:dio_voice_app>/Vazirmatn-Variable.ttf"
)
add_test(
  NAME app-recovery
  COMMAND "$<TARGET_FILE:dio_voice_app>" --recovery-self-test
)
set(DIO_MT_TOOL "${CMAKE_MT}")
if(NOT EXISTS "${DIO_MT_TOOL}")
  cmake_host_system_information(
    RESULT DIO_WINDOWS_KITS_ROOT
    QUERY WINDOWS_REGISTRY
      "HKLM/SOFTWARE/Microsoft/Windows Kits/Installed Roots"
    VALUE KitsRoot10
    VIEW BOTH
  )
  file(GLOB DIO_MT_CANDIDATES
    LIST_DIRECTORIES FALSE
    "${DIO_WINDOWS_KITS_ROOT}/bin/*/x64/mt.exe"
  )
  if(DIO_MT_CANDIDATES)
    list(SORT DIO_MT_CANDIDATES COMPARE NATURAL ORDER DESCENDING)
    list(GET DIO_MT_CANDIDATES 0 DIO_MT_TOOL)
  endif()
endif()
add_test(
  NAME app-manifest
  COMMAND
    "${CMAKE_COMMAND}"
    "-DMT_TOOL=${DIO_MT_TOOL}"
    "-DAPP_EXE=$<TARGET_FILE:dio_voice_app>"
    "-DOUTPUT_XML=${CMAKE_CURRENT_BINARY_DIR}/dio-voice-app-manifest.xml"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_app_manifest.cmake"
)
add_test(
  NAME app-icon-assets
  COMMAND powershell.exe
    -NoProfile
    -ExecutionPolicy Bypass
    -File "${CMAKE_CURRENT_SOURCE_DIR}/assets/icons/generate-tray-icons.ps1"
    -VerifyOnly
)
add_test(
  NAME app-ui-fa
  COMMAND "$<TARGET_FILE:dio_voice_app>" --ui-smoke-fa
)
add_test(
  NAME app-ui-en
  COMMAND "$<TARGET_FILE:dio_voice_app>" --ui-smoke-en
)
add_test(
  NAME app-settings-fa
  COMMAND "$<TARGET_FILE:dio_voice_app>" --settings-smoke-fa
)
add_test(
  NAME app-settings-en
  COMMAND "$<TARGET_FILE:dio_voice_app>" --settings-smoke-en
)
set_tests_properties(
  app-ui-fa
  app-ui-en
  app-settings-fa
  app-settings-en
  PROPERTIES
    RUN_SERIAL TRUE
    TIMEOUT 30
)
