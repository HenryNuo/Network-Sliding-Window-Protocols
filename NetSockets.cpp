#include <sys/socket.h>
#include <netinet/in.h>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>
#include <arpa/inet.h>
#include "NetSockets.h"
using namespace std;
/**
 * Network Sockets
 * 
 * This file contains everything to do with sockets including:
 * 		Server
 * 		Client
 * 		Sending Data
 * 		Receiving Data
 */

/**
 * @brief Set the socket type
 * 
 * The types are based on constants: NetSocket::TYPE_X
 * 
 * @param socketType 
 */
void NetSocket::setType(int socketType) {
	this->socketType = socketType;
}

/**
 * @brief The socket type
 * 
 * @return int 
 */
int NetSocket::getType() {
	return this->socketType;
}

/**
 * Create a listening socket for the server
 * 
 */
bool NetSocket::createServerSocket(int usePort) {
	// Set the type
	this->setType(NetSocket::TYPE_SERVER);

	cout << "Create Socket\n";

	// Create the socket file descripter
	srv_file_desc = socket(AF_INET, SOCK_STREAM, 0);

	// Did the socket fail?
	if (!srv_file_desc) {
		cout << "Socket Failed\n";
		return false;
	}

	int opt = 1;
	int addrlen = sizeof(address);

	cout << "Attach Socket To Port\n";

	// Attach the socket to a port
	if (setsockopt(srv_file_desc, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		cout << "SetSockOpt Failed\n";
		return false;
	}

	// Set the address information
	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons( usePort );

	// Bind the socket to the port
	if (::bind (srv_file_desc, (struct sockaddr *)&address, sizeof(address)) < 0) {
		cout << "Bind Failed\n";
		return false;
	}

	cout << "Create Listener\n";

	// Create a listener for connections
	listen(srv_file_desc, 3);

	cout << "Awaiting Connections on port: " << usePort << "\n";

	// Accept a new connection to the server
	client_socket = accept(srv_file_desc, (struct sockaddr *)&address, (socklen_t*)&addrlen);

	// Show a connection message
	printf("Client connected from %s on port %d\n", inet_ntoa(address.sin_addr), ntohs(address.sin_port));

	return true;
}

/**
 * Create a client socket
 */
bool NetSocket::createClientSocket(string serverIp, int usePort) {
	// Set the type
	this->setType(NetSocket::TYPE_CLIENT);

	// Create the socket file descripter
	srv_file_desc = socket(AF_INET, SOCK_STREAM, 0);

	address.sin_family = AF_INET;
    address.sin_port = htons(usePort);

	inet_pton(AF_INET, serverIp.c_str(), &address.sin_addr);

	printf("Connecting to %s on port %d\n", serverIp.c_str(), usePort);

	// Connect to the IP
	if (connect(srv_file_desc, (struct sockaddr *)&address, sizeof(address)) < 0) {
		cout << "Connection Failed\n";
		return false;
	}

	return true;
}

/**
 * Send data to server socket
 */
void NetSocket::sendData(string dataToSend) {
	// What socket are we sending to?
	int socketToUse = (this->getType() == NetSocket::TYPE_CLIENT) ? srv_file_desc : client_socket;

	// Send a message
	if (send(socketToUse, dataToSend.data(), dataToSend.length()+1, 0) < 0) {
		cout << "Send Failed...";
	}
}

/**
 * Get data from the socket
 */
string NetSocket::getFromSocket(int packetSize) {
	// What socket are we getting from?
	int socketToUse = (this->getType() == NetSocket::TYPE_CLIENT) ? srv_file_desc : client_socket;

	// How much of a packet do we want to read?
	// -- We need to account for additional 51 bytes for header information
	// -- If we don't pass in a packet size, then assume we don't care the size.
	if (packetSize > 0) packetSize += 51;
	int buffPacketSize = (packetSize > 0) ? packetSize : 65535;

	// Buffered data we want to return
	vector <char> fullBufferData;

	int hasRead = 0; // Keep track of how much we read so far

	do {
		// Adjust our buffer and read size based on how much is left
		// - We need to do this to account for multiple packets being sent at once.
		int remainToRead = buffPacketSize - hasRead;
		if (remainToRead == 0) break;

		vector <char> recvBuffer(remainToRead);
		ssize_t dataSize = recv(socketToUse, recvBuffer.data(), recvBuffer.size(), 0);

		// Didn't get any data? Move On
		if (dataSize == 0) {
			break;
		}

		// How much have we read?
		hasRead += dataSize;

		// Resize the buffer
		recvBuffer.resize(dataSize);

		// Add the data to our master vector.
		fullBufferData.insert(fullBufferData.end(), recvBuffer.begin(), recvBuffer.end());

	} while (packetSize != 0 && hasRead < packetSize);

	// Nothing in our buffer? Nothing to return
	if (fullBufferData.size() == 0) {
		return "";
	}

	return string(fullBufferData.begin(), fullBufferData.end()-1);
}

/**
 * @brief Close the socket
 */
void NetSocket::closeSocket() {
	close(srv_file_desc);
}