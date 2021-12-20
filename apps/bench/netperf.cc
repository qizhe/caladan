extern "C" {
#include <base/log.h>
#include <net/ip.h>
#include <unistd.h>
}

#include <chrono>
#include <iostream>
#include <fstream>
#include <memory>
#include <vector>

#include "net.h"
#include "runtime.h"
#include "sync.h"
#include "thread.h"

using us = std::chrono::duration<double, std::micro>;

namespace {

using namespace std::chrono;
using sec = duration<double>;

// constexpr uint16_t kNetperfPort = 8080;
constexpr uint64_t kNetperfMagic = 0xF00BAD11DEADBEEF;
constexpr size_t kMaxBuffer = 0x10000000;

enum {
  kTCPStream = 0,
  kTCPRR,
};

struct server_init_msg {
  uint64_t magic;
  uint64_t mode;
  size_t buflen;
};

void ServerWorker(std::unique_ptr<rt::TcpConn> c) {
  server_init_msg msg;
  ssize_t ret = c->ReadFull(&msg, sizeof(msg));
  if (ret != static_cast<ssize_t>(sizeof(msg))) {
    if (ret == 0 || ret == -ECONNRESET) return;
    log_err("read failed, ret = %ld", ret);
    return;
  }

  if (msg.magic != kNetperfMagic) {
    log_err("invalid magic %lx", msg.magic);
    return;
  }

  bool write_back;
  switch (msg.mode) {
    case kTCPStream:
      write_back = false;
      break;
    case kTCPRR:
      write_back = true;
      break;
    default:
      log_err("invalid mode %ld", msg.mode);
      return;
  }

  size_t buflen = std::min(msg.buflen, kMaxBuffer);
  std::unique_ptr<char[]> buf(new char[buflen]);
  while (true) {
    ret = c->ReadFull(buf.get(), buflen);
    if (ret != static_cast<ssize_t>(buflen)) {
      if (ret == 0 || ret == -ECONNRESET) break;
      log_err("read failed, ret = %ld", ret);
      break;
    }
    if (write_back) {
      ret = c->WriteFull(buf.get(), buflen);
      if (ret != static_cast<ssize_t>(buflen)) {
        if (ret == -EPIPE || ret == -ECONNRESET) break;
        log_err("write failed, ret = %ld", ret);
        break;
      }
    }
  }
}

void RunServer(int port) {
  std::unique_ptr<rt::TcpQueue> q(
      rt::TcpQueue::Listen({0, port}, 4096));
  if (q == nullptr) panic("couldn't listen for connections");

  while (true) {
    rt::TcpConn *c = q->Accept();
    if (c == nullptr) panic("couldn't accept a connection");
    rt::Thread([=] { ServerWorker(std::unique_ptr<rt::TcpConn>(c)); }).Detach();
  }
}

void ClientWorker(std::unique_ptr<rt::TcpConn> c, int timelen, int id, size_t buflen,
                  bool rr) {
  std::unique_ptr<char[]> buf(new char[buflen]);
  auto start = steady_clock::now();
	auto start_exp = start;
	auto end = start;
	uint64_t sent_bytes = 0;
	std::ofstream lfile, tfile;
	std::vector<double> latency;

	lfile.open("temp/netperf-" + std::to_string(id)+".log");
	tfile.open("temp/netperf-" + std::to_string(id)+"_thpt.log");
  while (1) {
    start = steady_clock::now();
    ssize_t ret = c->WriteFull(buf.get(), buflen);
    if (ret != static_cast<ssize_t>(buflen))
      panic("write failed, ret = %ld", ret);
    if (rr) {
      sent_bytes += ret;
      ret = c->ReadFull(buf.get(), buflen);
      if (ret != static_cast<ssize_t>(buflen))
        panic("read failed, ret = %ld", ret);
    }
    end = steady_clock::now();
    latency.push_back(std::chrono::duration_cast<us>(end - start).count());
    if(std::chrono::duration_cast<sec>(end - start_exp).count() > timelen) {
      break;
    }
  }
  tfile <<   sent_bytes * 8 / timelen  << std::endl;
  for(uint32_t i = 0; i < latency.size(); i++) {
    lfile << "finish time: " << latency[i] << "\n"; 
    // std::cout << "finish time: " << latency[i] << "\n"; 
  }
  tfile.close();
  lfile.close();
}

void RunClient(netaddr raddr, int threads, int timelen, size_t buflen,
               bool rr, int log_style) {
  // setup experiment
  server_init_msg msg = {kNetperfMagic, rr ? kTCPRR : kTCPStream, buflen};
  std::vector<std::unique_ptr<rt::TcpConn>> conns;
  for (int i = 0; i < threads; ++i) {
    std::unique_ptr<rt::TcpConn> outc(rt::TcpConn::Dial({0, 0}, raddr));
    if (unlikely(outc == nullptr)) panic("couldn't connect to raddr.");
    ssize_t ret = outc->WriteFull(&msg, sizeof(msg));
    if (ret != static_cast<ssize_t>(sizeof(msg))) {
      panic("init msg write failed, ret = %ld", ret);
    }
    conns.emplace_back(std::move(outc));
  }

  // |--- start experiment duration timing ---|
  barrier();
  auto start = steady_clock::now();
  barrier();

  // run the experiment
  std::vector<rt::Thread> ths;
  for (int i = 0; i < threads; ++i) {
    	int thread_id = i;
	if(log_style  == 1)
		thread_id = raddr.port - 8080;
	  ths.emplace_back(rt::Thread([&conns, i, timelen, buflen, rr, thread_id] {
      	ClientWorker(std::move(conns[i]), timelen, thread_id, buflen, rr);
    	}));
  }
  for (auto &t : ths) t.Join();

  // |--- end experiment duration timing ---|
  barrier();
  auto finish = steady_clock::now();
  barrier();

  // report results
  // double seconds = duration_cast<sec>(finish - start).count();
  // size_t mbytes = buflen * samples * threads / 1000 / 1000;
  // double mbytes_per_second = static_cast<double>(mbytes) / seconds;
  // std::cout << "transferred " << mbytes_per_second << " MB/s" << std::endl;
}

int StringToAddr(const char *str, uint32_t *addr) {
  uint8_t a, b, c, d;
  if (sscanf(str, "%hhu.%hhu.%hhu.%hhu", &a, &b, &c, &d) != 4) return -EINVAL;
  *addr = MAKE_IP_ADDR(a, b, c, d);
  return 0;
}

}  // anonymous namespace

