#!/bin/bash
clear
g++ -std=c++11 -lpthread -o bin/sender sender.cpp NetSockets.cpp Packet.cpp && ./bin/sender < ./inputs/sender-input
