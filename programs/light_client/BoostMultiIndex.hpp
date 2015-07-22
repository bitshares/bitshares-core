#pragma once

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/ordered_index.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>

using boost::multi_index_container;
using boost::multi_index::indexed_by;
using boost::multi_index::hashed_unique;
using boost::multi_index::tag;
using boost::multi_index::const_mem_fun;
using boost::multi_index::ordered_unique;
