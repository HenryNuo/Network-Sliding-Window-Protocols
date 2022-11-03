#include <string>
#include <iostream>
#include <chrono>
#include <vector>
using namespace std;
class Packet {
	private: 
		unsigned int seqNum;		// Packet sequence number
		int seqNumRange = 0;    // Sequence number range (0 = no range)
		int ack = 0;		// Acknowledgement (0 = None, 1 = OK, 2 = FAIL)
		int checksum;	// Current Checksum
		vector <char> data;	// Packet data or file name (initial packet only)
		chrono::system_clock::time_point timeoutTimePoint;	// Time that the packet times out.

	public:
		static const int ACK_OK = 1;
		static const int ACK_FAIL = 2;

		Packet();

		// Sequence Number
		void setSeqNum(int seqNum);
		int getSeqNum();

		// Show Sequence Number
		int showSeqNum();

		// Set Sequence Number Range 
		void setSeqNumRange(int seqNumRange);

		// Return Sequence Number Range
		int getSeqNumRange();

		// File Data
		void setData(vector <char> data);
		vector <char> getData();

		// Checksum
		u_short createChecksum();
		bool isValidChecksum();
		void setChecksum(int checksum);
		int getChecksum();

		// Acknowledgement
		void setAck(int ack);
		int getAck();

		// Packet creation / reversal
		string createPacketString(bool forceNACK);
		string createPacketString();
		void reversePacket(string inputData);

		// Packet Timeout
		void setTimeout(int timeout);
		bool hasTimedOut();

};
