#include <string>
#include <iostream>
#include <bitset>
#include <sstream>
#include <vector>
#include <chrono>
#include "Packet.h"
using namespace std;

/**
 * Empty Constructor
 */
Packet::Packet() {
}

/**
 * Set the Sequence Number
 */
void Packet::setSeqNum(int seqNum) {
	this->seqNum = seqNum;
}

/**
 * Return the Sequence Number
 */
int Packet::getSeqNum() {
	return this->seqNum;
}

/**
 * Show the sequence number
 * 
 */
int Packet::showSeqNum() {
	// Do we have a range? Show the range-based version instead.
	if (this->seqNumRange > 0) {
		return this->seqNum % this->seqNumRange;	
	}

	return this->seqNum;
}


/**
 * Set the sequence range
 * 
 */
void Packet::setSeqNumRange(int seqNumRange){
	this->seqNumRange = seqNumRange;
}


/**
 * Return the sequence range
 * 
 */
int Packet::getSeqNumRange(){
	return this->seqNumRange;
}


/**
 * Set Packet Data
 */
void Packet::setData(vector <char> data) {
	this->data = data;
}

/**
 * Return data in the packet
 */
vector <char> Packet::getData() {
	return this->data;
}

/**
 * Set the Sequence Number
 */
void Packet::setAck(int ack) {
	this->ack = ack;
}

/**
 * Return the Sequence Number
 */
int Packet::getAck() {
	return this->ack;
}

/**
 * Set the stored checksum
 */
void Packet::setChecksum(int checksum) {
	this->checksum = checksum;
}

/**
 * Return the stored checksum
 */
int Packet::getChecksum() {
	return this->checksum;
}

/**
 * Create a checksum based on the data
 */
u_short Packet::createChecksum() {

	// Convert data to binary (needed for checksum)
	int dataLen = this->data.size();
	vector <unsigned short> dataCSArray;
	int dataCSArrayLen = 0;

	// Break the data into 16-bit blocks
	for (int i = 0; i < dataLen; i++) {
		dataCSArray.push_back((unsigned short) bitset<16>(this->data[i]).to_ulong());
		dataCSArrayLen++;
	}

	unsigned short* buffer = &dataCSArray[0];

	// Calculate the checksum
	u_long sum = 0;
	while (dataCSArrayLen--) {
		sum += *buffer++;
		if (sum & 0xFFFF0000) {
			sum &= 0xFFFF;
			sum++;
		}
	}
	return ~(sum & 0xFFFF);
}

/*
Create a packet by converting the details to binary
*/
string Packet::createPacketString() {
	return this->createPacketString(false);
}
string Packet::createPacketString(bool forceNACK = false) {
	string pktString = "";

	// Add the sequence number to the packet string
	string seqNumStr = bitset<32>(this->getSeqNum()).to_string();
	pktString += seqNumStr;

	// Add an ack to the packet string
	pktString += bitset<2>(getAck()).to_string();

	// Determine a checksum, add it to the packet string
	u_short checksum = createChecksum();

	// Forcing a NACK / failed checksum?
	if (forceNACK) {
		checksum = 0;
	}

	pktString += bitset<16>(checksum).to_string();

	// Add the actual data to the packet string
	int dataLen = this->data.size();
	string dataBin = "";
	for (int i = 0; i < dataLen; i++) {	
		pktString += this->data[i];
	}

	return pktString;
}

/*
Take a binary input of packet data and determine its details
*/
void Packet::reversePacket(string inputData) {

	// Sequence Number
	int seqNum = bitset<32>(inputData.substr(0, 32)).to_ulong();
	setSeqNum(seqNum); 		// 32-bit sequence number
	
	// Acknowledgement 
	int ackNum = bitset<2>(inputData.substr(32, 2)).to_ulong();
	setAck(ackNum);			//1-bit ack
	
	// Checksum
	u_short checkSum = bitset<16>(inputData.substr(34, 16)).to_ulong();
	setChecksum(checkSum);	//16-bit checksum
	
	// Data - Need to convert to vector
	vector<char> data(inputData.begin() + 50, inputData.end());
	setData(data);
}

/**
 * @brief Is this a valid checksum?
 * 
 * This function computes the current checksum and verifies it against the one provided in a packet.
 * 
 * If the two checksums match, then the checksum is considered valid.
 * 
 * @return bool (true / false) 

 */
bool Packet::isValidChecksum() {

	// Calculate the checksum based off current data
	int newChecksum = this->createChecksum();

	// Compare the stored checksum to the newly calculated one
	if (this->checksum == newChecksum) {
		return true;
	} else {
		return false;
	}
}

/**
 * @brief Set and determine the timeout for the packet
 * 
 * This is tied to hasTimedOut(), which indicates of the time has passed.
 */
void Packet::setTimeout(int timeout) {

	// Determine the time to compare against in milliseconds
	this->timeoutTimePoint = chrono::system_clock::now() + chrono::milliseconds(timeout);
}

/**
 * @brief Has the packet timed out?
 * 
 * @return bool (true / false) 
 */
bool Packet::hasTimedOut() {

	// Get the current timepoint.
	chrono::system_clock::time_point curTimePoint = chrono::system_clock::now();

	// Compare the times. If the timeout time point has passed, then we have timed out the packet.
	return (curTimePoint > timeoutTimePoint);
}