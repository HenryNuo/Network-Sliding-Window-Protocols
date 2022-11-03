#!/bin/bash
clear
g++ -o bin/socket-client-test socket-client-test.cpp NetSockets.cpp && ./bin/socket-client-test 127.0.0.1 32001
