/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *
 * 1. Any modified source or binaries are used only with the BitShares network.
 *
 * 2. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 3. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#pragma once

#define HARDFORK_357_TIME (fc::time_point_sec( 1444416300 ))
#define HARDFORK_359_TIME (fc::time_point_sec( 1444416300 ))
#define HARDFORK_385_TIME (fc::time_point_sec( 1445558400 )) // October 23 enforce PARENT.CHILD and allow short names
#define HARDFORK_393_TIME (fc::time_point_sec( 2445558400 )) // Refund order creation fee on cancel
#define HARDFORK_409_TIME (fc::time_point_sec( 1446652800 ))
#define HARDFORK_413_TIME (fc::time_point_sec( 1446652800 ))
#define HARDFORK_415_TIME (fc::time_point_sec( 1446652800 ))
#define HARDFORK_416_TIME (fc::time_point_sec( 1446652800 ))
#define HARDFORK_419_TIME (fc::time_point_sec( 1446652800 ))

// #436 Prevent margin call from being triggered unless feed < call price
#define HARDFORK_436_TIME (fc::time_point_sec( 1450288800 ))

// #445 Refund create order fees on cancel
#define HARDFORK_445_TIME (fc::time_point_sec( 1450288800 ))

// #453 Hardfork to retroactively correct referral percentages
#define HARDFORK_453_TIME (fc::time_point_sec( 1450288800 ))

// #480 Fix non-CORE MIA core_exchange_rate check
#define HARDFORK_480_TIME (fc::time_point_sec( 1450378800 ))

// #483 Operation history numbering change
#define HARDFORK_483_TIME (fc::time_point_sec( 1450378800 ))
