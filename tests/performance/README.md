HOW TO
======

This small test suite serves to demonstrate two key points about the performance
of our current implementation. The subject was talked about in detail at
BitFest Amsterdam, Sep 22, 2018.

The original description of the 100,000 transactions per second test can be
found at https://bitshares.org/blog/2015/06/08/measuring-performance/ .

Prepare
-------

1. Follow the build instructions in the top-level README file.
2. Instead of running ``make`` you can run ``make performance_test`` to build
   only the test suite.
3. Run ``tests/performance_test -t performance_tests/<testcase>``


100k TX/s
---------

``tests/performance_test -t performance_tests/one_hundred_k_benchmark``

This test will create 200,000 accounts, make two transfers from each account,
then create an asset and issue tokens to each account, for a total of one
million operations.

Different operation types have different execution times, but on fairly modern
off-the-shelf hardware an average of 100,000 transactions per second should be
achieved.

Signature verification
----------------------

``tests/performance_test -t performance_tests/sigcheck_benchmark``

This suite pre-creates 100,000 signatures and then measures how long it takes
to verify them. Results vary depending on CPU type and clockspeed, but should be
somewhere between 5,000 and 20,000 per second.
