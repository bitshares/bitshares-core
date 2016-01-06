/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.
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

#define HARDFORK_357_TIME (fc::time_point_sec( 1444416300 ))
#define HARDFORK_359_TIME (fc::time_point_sec( 1444416300 ))
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
