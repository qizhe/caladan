#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <fstream>
using std::fstream;

#include "net.h"
#include "runtime.h"
#include "sync.h"
#include "thread.h"

void compute(int id, int n) {
    auto start = std::chrono::steady_clock::now();
    unsigned long long counter = 0;
    std::ofstream file("./temp/compute_" + std::to_string(id) + "-" + std::to_string(n) + ".log", fstream::out);
    while (1) { 
        counter += 1; 
        if (counter % 100000000ull == 0) {
            auto now = std::chrono::steady_clock::now();
            if (now - start > std::chrono::seconds(1)) {
                file << counter * 1000000000ull / std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count()
                          << " increments/s" 
                          << std::endl;
                file.flush();
                counter = 0;
                start = now;
            }
        }
    }
    file.close();
}

void RunClient(int threads) {
    std::vector<rt::Thread> ths;

    for (int  i = 0; i < threads; i++) {
        ths.emplace_back(rt::Thread([i, threads] {
            compute(i, threads);
        }));   
    } 
    for (auto &t : ths) t.Join();
}

int main (int argc, char** argv) {
    int threads;
    if (argc < 3) {
    std::cerr << "usage: [cfg_file] [threads] ..." << std::endl;
    return -EINVAL;
    }
    std::cout << "reach here" << std::endl;

    threads = std::stoi(argv[2], nullptr, 0);

//   std::string cmd = argv[2];
//   netaddr raddr = {};
//   int threads = 0, samples = 0;
//   size_t buflen = 0;
//   if (cmd.compare("tcpstream") == 0 || cmd.compare("tcprr") == 0) {
//     if (argc != 7) {
//       std::cerr << "usage: [cfg_file] " << cmd << " [ip_addr] [threads] "
//                 << "[samples] [buflen]" << std::endl;
//       return -EINVAL;
//     }
//     threads = std::stoi(argv[4], nullptr, 0);
//     samples = std::stoi(argv[5], nullptr, 0);
//     buflen = std::stoul(argv[6], nullptr, 0);
//   } else if (cmd.compare("server") != 0) {
//     std::cerr << "invalid command: " << cmd << std::endl;
//     return -EINVAL;
//   }
  return rt::RuntimeInit(argv[1], [=]() {
      RunClient(threads);
  });
}


