#!/usr/bin/env python3
import os
import re
import sys

client_config = """host_addr 192.168.2.{}
host_netmask 255.255.255.0
host_gateway 192.168.1.1
runtime_kthreads 1
runtime_priority lc"""

num = int(sys.argv[1])

for i in range(1, num + 1):
    f = "client_ma_{}.config".format(i)
    with open(f, "w") as file:
        file.write(client_config.format(i))
        file.close()


