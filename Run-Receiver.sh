#!/bin/bash
clear
rm out-*
g++ -std=c++11 -o bin/receiver receiver.cpp NetSockets.cpp Packet.cpp && ./bin/receiver 32001
