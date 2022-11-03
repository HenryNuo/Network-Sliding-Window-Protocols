# Makefile
#
# This makefile features two parts:
#	sender <-- What starts the process and handles transferring the file to the receiver
#		make sender
#		./sender
#	receiver <-- What receives the file from the sender.
#		make receiver
#		./receiver <listen port>

# Sender / Client
sender: sender.o Packet.o NetSockets.o
	g++ -std=c++11 -lpthread sender.o Packet.o NetSockets.o -o sender

sender.o: sender.cpp Packet.h NetSockets.h
	g++ -std=c++11 -lpthread -c sender.cpp -o sender.o

# Receiver / Server
receiver: receiver.o Packet.o NetSockets.o
	g++ -std=c++11 receiver.o Packet.o NetSockets.o -o receiver

receiver.o: receiver.cpp Packet.h NetSockets.h
	g++ -std=c++11 -c receiver.cpp -o receiver.o

# Additional Libraries
Packet.o: Packet.cpp Packet.h
	g++ -std=c++11 -c Packet.cpp -o Packet.o

NetSockets.o: NetSockets.cpp NetSockets.h
	g++ -std=c++11 -c NetSockets.cpp -o NetSockets.o

clean:
	rm out-*
	rm *.o