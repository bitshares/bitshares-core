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

#include <graphene/utilities/recurring_task.hpp>

#include <fc/exception/exception.hpp>

namespace graphene { namespace utilities {

recurring_task::recurring_task( const std::string& name )
{
   fc::set_fiber_name( name );
}

recurring_task::recurring_task( std::thread::id runner, const std::string& name ) : _runner( runner )
{
   fc::set_fiber_name( name );
}

recurring_task::~recurring_task()
{
   if( _worker.valid() && _worker.wait_for( std::chrono::seconds(0) ) != boost::fibers::future_status::ready )
   {
      cancel();
      try
      {
         wait();
      }
      catch( const fc::canceled_exception ) {}
   }
}

void recurring_task::_sleep( std::chrono::microseconds how_long )
{
   static const auto cycle = std::chrono::microseconds(2000000);
   static const auto zero = std::chrono::microseconds(0);
   std::unique_lock<boost::fibers::mutex> lock(_mtx);
   if( !_triggered )
   {
      do
      {
         if( how_long > cycle )
         {
            _cv.wait_for( lock, cycle );
            how_long -= cycle; // FIXME: total can be longer than originally desired
         }
         else
         {
            _cv.wait_for( lock, how_long );
            how_long = zero;
         }
         check_cancelled();
      }
      while( how_long > zero && !_triggered );
   }
   _triggered = false;
   check_cancelled();
}

void recurring_task::check_cancelled()
{
   if( _cancelled )
      FC_THROW_EXCEPTION( fc::canceled_exception, "Task '${n}' was cancelled!", ("n",fc::get_fiber_name()) );
}

void recurring_task::trigger()
{
   std::unique_lock<boost::fibers::mutex> lock(_mtx);
   check_cancelled();
   if( !_worker.valid() || _worker.wait_for( std::chrono::seconds(0) ) == boost::fibers::future_status::ready )
   {
      _worker = _runner != std::thread::id() ? fc::async( std::bind( &recurring_task::run, this ), _runner )
                                             : fc::async( std::bind( &recurring_task::run, this ) );
   }
   else
   {
      _triggered = true;
      _cv.notify_all();
   }
}

void recurring_task::cancel()
{
   std::unique_lock<boost::fibers::mutex> lock(_mtx);
   _cancelled = true;
   _cv.notify_all();
}

void recurring_task::wait()
{
   std::unique_lock<boost::fibers::mutex> lock(_mtx);
   if( !_worker.valid() )
      check_cancelled();
   else
   {
      lock.unlock();
      _worker.wait();
   }
};

} } // end namespace graphene::utilities