int main(int argc, char *argv[]) {
  if (argc < 3) {
    std::cerr << "usage: [cfg_file] [command] ..." << std::endl;
    std::cerr << "commands>" << std::endl;
    std::cerr << "\tserver - runs a netperf TCP server" << std::endl;
    std::cerr << "\ttcpstream - runs a streaming TCP client" << std::endl;
    std::cerr << "\ttcprr - runs a request-reply TCP client" << std::endl;
    return -EINVAL;
  }

  std::string cmd = argv[2];
  netaddr raddr = {};
  int threads = 0, samples = 0;
  int port = 8080;
  size_t buflen = 0;
  /* 0: use thread id; 1: use port_id - 8080 */
  int log_style = 0;
  if (cmd.compare("tcpstream") == 0 || cmd.compare("tcprr") == 0) {
    if (argc != 9) {
      std::cerr << "usage: [cfg_file] " << cmd << " [ip_addr] [threads] "
                << "[samples] [buflen]" << std::endl;
      return -EINVAL;
    }
    int ret = StringToAddr(argv[3], &raddr.ip);
    if (ret) return -EINVAL;
    // raddr.port = kNetperfPort;
    threads = std::stoi(argv[4], nullptr, 0);
    samples = std::stoi(argv[5], nullptr, 0);
    buflen = std::stoul(argv[6], nullptr, 0);
    raddr.port = std::stoi(argv[7], nullptr, 0);
    log_style = std::stoi(argv[8], nullptr, 0);
  } else if (cmd.compare("server") != 0) {
    std::cerr << "invalid command: " << cmd << std::endl;
    port = std::stoi(argv[4], nullptr, 0);
    return -EINVAL;
  }
  return rt::RuntimeInit(argv[1], [=]() {
    std::string cmd = argv[2];
    if (cmd.compare("server") == 0) {
      RunServer(port);
    } else if (cmd.compare("tcpstream") == 0) {
      RunClient(raddr, threads, samples, buflen, false, log_style);
    } else if (cmd.compare("tcprr") == 0) {
      RunClient(raddr, threads, samples, buflen, true, log_style);
    }
  });
}
