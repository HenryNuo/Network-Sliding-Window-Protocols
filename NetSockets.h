#include <netinet/in.h>
#include <string>
using namespace std;
#ifndef NETSOCKET_H
#define NETSOCKET_H

class NetSocket {

	private:
		struct sockaddr_in address;
		int socketType;
		int srv_file_desc, client_socket;

	public:
		static const int TYPE_SERVER = 1;
		static const int TYPE_CLIENT = 2;
		bool createServerSocket(int usePort);
		bool createClientSocket(string serverIp, int usePort);
		void setType(int socketType);
		int getType();
		void sendData(string dataToSend);
		string getFromSocket(int packetSize);
		void closeSocket();
};

#endif