/*
 * Copyright (c) 2019 BitShares Blockchain Foundation
 *
 * The MIT License
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#pragma once

#include <fc/thread/async.hpp>

#include <chrono>
#include <thread>

namespace graphene { namespace utilities {

/** This class implements a framework for potentially long-running background tasks.
 *  Subclasses must override run() to do the actual work. run() implementations should use the provided
 *  sleep() method for waiting, and should regularly call check_cancelled(). Both sleep() and check_cancelled()
 *  will throw when cancelled.
 */
class recurring_task
{
   std::thread::id _runner;
   bool _cancelled = false;
   bool _triggered = false;
   boost::fibers::mutex _mtx;
   boost::fibers::condition_variable _cv;
   boost::fibers::future<void> _worker;

   /** Waits for the given duration. Waiting can be interrupted by trigger() or cancel().
    *  Throws when cancelled.
    */
   void _sleep( std::chrono::microseconds how_long );
protected:
   /** Must be overridden to perform the actual work.
    */
   virtual void run() {}

   /** Waits for the given duration. Waiting can be interrupted by trigger() or cancel().
    *  Throws when cancelled.
    */
   template< class Rep, class Period >
   void sleep( std::chrono::duration< Rep, Period > how_long )
   {
      _sleep( std::chrono::duration_cast< std::chrono::microseconds >( how_long ) );
   }

   /** Checks if the task has been cancelled, and throws if so.
    */
   void check_cancelled();
public:
   explicit recurring_task( const std::string& name = "" );
   explicit recurring_task( std::thread::id runner, const std::string& name = "" );
   virtual ~recurring_task();

   /** Throws when cancelled.
    * If no active fiber is running, starts a new one. Will wake up a sleeping fiber.
    */
   void trigger();

   /** Cancels the running task. Future calls to trigger() and wait() will throw. */
   void cancel();

   /** Waits for the running task to complete. Throws when cancelled. */
   void wait();
};

} } // end namespace graphene::utilities
