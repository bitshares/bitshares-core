/*
 * Copyright (c) 2015, Cryptonomex, Inc.
 * All rights reserved.
 *
 * This source code is provided for evaluation in private test networks only, until September 8, 2015. After this date, this license expires and
 * the code may not be used, modified or distributed for any purpose. Redistribution and use in source and binary forms, with or without modification,
 * are permitted until September 8, 2015, provided that the following conditions are met:
 *
 * 1. The code and/or derivative works are used only for private test networks consisting of no more than 10 P2P nodes.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <graphene/time/time.hpp>

#include <fc/exception/exception.hpp>
#include <fc/network/ntp.hpp>
#include <fc/thread/mutex.hpp>
#include <fc/thread/scoped_lock.hpp>

#include <atomic>

namespace graphene { namespace time {

static int32_t simulated_time    = 0;
static int32_t adjusted_time_sec = 0;

time_discontinuity_signal_type time_discontinuity_signal;

namespace detail
{
  std::atomic<fc::ntp*> ntp_service(nullptr);
  fc::mutex ntp_service_initialization_mutex;
}

fc::optional<fc::time_point> ntp_time()
{
  fc::ntp* actual_ntp_service = detail::ntp_service.load();
  if (!actual_ntp_service)
  {
    fc::scoped_lock<fc::mutex> lock(detail::ntp_service_initialization_mutex);
    actual_ntp_service = detail::ntp_service.load();
    if (!actual_ntp_service)
    {
      actual_ntp_service = new fc::ntp;
      detail::ntp_service.store(actual_ntp_service);
    }
  }
  return actual_ntp_service->get_time();
}

void shutdown_ntp_time()
{
  fc::ntp* actual_ntp_service = detail::ntp_service.exchange(nullptr);
  delete actual_ntp_service;
}

fc::time_point now()
{
   if( simulated_time )
       return fc::time_point() + fc::seconds( simulated_time + adjusted_time_sec );

   fc::optional<fc::time_point> current_ntp_time = ntp_time();
   if( current_ntp_time.valid() )
      return *current_ntp_time + fc::seconds( adjusted_time_sec );
   else
      return fc::time_point::now() + fc::seconds( adjusted_time_sec );
}

fc::time_point nonblocking_now()
{
  if (simulated_time)
    return fc::time_point() + fc::seconds(simulated_time + adjusted_time_sec);

  fc::ntp* actual_ntp_service = detail::ntp_service.load();
  fc::optional<fc::time_point> current_ntp_time;
  if (actual_ntp_service)
    current_ntp_time = actual_ntp_service->get_time();

  if (current_ntp_time)
    return *current_ntp_time + fc::seconds(adjusted_time_sec);
  else
    return fc::time_point::now() + fc::seconds(adjusted_time_sec);
}

void update_ntp_time()
{
  detail::ntp_service.load()->request_now();
}

fc::microseconds ntp_error()
{
  fc::optional<fc::time_point> current_ntp_time = ntp_time();
  FC_ASSERT( current_ntp_time, "We don't have NTP time!" );
  return *current_ntp_time - fc::time_point::now();
}

void start_simulated_time( const fc::time_point sim_time )
{
   simulated_time = sim_time.sec_since_epoch();
   adjusted_time_sec = 0;
}
void advance_simulated_time_to( const fc::time_point sim_time )
{
   simulated_time = sim_time.sec_since_epoch();
   adjusted_time_sec = 0;
}

void advance_time( int32_t delta_seconds )
{
   adjusted_time_sec += delta_seconds;
   time_discontinuity_signal();
}

} } // graphene::time
