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
#pragma once

namespace graphene { namespace chain {


   /**
    *  @brief provides for persistant storage of arbitrary data 
    *
    *  Smart contracts need data to be stored persistantly that can be shared with
    *  other smart contracts.  There is a cost associated with storing data, especially if
    *  that data will be kept in RAM.  
    *
    *  File objects allow smart contracts to interact with persistant storage much like
    *  traditional programs interact with files on disk.  The cost of accessing a file
    *  object to modify it is much higher than the cost to simply read it because the
    *  the database must make a backup of the file for the undo history in the event
    *  of a blockchain reorganization or failure in evaluation.   For this reason files
    *  are limited to 2^16 bytes and smart contracts will have to use multiple files if
    *  they need to store additional data.
    *
    *  Every file has an automatic expiration date at which point in time it will be
    *  deleted unless a fee is paid to extend its life time.  
    *
    *  The contents of all files are public, but not to scripts.  A smart contract attempting
    *  to access the contents of a file must have permission to read the file.  The purpose
    *  of this restriction is to help users monetize the trust associated with publishing
    *  data.   Anyone could re-publish the data under a new file, but the trust in the
    *  quality of the data would not be the same as the original file.  
    */
   class file_object  : public graphene::db::annotated_object<file_object>
   {
      public:
         static const uint8_t space_id = protocol_ids;
         static const uint8_t type_id  = file_object_type;

         /**
          *  @brief sets bits that control the permissions granted to smart contracts regarding this data.
          */
         enum permision_flags
         {
            owner_read  = 0x01,
            owner_write = 0x02,
            group_read  = 0x04,
            group_write = 0x08,
            all_read    = 0x10,
            execute     = 0x20  ///< set if data contains virtual machine instructions
         };

         /**
          * The owner can access this file based upon the @ref permissions flags
          *
          * @note - if the owner removes write permission from himself then the file
          * will be imutable thereafter.
          */
         account_id_type owner;
         /** any account that has been white listed by this group can read/write 
          * @ref data based upon the @ref permissions flags.
          */
         account_id_type group;
         /**
          * Bits set according to @ref permission_flags
          */
         uint8_t         permissions = owner_read | owner_write | all_read;

         /**
          *  Files consume memory and thus are cleaned up unless a fee is paid to
          *  keep them alive.  
          */
         time_point_sec  expiration;

         /**
          *  The maximum data size for a file is 2^16 bytes so that the
          *  undo history doesn't have to backup larger files.  If a smart contract
          *  requires more data then it can create more file objects.
          */
         vector<char>    data;
   };

   struct by_expiration;
   struct by_owner;
   struct by_group;
   /**
    * @ingroup object_index
    */
   typedef multi_index_container<
      file_object,
      indexed_by<
         hashed_unique< tag<by_id>, member< object, object_id_type, &object::id > >,
         hashed_non_unique< tag<by_owner>, member< file_object, account_id_type, &file_object::owner > >,
         hashed_non_unique< tag<by_group>, member< file_object, account_id_type, &file_object::group > >,
         ordered_non_unique< tag<by_expiration>, member<file_object, time_point_sec, &file_object::expiration> >
      >
   > file_object_multi_index_type;

   typedef generic_index<file_object, file_object_multi_index_type> file_object_index;

} }

FC_REFLECT_ENUM( graphene::chain::file_object::permision_flags, (owner_read)(owner_write)(group_read)(group_write)(all_read)(execute) )
FC_REFLECT_DERIVED( graphene::chain::file_object, (graphene::db::object), (owner)(group)(permissions)(expiration)(data) )
