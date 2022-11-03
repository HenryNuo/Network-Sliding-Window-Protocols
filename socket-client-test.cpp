/**
 * Client Test
 * 
 * This program listens for a connection and receives a file.
 */
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include "NetSockets.h"
using namespace std;

int main(int argc, char *argv[]) {

	// Global Variables
	string serverIp;
	int portNum;

	// Make sure user provided a port
	if (argc != 3) {
		cout << "Please provide a port # above 1024 as a parameter.\n";
		cout << "Example: ./receiver xxx.xxx.xxx.xxx 10000\n";
		return 1;
	}

	serverIp = argv[1];

	// Convert the number to int
	istringstream iss(argv[2]);

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
	clientSocket.createClientSocket(serverIp, portNum);
	sleep(2);
	clientSocket.sendData("Hello");
	cout << "Sent Hello\n";
	sleep(2);
	clientSocket.sendData("Hello2");
	cout << "Sent Hello2\n";
	sleep(2);
	clientSocket.sendData("STOP");
	cout << "Sent STOP\n";

	cout << "Good Bye";

	// Handle receiving file

	// Parse packet

	// Store file

	return 0;
}