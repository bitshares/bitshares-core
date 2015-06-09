
How to use fc async to do recurring tasks
-----------------------------------------

    _my_task = fc::async( callable, "My Task" );
    _my_task = fc::schedule( callable, "My Task 2", exec_time );

Stuff to know about the code
----------------------------

`static_variant<t1, t2>` is a *union type* which says "this variable may be either t1 or t2."  It is serializable if t1 and t2 are both serializable.

The file `operations.hpp` documents the available operations, and `database_fixture.hpp` is a good reference for building and submitting transactions for processing.

Tests also show the way to do many things, but are often cluttered with code that generates corner cases to try to break things in every possible way.

Visitors are at the end of `operations.hpp` after the large typedef for `operation` as a `static_variant`.  TODO:  They should be refactored into a separate header.

Downcasting stuff
-----------------

- You have an `object_id_type` and want to downcast it to a `key_id_type` : `key_id_type( object_id )`
- You have an `operation_result` and want to downcast it to an `object_id_type` : `op_result.get<object_id_type>()`
- Since `operation_result` is a `static_variant`, the above is also how you downcast `static_variant`

Debugging FC exceptions with GDB
--------------------------------

- `catch throw`
