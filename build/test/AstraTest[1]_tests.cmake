add_test( BasicEventHandlerDataTest.Init /home/cman8/astra/astra-sim/build/test/AstraTest [==[--gtest_filter=BasicEventHandlerDataTest.Init]==] --gtest_also_run_disabled_tests)
set_tests_properties( BasicEventHandlerDataTest.Init PROPERTIES WORKING_DIRECTORY /home/cman8/astra/astra-sim/build/test SKIP_REGULAR_EXPRESSION [==[\[  SKIPPED \]]==])
set( AstraTest_TESTS BasicEventHandlerDataTest.Init)
