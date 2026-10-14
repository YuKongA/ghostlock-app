# 由 CTest 调用：把 JNI 边界协议表导出到 build/native/jni-protocol-manifest.tsv，并核对三张表的行数。
#
# 为什么用 CMake 脚本而不是 sh -c：命令行里的引号/花括号在 CMake→sh→工具三层之间太容易写错；
# execute_process 只有一层，且失败就是 FATAL_ERROR（CTest 记为 Failed）。
# 参数：-DTOOL=<jni_protocol_manifest 路径>  -DOUT=<导出的 tsv 路径>
if(NOT DEFINED TOOL OR NOT DEFINED OUT)
  message(FATAL_ERROR "用法：cmake -DTOOL=<manifest 工具> -DOUT=<tsv 路径> -P jni_protocol_manifest.cmake")
endif()
execute_process(COMMAND "${TOOL}" OUTPUT_VARIABLE manifest RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
  message(FATAL_ERROR "jni_protocol_manifest 退出码 ${rc}")
endif()
get_filename_component(out_dir "${OUT}" DIRECTORY)
file(MAKE_DIRECTORY "${out_dir}")
file(WRITE "${OUT}" "${manifest}")
string(REGEX MATCHALL "\nop\t" op_rows "${manifest}")
string(REGEX MATCHALL "\nkey\t" key_rows "${manifest}")
string(REGEX MATCHALL "\nstatus\t" status_rows "${manifest}")
list(LENGTH op_rows op_count)
list(LENGTH key_rows key_count)
list(LENGTH status_rows status_count)
message(STATUS "jni protocol manifest: ops=${op_count} keys=${key_count} statuses=${status_count}")
if(NOT op_count EQUAL 15 OR NOT key_count EQUAL 34 OR NOT status_count EQUAL 18)
  message(FATAL_ERROR "行数与表不符：ops=${op_count}(15) keys=${key_count}(34) statuses=${status_count}(18)")
endif()
