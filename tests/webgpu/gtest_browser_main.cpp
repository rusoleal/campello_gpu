// main() for running the GoogleTest GPU suite (tests/platform/) in a browser.
//
// Like webgpu_browser_test.cpp, it ends with the completion line that
// run_in_browser.mjs waits for. GoogleTest itself runs unchanged: blocking
// campello_gpu calls inside tests yield to the browser through ASYNCIFY.
#include <gtest/gtest.h>
#include <cstdio>

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();
    const auto* unitTest = testing::UnitTest::GetInstance();
    printf("CAMPELLO_WEBGPU_TESTS_DONE failures=%d tests=%d skipped=%d\n",
           unitTest->failed_test_count(), unitTest->test_to_run_count(),
           unitTest->skipped_test_count());
    return result;
}
