/**
 * Receiver
 * 
 * This program listens for a connection and receives a file.
 */
#include <memory>
#include <map>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <algorithm>
#include "NetSockets.h"
#include "Packet.h"
using namespace std;
 
// Global Variables
string dataString;
vector <unique_ptr <Packet> > packetBuffer;
string outputFileName;		// Name of file being transferred
ofstream senderFile; 	// Pointer to file being saved
int curPktNum = 0; 		// Starts at 1 due to special initial packet using 0
int lastPktNum = 0;		// Last packet # received - highest (used for sliding)
int numPackets = 0;		// Number of packets to expect
int fileSize = 0;		// File size of file being transferred
int packetSize = 0;		// Data size of our packets	
int slidingWindowSize = 0; // Size of our sliding window
int seqNumRange = 0;
map<int, bool> donePackets;	// List of packets already processed (for duplicate detection)
string protocol = "SR";		// Type of protocol we're using (GBN or SR)


/**
 * @brief Read the initial packet
 * 
 */
void readInitialPacket(vector <char> rawPacketData) {

	// Convert the data to a string
	string packetData = rawPacketData.data();

	// First 16 bytes are filesize
	fileSize = stoi(packetData.substr(0, 16));

	// Second 16 bytes are number of packets
	numPackets = stoi(packetData.substr(16, 16));

	// Third 16 bytes are the packet size
	packetSize = stoi(packetData.substr(32, 16));

	// Fourth 16 bytes are window size
	slidingWindowSize = stoi(packetData.substr(48, 16));

	// Fifth 3 bytes are protocol
	protocol = packetData.substr(64, 3);

	// Sixth 16 bytes are sequence number range
	seqNumRange = stoi(packetData.substr(67, 16));

	// Everything else is file name.
	outputFileName = packetData.substr(83);

	// Open up the file for writing
	senderFile.open(outputFileName, ios::out | ios::binary);

	// TODO: Check for existence
	cout << "File Details: " << outputFileName << " | Size: " << to_string(fileSize) << "\n"
		<< "# Packets: " << to_string(numPackets) << " | Packet Size: " << to_string(packetSize) 
		<< " | Window Size: " << to_string(slidingWindowSize) << " | Protocol: " << protocol << "\n";
}

/**
 * @brief Compare two packets to sort them by sequence number
 */
bool comparePacketSeq(unique_ptr<Packet>& pkt1, unique_ptr<Packet>& pkt2) {
	return (pkt1->getSeqNum() < pkt2->getSeqNum());
}

/**
 * @brief Process the packet buffer
 * 
 * After every packet is received, which precedes this function, we want to go through the
 * 		buffer of stored packets to reconstruct the file. This method works by comparing 
 * 		the packet we *should* be on with the ones stored in the buffer. If we're ready to move
 * 		forward, it'll write to the file and see if we should move onto the next packet in the buffer
 * 
 * All data in the buffer is sorted already after we receive a packet out of order.
 */
void processPacketBuffer() {
	// Loop through the packetBuffer to see if we can move it forward
	vector<unique_ptr<Packet>>::iterator iterator = packetBuffer.begin();
	while (iterator != packetBuffer.end()) {

		// If the sequence number is greater than the packet we are working with, it's not ready yet. Stop and wait for next packet
		if ((*iterator)->getSeqNum() > curPktNum) {
			++iterator;
			break;
		}

		// Add the data
		vector <char> fileData = (*iterator)->getData();
		// dataString += fileData;

		senderFile.write(fileData.data(), fileData.size());
		senderFile.flush(); // Immediately write to the file

		// We can move onto the next packet.
		curPktNum++;

		// Remove the element from the list.
		iterator = packetBuffer.erase(iterator);
	}
}

/**
 * @brief Send back an ACK packet 
 * 
 * This sends a packet back to the client connection.
 * 
 * @param seqNum 		Sequence number of the packet
 * @param validChecksum Checksum status (true = valid, false = invalid)
 */
void sendAckMessage(NetSocket *clientSocket, int seqNum, bool validChecksum) {

	// Build the packet (no data needed, just sequence # and ack state)
	Packet ackPacket = Packet();
	ackPacket.setSeqNum(seqNum);
	ackPacket.setSeqNumRange(seqNumRange);
	ackPacket.setAck((validChecksum) ? Packet::ACK_OK : Packet::ACK_FAIL);
	ackPacket.setData(vector <char> (1));

	// Send the request back
	clientSocket->sendData(ackPacket.createPacketString());
}

/**
 * @brief Show the sliding window on the receiver side
 * 
 * The windows shifts after each ACK.
 * Selective Repeat = Size = N | Go-Back-N = 1
 * Example: [1, 2, 3, 4, 5]
 */
void showSlidingWindow() {

	int slidingWindowFront = curPktNum + 1; // Our sliding number is based on *after* ACK is sent.

    // Create the window display
    int slidingWindowMax = slidingWindowFront + slidingWindowSize - 1;
    string windowDisplay = "Current window = [";
    for (int i = slidingWindowFront; i <= slidingWindowMax; i++) {

        // Display the number (how depends on range)
        if (seqNumRange > 0) {
            windowDisplay.append(to_string(i % seqNumRange));
        } else {
            windowDisplay.append(to_string(i));
        }

        if (i != slidingWindowMax) {
            windowDisplay.append(", ");
        }
    }
    windowDisplay.append("]");

    // Display the full window - we do this at the end to prevent delays in cout.
    printf("%s\n", windowDisplay.c_str());
}

