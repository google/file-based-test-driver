## File Based Test Driver

#### Contents

[TOC]

## Overview

file_based_test_driver
uses `.test` files to specify sets of inputs and expected outputs. The format
of these files is documented in `file_based_test_driver.h`.

A detailed example that shows all features is in
[`example.test`](example.test) and the associated test driver
[`example_test.cc`](example_test.cc).

#### Test output with modes

file_based_test_driver's basic test runner
[RunTestCasesFromFiles](file_based_test_driver.h)
calls a callback to run a test, gets back the actual outputs, and does string
comparison on the actual and expected outputs. In some cases, the test callback
may run a test case multiple times in different modes and each mode may generate
different outputs. Since the basic test runner does not understand test modes,
the test callback must always run the test in all the modes and return the
complete results.

To allow running tests in selected modes, we added a new test runner
`RunTestCasesWithModesFromFiles`
that understands test modes. The format of the test file is very similar to the
one that is used with the basic test runner. Each test still has an input block
followed by multiple output blocks. The only difference is that the output
blocks of a test must be parsable by
[`test_case_outputs.h`](test_case_outputs.h).
The text format of such test output and its internal representation is
documented in
[`test_case_outputs.h`](test_case_outputs.h).
The new test runner will first parse the expected output and group the outputs
by test modes. It then calls the test callback and gets back the actual outputs
for one or more modes, merges them into the expected outputs (only the outputs
for the modes tested are replaced), and compares the merged results with the
expected results.

Example that shows the test mode related features is in
[`example_with_modes.test`](example_with_modes.test)
and
the associated test driver
[`example_test_with_modes.cc`](example_test_with_modes.cc).

## Contributions

Still a work in progress, not accepting contributions yet.

## License

[Apache License 2.0](LICENSE)

## Support Disclaimer
This is not an officially supported Google product.
