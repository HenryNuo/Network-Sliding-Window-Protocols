#include "sender.h"
#include <iostream>
#include <fstream>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <string>
#include <unistd.h>
#include <filesystem>
using namespace std;
/**
 * 
 * This program prompts the user for details on transferring the file, then connects to a receiver.
 *
 */

vector <string> endData;
mutex pktMutex;
int curSeqNum = 0;
int slidingWindowSize = -1;
queue <thread>  threadList; // All of our threads

void myMagicThread(int aNum, string data) {
    printf("DATA (%d): %s\n", aNum, data.c_str());

    // Add the data to the array
    pktMutex.lock();
    endData.push_back(data);
    pktMutex.unlock();

    sleep(2);
}

/**
 * @brief Process chunk data
 * 
 * This function is what handles breaking the data into threads and packets.
 * @param chunkData 
 */
void processChunkData(string chunkData) {
    thread myFirstThread(myMagicThread, curSeqNum, chunkData); // Y U NO WORK?!
    printf("\nStarting thread: %d\n", curSeqNum);

    // myFirstThread.detach();
    threadList.push(move(myFirstThread));

    printf("\nThreadList Size: %lu\n", threadList.size());

    // Once we hit our limit, wait until the first item is finished.
    if (slidingWindowSize > 0 && threadList.size() == slidingWindowSize) {
        // Hit our limit? Then wait until the top thread finishes
        cout << "Thread Hold...\n";
        thread myNewThread = move(threadList.front());
        threadList.pop(); // Remove the item from the queue
        myNewThread.join();
    }

    curSeqNum++;
}

int main(int argc, char *argv[]) {
	//prompt for file name
    string fileName;
    string serverHost;
	string serverPort;
    string protocolType;
    int packetSize;
    int timeout;
    int sequencerangeNumber;
    string artificialErrors;

    cout << "What is the name of the file you'd like to send? \n";
    cin >> fileName;
     
    //prompt for server host(s)
    // cout << "Specify a server host: \n";
    // cin >> serverHost;
	
	//prompt for server port
	// cout << "Specify a server port: \n";
	// cin >> serverPort;

    //prompt for protocol type (GBN or SR)
    // cout << "What protocol? (enter \"GBN\" or \"SR\") \n";
    // cin >> protocolType;
    if (protocolType != "GBN" && protocolType != "SR") {

    }

    //prompt for packet size
    cout << "Packet size: \n";
    cin >> packetSize;
    
    //prompt for timeout interval
    // cout << "Timeout interval: \n";
    // cin >> timeout;

    //prompt for sliding window size
    cout << "Sliding window size: \n";
    cin >> slidingWindowSize;

    //prompt for sequence range #
    // cout << "Sequence range number: \n";
    // cin >> sequencerangeNumber;

    //prompt for artificial errors
    // cout << "What is the type of the error? (\"None\" or \"Random\" or \"User specified\") \n";
    // cin >> artificialErrors;

	/* Process the file  */
	// -- Validate existence
	// -- Identify # of packets

    // FILE VALIDATION + GET FILE SIZE
    ifstream inFile(fileName, ifstream::ate | ifstream::binary);
    int fileSize = inFile.tellg();
    if (fileSize < 0) {
        cout << "Cannot Read File: " << fileName << "\n";
        return 1;
    }

    int numPackets = (fileSize / packetSize) + 1;     //calculate numPackets
    int packetSizeFinal = (fileSize % packetSize) - 1;

    cout << "FILESIZE: " << fileSize << "\nNUM PACKETS: " << numPackets << "\nFINAL PACKET: " << packetSizeFinal << "\n";

    // Attempt to read the file in chunks
    inFile.seekg(0);
    cout << "Reading File...\n";
    vector <char> fileBuff(packetSize, 0);
    vector <char> fileBuffFinal(packetSizeFinal, 0);
    // int fileCounter = 0;
    // while (!inFile.eof()) {
    // while (inFile.read(fileBuff.data(), fileBuff.size())) {
    //     // cout << fileBuff.data() << "|";
    //     // fileCounter++;
    // }

    // cout << "\nChunks Read: " << fileCounter << "\n";

    // find the file size
    // cout << "Opening File...\n";
    // ifstream in_file(fileName, ifstream::binary);        //originally (..., ios::binary)
    // in_file.open(fileName);     //don't know if this has already been opened by the above line
    // if (!in_file.good()) {
    //     cout << "Bad File\n";
    //     return 1;
    // }
    // in_file.seekg(0, ios::end);
    // fileSize = in_file.tellg();
    // cout << "Size of the file is " << fileSize << " bytes\n";

    // Create socket and connect to receiver
	// Read the file - based on packet size
    // -- Create a file buffer
    // cout << "Reading File...\n";
    // vector <char> fileBuff(packetSize, 0);                      
    // while (in_file.read(fileBuff.data(), fileBuff.size())) {
    //     //cout << fileBuff.front();
    //     cout << fileBuff.data();
    //     cout << ".";
    // }
    // cout << "";
    // while (!in_file.eof()) {
    //     in_file.read(fileBuff.data(), fileBuff.size());
    //     cout << "Number of characters read and stored by .read() : " << in_file.gcount() << "\n";
    //     cout << fileBuff.data();
    //     cout << ".";
    //     streamsize dataSize = in_file.gcount();
    // }

    

    // in_file.close();
    // cout << "\nDONE\n";

    // MOVE TO READ FILE
    // - Create a thread for every # based on windows size
    // - Once we hit the # of windows sizes, hold until the first thread finishes. (.join)
    // - Once that thread finishes, then get the next one in the list.
    // - Ideally we'll have X threads running at any one time
    // slidingwindowSize = 3;
    int curChunkNum = 0;
    while (!inFile.eof()) {
        curChunkNum++;

        // The last chunk has different size to grab.
        if (curChunkNum == numPackets) {
            inFile.read(fileBuffFinal.data(), fileBuffFinal.size());
            processChunkData(fileBuffFinal.data());

        // Read the next chunk of data
        } else {
            inFile.read(fileBuff.data(), fileBuff.size());
            processChunkData(fileBuff.data());
        }
        printf("\n%d of %d\n", curChunkNum, numPackets);

        if (curChunkNum == numPackets) {
            break;
        }
    }

    cout << "\n Run Remaining " << threadList.size() << " Threads \n";
    while (!threadList.empty()) {
        thread curThread = move(threadList.front());
        threadList.pop();
        curThread.join();
        cout << "Remaining... " << threadList.size() << "\n";
    }

    cout << "\n---- HUZZAH - All Threads Done ----\n";

    cout << "\n---- OUTPUT DATA ----\n";
    cout << "||";
    for (auto & elData : endData) {
        cout << elData;
    }
    cout << "||";

    cout << "\n\n!-!-!-!-!-!-!-!- DONE -!-!-!-!-!-!-!- \n";

    // Build packets

	// Send packets

	// Receive packets (ACK)

	// DONE!
    
    return 0;  
} 