/**
 * @brief Main Function for Receiver
 * 
 * @param argc 
 * @param argv 
 * @return int 
 */
int main(int argc, char *argv[]) {

	// Global Variables
	int portNum;
	int numReceived = 0; // How many packets did we receive?
	int numRetrans = 0; // How many packets re-transmitted?
	int lastReceived = 0; // What was the last seq number received?

	// Make sure user provided a port
	if (argc != 2) {
		cout << "Please provide a port # above 1024 as a parameter.\n";
		cout << "Example: ./receiver 10000\n";
		return 1;
	}

	// Convert the number to int
	istringstream iss(argv[1]);

	// Store the port number
	if (iss >> portNum) {
		// Validate the port number between 1024 and 65535
		if (portNum < 1025 || portNum > 65535) {
			cout << "Pease provide a port # between 1024 and 65535.\n";
			return 1;
		}
	} else {
		cout << "Please provide a valid port. \n";
		return 1;
	}

	// Create the socket and listen
	NetSocket clientSocket;
	clientSocket.createServerSocket(portNum);

	// Keep reading FOR-EV-ER  (until we say stop / socket is closed)
	while (1) {
		string socketData = clientSocket.getFromSocket(packetSize);

		// Length greater than 0? We have data
		if (socketData.length() > 0) {

			// Is this a ping request? We do nothing with it other than sent data back.
			if (socketData.substr(0, 4) == "PING") {
				clientSocket.sendData("PING");
				continue;
			}

			// Increase our packet count.
			numReceived++;

			// Create a new packet from the socket data
			unique_ptr<Packet> dataPacket(new Packet());
			dataPacket->reversePacket(socketData);
			dataPacket->setSeqNumRange(seqNumRange);

			// Track that we received this 'last'
			lastReceived = dataPacket->showSeqNum();

			// Checksum Status
			bool validChecksum = dataPacket->isValidChecksum();

			// Did we already process this packet?
			bool isDuplicate = donePackets.find(dataPacket->getSeqNum()) != donePackets.end();
			if (isDuplicate) {
				cout << "Packet " << dataPacket->showSeqNum() << " received (duplicate)\n";
			} else {
				// Indicate we received the packet
				cout << "Packet " << dataPacket->showSeqNum() << " received\n";
			}

			cout << "Checksum " << (validChecksum ? "OK" : "failed") << "\n";

			// Send acknowledgement
			sendAckMessage(&clientSocket, dataPacket->getSeqNum(), validChecksum);
			cout << "Ack " << dataPacket->showSeqNum() << " sent\n";

			// Show the current sliding window
			showSlidingWindow();

			// If the checksum failed, or it is a duplicate, - no reason to keep the packet.
			if (!validChecksum || isDuplicate) {
				continue;
			}

			// Indicate the packet was processed
			donePackets[dataPacket->getSeqNum()] = 1;

			// Keep track of the last & highest packet we received.
			if (dataPacket->getSeqNum() > lastPktNum) {
				lastPktNum = dataPacket->getSeqNum();
			}

			// Is this the first packet? Then it sets the stage for creating a file
			if (dataPacket->getSeqNum() == 0) {
				readInitialPacket(dataPacket->getData());
				curPktNum++; // Increase the packet number of the next one.
				continue; // Nothing more to do here.
			}

			// If the sequence number is next, add it to the beginning of the list
			if (curPktNum == dataPacket->getSeqNum()) { // Next Seq Num
				packetBuffer.insert(packetBuffer.begin(), move(dataPacket));
			} else {
				// Add the packet to the buffer
				packetBuffer.push_back(move(dataPacket));

				// Sort the buffer for easier processing
				sort(packetBuffer.begin(), packetBuffer.end(), comparePacketSeq);
			}

			// Process the current buffer of stored packets.
			processPacketBuffer();

			// Are we done? Then stop the main socket loop
			if (curPktNum == numPackets) {
				break;

			// Is the last packet next? Identify remaining packet size for socket.
			} else if ((curPktNum+1) == numPackets) {
				int newPacketSize = fileSize % packetSize;
				if (newPacketSize > 0) packetSize = newPacketSize; // Next packet will be different.
			}
		
		// Length of "0"? Then the socket was closed.
		} else if (socketData.length() == 0) {
			cout << "Socket was closed...";
			break;
		}
	}

	// Close our socket and file
	clientSocket.closeSocket();
	senderFile.close();

	// Math about the number of packets retransmitted
	numRetrans = numReceived - numPackets;

	// Statistics!
	printf("\n\n");
	printf("Last packet seq# received: %d\n", lastReceived);
	printf("Number of original packets received: %d\n", numPackets);
	printf("Number of retransmitted packets received: %d\n\n", numRetrans);

	return 0;
}
