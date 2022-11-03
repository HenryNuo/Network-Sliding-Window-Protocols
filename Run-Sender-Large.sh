#!/bin/bash
clear
g++ -std=c++11 -o bin/sender sender.cpp NetSockets.cpp Packet.cpp && ./bin/sender < ./inputs/sender-input-large
