
all:test

main.o:main.cpp
	g++ -c -g  -o $@ -I ../../build/install/include $<


test:main.o
	g++ -o $@ -g   $< -L../../build/install/lib -lcybex_wallet -lgraphene_wallet -lgraphene_egenesis_none -lgraphene_chain -lgraphene_db -lgraphene_app -lgraphene_utilities -lgraphene_market_history -lgraphene_grouped_orders -lfc -lboost_program_options -lboost_date_time -lboost_context -lboost_coroutine -lboost_chrono -lboost_iostreams  -lboost_exception  -lboost_system -lboost_filesystem -lboost_thread  -L../../build/install/lib/cryptonomex -lsecp256k1 -lssl -lcrypto -lrt  -lstdc++ -pthread -lz  

clean:
	rm -rf main.o test

