#include <string>
#include <iostream>
#include <bitset>
#include <sstream>
#include <vector>
#include <Packet.h>
using namespace std;

/**
 * Empty Constructor
 */
Packet::Packet() {
}

/**
 * Set the packet flag
 * 
 * This is used to indicate what type of packet this is.
 * The type is determined by the TYPE_X constants.
 */
void Packet::setType(int type) {
	type = type;
}

/**
 * Set the Sequence Number
 */
void setSeqNum(int seqNum) {
	seqNum = seqNum;
}

/**
 * Set Packet Data
 */
void setData(string data) {
	data = data;
}

/**
 * Create a checksum
 */
u_short Packet::createChecksum(u_short *buffer, int count) {
	register u_long sum = 0;
	while (count--) {
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
string Packet::createPacket() {

	// Convert data to binary (needed for checksum?)
	int dataLen = data.length();
	vector <unsigned short> dataChecksumArray;
	int dataChecksumArrayLen = 0;
	unsigned short dataCSArr[] = {};
	string dataBin = "";
	for (int i = 0; i < dataLen; i++) {
		dataBin += bitset<8>(data[i]).to_string();
		dataChecksumArray.push_back((unsigned short) bitset<16>(data[i]).to_ulong());
		dataChecksumArrayLen++;
	}

	// Create a Checksum
	// unsigned short dataShort;
	// istringstream dataStream(data);
	// dataStream >> dataShort;
	// cout << "SHORT: " << dataShort << "\n";
	// this->checksum = this->createChecksum(&dataShort, 4);
	unsigned short* dataChecksumArrayUS = &dataChecksumArray[0];
	checksum = createChecksum(dataChecksumArrayUS, dataChecksumArrayLen);
	cout << "CHECKSUM: " << checksum << "\n";

	string pktData = "";
	// 0 - 15 = Source Port  |  17 - 31 = Destination Port
	pktData += bitset<16>(srcPort).to_string();
	pktData += bitset<16>(destPort).to_string();
	// pktData += "\n";
	
	// 0 - 31 = Sequence Num
	pktData += bitset<32>(seqNum).to_string();
	// pktData += "\n";
	// 0 - 31 = Acknowledgement
	pktData += bitset<32>(ack).to_string();
	// pktData += "\n";
	// 0 - 3 = HdrLen   |  4-9 = 0  |  10 - 15 = Flags  |   16-31 = Advertised Window
	pktData += bitset<4>(0).to_string();
	pktData += bitset<6>(0).to_string();
	pktData += bitset<6>(0).to_string();
	pktData += bitset<16>(0).to_string();
	// pktData += "\n";
	// 0 - 15 = Checksum    |    16-31  = UrgPtr
	pktData += bitset<16>(checksum).to_string();
	pktData += bitset<16>(0).to_string();
	// pktData += "\n";
	// 0 - 31 = Options
	pktData += bitset<32>(0).to_string();
	// pktData += "\n";

	// ... = Data
	// Possibly a horribly inefficient way to do this. :)
	pktData += dataBin;

	return pktData;
}

/*
Take a binary input of packet data and determine its details
*/
void Packet::reversePacket(string inputData) {

	// 0 - 15 = Source Port  |  17 - 31 = Destination Port
	srcPort = bitset<16>(inputData.substr(0, 16)).to_ulong();
	destPort += bitset<16>(inputData.substr(16, 16)).to_ulong();
	
	
	// 0 - 31 = Sequence Num
	seqNum += bitset<32>(inputData.substr(32, 32)).to_ulong();
	
	// 0 - 31 = Acknowledgement
	ack += bitset<32>(inputData.substr(64, 32)).to_ulong();
	
	// 0 - 3 = HdrLen   |  4-9 = 0  |  10 - 15 = Flags  |   16-31 = Advertised Window
	
	// 0 - 15 = Checksum    |    16-31  = UrgPtr
	checksum = bitset<32>(inputData.substr(128,16)).to_ulong();

	// 0 - 31 = Options

	// Data
	// - Loop through the remaining info 8 bits at a time to reconstruct the data.
	int dataLen = inputData.length();
	for (int i = 192; i < dataLen; i += 8) {
		string chunk = inputData.substr(i, 8);
		// cout << "CHUNK: " << chunk << " | " << char(bitset<8>(chunk).to_ulong()) << "\n";
		data += char(bitset<8>(chunk).to_ulong());
	}
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
	// Convert data to binary (needed for checksum?)
	int dataLen = data.length();
	vector <unsigned short> dataChecksumArray;
	int dataChecksumArrayLen = 0;
	unsigned short dataCSArr[] = {};
	for (int i = 0; i < dataLen; i++) {
		dataChecksumArray.push_back((unsigned short) bitset<16>(data[i]).to_ulong());
		dataChecksumArrayLen++;
	}

	// Create a Checksum
	unsigned short* dataChecksumArrayUS = &dataChecksumArray[0];
	int newChecksum = this->createChecksum(dataChecksumArrayUS, dataChecksumArrayLen);

	if (checksum == newChecksum) {
		return true;
	} else {
		return false;
	}
}

/*int main(int argc, char *argv[]) {

	// CREATE A PACKET
	cout << "\n\n ---- CREATE PACKET ---- \n\n";
	Packet myPkt = Packet();
	myPkt.srcPort = 1000;
	myPkt.destPort = 5020;
	myPkt.seqNum = 10;
	myPkt.ack = 0;
	myPkt.data = "Hello, this is data. Hi. Test. ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789abcdefghijklmnopqrstuvwxyz";

	cout << myPkt.createPacket();

	// REVERSE A PACKET
	cout << "\n\n ---- REVERSE PACKET ---- \n\n";
	string packetData = 
	"000000111110100000010011100111000000000000000000000000000000101000000000000000000000000000000000000000000000000000000000000000001110000100001011000000000000000000000000000000000000000000000000010010000110010101101100011011000110111100101100001000000111010001101000011010010111001100100000011010010111001100100000011001000110000101110100011000010010111000100000010010000110100100101110001000000101010001100101011100110111010000101110001000000100000101000010010000110100010001000101010001100100011101001000010010010100101001001011010011000100110101001110010011110101000001010001010100100101001101010100010101010101011001010111010110000101100101011010001100000011000100110010001100110011010000110101001101100011011100111000001110010110000101100010011000110110010001100101011001100110011101101000011010010110101001101011011011000110110101101110011011110111000001110001011100100111001101110100011101010111011001110111011110000111100101111010";
	aPacket newPkt = Packet();
	newPkt.reversePacket(packetData);

	cout << "Packet Info:\n";
	cout << "Source Port: " << newPkt.srcPort << " | Dest Port: " << newPkt.destPort << "\n";
	cout << "Sequence Num: " << newPkt.seqNum << "\n";
	cout << "Acknowledgement: " << newPkt.ack << "\n";
	cout << "Checksum: " << newPkt.checksum << " - " << (newPkt.isValidChecksum() ? "VALID" : "INVALID") << "\n";
	cout << "Data: " << newPkt.data << "\n";

	return 0;
} */