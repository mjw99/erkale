function(run_test target)
  # Run ERKALE to get checkpoint file
  add_test(NAME ${target}_chk COMMAND erkale ${CMAKE_CURRENT_SOURCE_DIR}/${target}.run)# &> ${target}.stdout)
  set_property(TEST ${target}_chk APPEND PROPERTY ENVIRONMENT "ERKALE_SYSDIR=${CMAKE_CURRENT_SOURCE_DIR}/xyz")
  set_property(TEST ${target}_chk APPEND PROPERTY ENVIRONMENT "ERKALE_LIBRARY=${CMAKE_SOURCE_DIR}/basis")
  set_property(TEST ${target}_chk APPEND PROPERTY DEPENDS erkale)
  set_property(TEST ${target}_chk APPEND PROPERTY DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${target}.run)
  
  # Run comparison against checkpoint file
  add_test(NAME ${target} COMMAND chkcompare ${target}.chk)
  set_property(TEST ${target} APPEND PROPERTY DEPENDS ${target}_chk)
  set_property(TEST ${target} APPEND PROPERTY ENVIRONMENT "ERKALE_REFDIR=${CMAKE_SOURCE_DIR}/refdata")
  set_property(TEST ${target} APPEND PROPERTY DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${target}.chk)
endfunction()

function(run_xrs target dep)
  # Run ERKALE to get checkpoint file
  add_test(NAME ${target}_chk COMMAND erkale_xrs ${CMAKE_CURRENT_SOURCE_DIR}/${target}.run)# &> ${target}.stdout)
  set_property(TEST ${target}_chk APPEND PROPERTY ENVIRONMENT "ERKALE_SYSDIR=${CMAKE_CURRENT_SOURCE_DIR}/xyz")
  set_property(TEST ${target}_chk APPEND PROPERTY ENVIRONMENT "ERKALE_LIBRARY=${CMAKE_SOURCE_DIR}/basis")
  set_property(TEST ${target}_chk APPEND PROPERTY DEPENDS erkale_xrs)
  set_property(TEST ${target}_chk APPEND PROPERTY DEPENDS ${dep})
  set_property(TEST ${target}_chk APPEND PROPERTY DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${target}.run)
  
  # Run comparison against checkpoint file
  add_test(NAME ${target} COMMAND chkcompare ${target}.chk)
  set_property(TEST ${target} APPEND PROPERTY DEPENDS ${target}_chk)
  set_property(TEST ${target} APPEND PROPERTY ENVIRONMENT "ERKALE_REFDIR=${CMAKE_SOURCE_DIR}/refdata")
  set_property(TEST ${target} APPEND PROPERTY DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${target}.chk)
endfunction()

# First, compile and run the basic test
add_test(build_basictests  "${CMAKE_COMMAND}" --build ${CMAKE_BINARY_DIR} --target basictests)
add_test(NAME basictests COMMAND basictests)

# To run any tests we need to check that ERKALE and the test comparison binary have been built
add_test(build_erkale     "${CMAKE_COMMAND}" --build ${CMAKE_BINARY_DIR} --target erkale)
add_test(build_chkcompare "${CMAKE_COMMAND}" --build ${CMAKE_BINARY_DIR} --target chkcompare)

# Generate list with genlist.sh
include(TestList.txt)
