/*
 * Copyright (c) 2015 Cryptonomex, Inc., and contributors.  All rights reserved.
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
#include <graphene/db/object.hpp>

namespace graphene { namespace chain {

   /**
    * @brief Contains per-node database configuration.
    *
    *  Transactions are evaluated differently based on per-node state.
    *  Settings here may change based on whether the node is syncing or up-to-date.
    *  Or whether the node is a witness node. Or if we're processing a
    *  transaction in a witness-signed block vs. a fresh transaction
    *  from the p2p network.  Or configuration-specified tradeoffs of
    *  performance/hardfork resilience vs. paranoia.
    */
   class node_property_object
   {
      public:
         node_property_object(){}
         ~node_property_object(){}

         uint32_t skip_flags = 0;
   };
} } // graphene::chain
