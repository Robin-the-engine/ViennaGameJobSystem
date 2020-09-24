#include <iostream>
#include <iomanip>
#include <stdlib.h>
#include <functional>
#include <string>
#include <algorithm>
#include <chrono>

#include "VEGameJobSystem.h"
#include "VECoro.h"


using namespace std::chrono;


namespace mixed {

    using namespace vgjs;

    auto g_global_mem5 = std::pmr::synchronized_pool_resource({ .max_blocks_per_chunk = 20, .largest_required_pool_block = 1 << 20 }, std::pmr::new_delete_resource());


    Coro<int> compute(int i) {

        //co_await 1;

        //std::cout << "Compute " << i << std::endl;

        //std::this_thread::sleep_for(std::chrono::microseconds(1));

        co_return 2 * i;
    }

    void printData(int i, int id);

    Coro<int> printDataCoro(int i, int id) {
        //std::cout << "Print Data Coro " << i << std::endl;
        if (i >0 ) {
            co_await compute(i);
            co_await FUNCTION( printData( i - 1, i+1 ) );
            //co_await printDataCoro(i - 1, i + 1);
            //co_await printDataCoro(i - 1, i + 1);
        }
        co_return i;
    }

    void printData(int i, int id ) {
        //std::cout << "Print Data " << i << std::endl;
        if (i > 0) {
            auto f1 = printDataCoro(i, -(i - 1))(-1, 2,1);
            //auto f2 = printDataCoro(i, i + 1 )(-1, 2, 1);

            schedule( f1 );
            //schedule( f2 );

            //schedule(F(printData(i-1, 0)));
        }
    }


    void loop( int N) {

        for (int i = 0; i < N; ++i) {
            //std::cout << "Loop " << i << std::endl;

            auto f = printDataCoro(i,10);
            schedule( f );
            //std::this_thread::sleep_for(std::chrono::microseconds(1));
        }

    }

    void driver(int i, std::string id) {
        //std::cout << "Driver " << i << std::endl;
        if (i == 0) {
            return;
        }

        //schedule( Function( F( printData(i, -1) ), -1, 1, 0)  );


        //schedule( F(printData(i, -1)));

        schedule(FUNCTION(loop(i)));

        //continuation( Function( F( vgjs::terminate() ), -1, 3, 0 ) );
    }


    void test() {
        std::cout << "Starting mixed test()\n";

        auto& types = JobSystem::instance()->types();
        types[0] = "Driver";
        types[1] = "printData";
        types[2] = "printDataCoro";
        types[3] = "terminate";

        //JobSystem::instance()->enable_logging();

        //schedule( Function( FUNCTION(driver( 100 , "Driver")), -1, 0, 0 ) );
        schedule( F( driver(50, "Driver") ) );

        std::cout << "Ending mixed test()\n";

    }

}


