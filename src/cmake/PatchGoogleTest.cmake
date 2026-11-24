# GoogleTest 1.14 prints UTF-8 code units numerically rather than decoding them.
# Make the widening cast explicit to satisfy LLVM 22's -Wcharacter-conversion
# while retaining GoogleTest's warnings-as-errors policy.
set(printers_header "${GOOGLETEST_SOURCE_DIR}/googletest/include/gtest/gtest-printers.h")
file(READ "${printers_header}" printers)
set(original "inline void PrintTo(char8_t c, ::std::ostream* os) {
  PrintTo(ImplicitCast_<char32_t>(c), os);
}")
set(patched "inline void PrintTo(char8_t c, ::std::ostream* os) {
  PrintTo(static_cast<char32_t>(c), os);
}")
string(FIND "${printers}" "${original}" original_pos)
if(NOT original_pos EQUAL -1)
  string(REPLACE "${original}" "${patched}" printers "${printers}")
  file(WRITE "${printers_header}" "${printers}")
else()
  string(FIND "${printers}" "${patched}" patched_pos)
  if(patched_pos EQUAL -1)
    message(FATAL_ERROR "GoogleTest char8_t printer changed; review the LLVM 22 patch")
  endif()
endif()
