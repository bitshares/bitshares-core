# Network Protocol 2

Building a low-latency network requires P2P nodes that have low-latency 
connections and a protocol designed to minimize latency. for the purpose
of this document we will assume that two nodes are located on opposite
sides of the globe with a ping time of 250ms.   


## Announce, Request, Send Protocol 
Under the prior network archtiecture, transactions and blocks were broadcast
in a manner similar to the Bitcoin protocol: inventory messages notify peers of
transactions and blocks, then peers fetch the transaction or block from one
peer.  After validating the item a node will broadcast an inventory message to
its peers.

Under this model it will take 0.75 seconds for a peer to communicate a transaction
or block to another peer even if their size was 0 and there was no processing overhead. 
This level of performance is unacceptable for a network attempting to produce one block
every second. 

This prior protocol also sent every transaction twice: initial broadcast, and again as
part of a block.  


## Push Protocol 
To minimize latency each node needs to immediately broadcast the data it receives 
to its peers after validating it.   Given the average transaction size is less than
100 bytes, it is almost as effecient to send the transaction as it is to send
the notice (assuming a 20 byte transaction id)

Each node implements the following protocol:


    onReceiveTransaction( from_peer, transaction )
        if( isKnown( transaction.id() ) ) 
            return

        markKnown( transaction.id() )

        if( !validate( transaction ) ) 
           return

        for( peer : peers )
          if( peer != from_peer )
             send( peer, transaction )


    onReceiveBlock( from_peer, block_summary )
        if( isKnown( block_summary ) 
            return

        full_block = reconstructFullBlcok( from_peer, block_summary )
        if( !full_block ) disconnect from_peer 

        markKnown( block_summary )

        if( !pushBlock( full_block ) ) disconnect from_peer 

        for( peer : peers )
           if( peer != from_peer )
             send( peer, block_summary )
             

     onHello( new_peer, new_peer_head_block_num )

        replyHello( new_peer ) // ack the hello message with our timestamp to measure latency

        if( peers.size() >= max_peers )
           send( new_peer, peers )
           disconnect( new_peer )
           return
          
        while( new_peer_head_block_num < our_head_block_num )
           sendFullBlock( new_peer, ++new_peer_head_block_num )

        new_peer.synced = true
        for( peer : peers )
            send( peer, new_peer )

     onHelloReply( from_peer, hello_reply )
         update_latency_measure, disconnect if too slow
    
     onReceivePeers( from_peer, peers )
        addToPotentialPeers( peers )

     onUpdateConnectionsTimer
        if( peers.size() < desired_peers )
          connect( random_potential_peer )

     onFullBlock( from_peer, full_block )
        if( !pushBlock( full_block ) ) disconnect from_peer 

     onStartup
        init_potential_peers from config
        start onUpdateConnectionsTimer
     
