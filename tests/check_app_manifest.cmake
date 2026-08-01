if(NOT DEFINED MT_TOOL OR NOT EXISTS "${MT_TOOL}")
  message(FATAL_ERROR "mt.exe is unavailable")
endif()
if(NOT DEFINED APP_EXE OR NOT EXISTS "${APP_EXE}")
  message(FATAL_ERROR "DIO Voice application is unavailable")
endif()
if(NOT DEFINED OUTPUT_XML)
  message(FATAL_ERROR "OUTPUT_XML is required")
endif()

execute_process(
  COMMAND "${MT_TOOL}" "-inputresource:${APP_EXE};#1" "-out:${OUTPUT_XML}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error
)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "manifest extraction failed: ${output}${error}")
endif()

file(READ "${OUTPUT_XML}" manifest)
file(REMOVE "${OUTPUT_XML}")
foreach(required
    "Microsoft.Windows.Common-Controls"
    "version=\"6.0.0.0\""
    "requestedExecutionLevel level=\"asInvoker\""
    ">PerMonitorV2</dpiAwareness>")
  string(FIND "${manifest}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "embedded manifest is missing: ${required}")
  endif()
endforeach()
