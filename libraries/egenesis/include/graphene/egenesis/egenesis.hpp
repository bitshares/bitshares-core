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

#include <string>

#include <fc/crypto/sha256.hpp>
#include <graphene/chain/protocol/types.hpp>
#include <graphene/chain/genesis_state.hpp>

namespace graphene { namespace egenesis {

/**
 * Get the chain ID of the built-in egenesis, or chain_id_type()
 * if none was compiled in.
 */
graphene::chain::chain_id_type get_egenesis_chain_id();

/**
 * Get the egenesis JSON, or the empty string if none was compiled in.
 */
void compute_egenesis_json( std::string& result );

/**
 * The file returned by compute_egenesis_json() should have this hash.
 */
fc::sha256 get_egenesis_json_hash();

} } // graphene::egenesis
