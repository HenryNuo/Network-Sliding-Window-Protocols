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
#include <chrono>
#include "Packet.h"
#include "NetSockets.h"
using namespace std;
/**
 * 
 * This program prompts the user for details on transferring the file, then connects to a receiver.
 *
 */

// Global Data
mutex ackMutex;
unsigned int curSeqNum = 0;      // Starts at 1 due to initial file details packet being 0.
int numRetrans = 0;         // Number of retransmitted packets
int timeoutMS;          // User-specified timeout in milliseconds
int slidingWindowSize = -1;
int slidingWindowFront = 1; // Track where we are in the start of the sliding window.
int slidingWindowEnd = 0;   // Track where we are in the end of the sliding window
vector <unique_ptr<Packet> > packetList; // All of our active packets
NetSocket clientSocket; // Socket Connection
int packetSize;

/**
 * @brief Show the current sliding window
 * 
 * Example: [1, 2, 3, 4, 5]
 */
void showSlidingWindow() {

    // If the front of the sliding window is after the ending, then show nothing.
    if (slidingWindowFront > slidingWindowEnd) {
        return;
    }

    // Create the window display
    int slidingWindowMax = slidingWindowFront + slidingWindowSize - 1;
    cout << "[";
    for (int i = slidingWindowFront; i <= slidingWindowMax; i++) {
        cout << i;

        if (i != slidingWindowMax) {
            cout << ", ";
        }
    }
    cout << "]\n";
}

/**
 * @brief Send the initial file packet through the socket
 * 
 * This method's purpose is to send all of the initial details about the file such as name, file size,
 *      and expected number of packets to the receiver to prep it.
 * 
 * Once it constructs and sends the information, it'll wait to receive a successful ACK request
 *      before moving forward. If this process fails, the file will not be sent.
 * 
 * @param outFileName Output Name of the file being sent
 * @param fileSize Size of the file being sent
 * @param numPackets Number of expected packets (including this one)
 */
void sendInitialFilePacket(string outFileName, int fileSize, int numPackets) {

    // Construct the initial packet
    //      Due to the difference in information, we need to know where each piece of data
    //          starts and ends for safety. So we do this by padding the numbers into defined lengths
    //      an alterative is adding specialty dividers, but that runs the risk of file name restrictions.

    // Pad the file size to 16 bytes
    string fileSizeStr = to_string(fileSize);
    string padFileSize = string(16 - fileSizeStr.length(), '0').append(fileSizeStr);

    // Pad the file size to 16 bytes
    string numPacketsStr = to_string(numPackets);
    string padNumPackets = string(16 - numPacketsStr.length(), '0').append(numPacketsStr);

    // Pad the packet size to 16 bytes
    string packetSizeStr = to_string(packetSize);
    string padPacketSize = string(16 - packetSizeStr.length(), '0').append(packetSizeStr);

    // Create the data string for the packet
    string packetData = padFileSize + padNumPackets + padPacketSize + outFileName;

    Packet initialPacket = Packet();
    initialPacket.setSeqNum(0);
    initialPacket.setData(packetData);

    // Send the packet
    clientSocket.sendData(initialPacket.createPacketString());

    // Wait until we receive an acknowledgement
    while (1) {
        string socketData = clientSocket.getFromSocket();
		if (socketData.length() > 0) {
            
            Packet ackPacket = Packet();
            ackPacket.reversePacket(socketData);
            cout << "Ack " << ackPacket.getSeqNum() << " received\n";

            // TODO: Handle ACK failure

            // No longer need the packet
            break;
        }
    }
}

/**
 * @brief Continuously read ACK messages
 * 
 * This function's purpose is to have an infinite loop that constantly reads from the socket
 *      to see if there are any ACK message.s
 * 
 * As packets come in, it'll determine if the packet was successfully send, or if it'll need to resend it.
 */
void readACKMessages() {
    while (1) {
        string socketData = clientSocket.getFromSocket();
		if (socketData.length() > 0) {
            Packet ackPacket = Packet();
            ackPacket.reversePacket(socketData);

            // // Successful Ack? Remove it from the packet list and adjust the window if need be.
            if (ackPacket.getAck() == Packet::ACK_OK) {
                printf("Ack %d received\n", ackPacket.getSeqNum());

                // Find the associated packet in our packetList
                int findSeqNum = ackPacket.getSeqNum();
                std::lock_guard<mutex> lock(ackMutex);

                vector<unique_ptr<Packet>>::iterator iterator = packetList.begin();

                while (iterator != packetList.end()) {
                    // printf("U");
                    
                    // Find it?
                    if ((*iterator)->getSeqNum() == findSeqNum) {

                        // Remove the packet from the list.
                        iterator = packetList.erase(iterator);

                        // If this is the start of the sliding window, then we can move it.
                        if (slidingWindowFront == ackPacket.getSeqNum()) {

                            // Already have packets? Use the first one.
                            if (packetList.size() > 0) {
                                slidingWindowFront = packetList.front()->getSeqNum();

                            // Otherwise just increase the window by one
                            } else {
                                slidingWindowFront = ackPacket.getSeqNum() + 1;
                            }

                            // Show the current sliding window.
                            showSlidingWindow();
                        }
                        break;
                    } else {
                        iterator++;
                    }
                }
                

            // // ACK failure - retransmit.
            } else {
                printf("Failure ack %d received\n", ackPacket.getSeqNum());

                // Set a new timeout
                ackPacket.setTimeout(timeoutMS);

                // Retransmit the packet
                clientSocket.sendData(ackPacket.createPacketString());
                printf("Packet %d Re-transmitted \n", ackPacket.getSeqNum());
                numRetrans++;
            }

            // Release the memory for the packet
            // ackPacket.reset();
        }
    }
}

/**
 * @brief Process chunk data
 * 
 * This function is what handles taking pieces of the broken up file and sending it through the socket.
 * @param chunkData 
 */
void processChunkData(string chunkData) {

    std::lock_guard<mutex> lock(ackMutex);

    // Increase the sequence number
    curSeqNum++; // Increase the sequence number

    // Move the ending window size - we trust other code to keep this in check.
    slidingWindowEnd++;

    // Create the packet we want to send to the receiver
    unique_ptr<Packet> newPacket(new Packet());
    newPacket->setTimeout(timeoutMS);
    newPacket->setSeqNum(curSeqNum);        // Set the sequence number

    // Set the data
    newPacket->setData(chunkData);
    
    // Compile and send the packet data through the socket.
    clientSocket.sendData(newPacket->createPacketString());
    printf("Packet %d sent (%lu bytes)\n", newPacket->getSeqNum(), chunkData.size());

    // Add the packet to the list of packets in progress.
    packetList.push_back(move(newPacket));

}

/**
 * @brief Check the packet queue
 * 
 * This method checks to see if we are able to continue in the packet queue based on:
 *      A) The first packet in the queue isn't the start of the window size.
 *      B) We haven't hit the end of our sliding window size
 * 
 * If we detect a packet that has timed out without a ACK, it also resends the packet.
 */
void checkPacketQueue() {
    // Keep going until we decide to move forward.
    while (1) {
        // TODO: Determine if locking will cause issues in infinite loop...
        // We do need to lock due to use from the ACK thread constantly making updates.
        std::lock_guard<mutex> lock(ackMutex);

        // Process to see if we need to retransmit any packets
        vector<unique_ptr<Packet>>::iterator iterator = packetList.begin();
        while (iterator != packetList.end()) {

            // Did we hit the timeout?
            if ((*iterator)->hasTimedOut()) {
                printf("Packet %d ***** Timed Out *****\n", (*iterator)->getSeqNum());

                // Set a new timeout
                (*iterator)->setTimeout(timeoutMS);

                // Retransmit the packet
                clientSocket.sendData((*iterator)->createPacketString());
                printf("Packet %d Re-transmitted\n", (*iterator)->getSeqNum());
                numRetrans++;

                // TODO: If this is Go-Back-N and this was the beginning of the sliding window, we 
                //      need to throw away all other following packets and run them again.
            }

            ++iterator;
        }

        // Are we allowed to continue to the next packet?
        int slidingWindowMax = slidingWindowFront + slidingWindowSize - 1;
        if (slidingWindowEnd < slidingWindowMax) {
            break;
        }
    }
}

/**
 * @brief Main Entry point for the program
 * 
 * Asks for user input to determine how it functions
 * Creates a socket and connects to the server / receiver
 * Spins up a thread to read acknowledgement requests *from* the receiver
 * Creates X number of threads to send data to the receiver
 * Sends the file
 */
int main(int argc, char *argv[]) {
	//prompt for file name
    string inputFileName;
    string outputFileName;
    string serverHost;
	int serverPort;
    string protocolType;
    //int packetSize;       //now a global variable
    int sequencerangeNumber;
    string artificialErrors;

    cout << "What is the name of the file you'd like to send? \n";
    cin >> inputFileName;

    cout << "What would you like the file saved as? \n";
    cin >> outputFileName;
     
    //prompt for server host(s)
    cout << "Specify a server host: \n";
    cin >> serverHost;
	
	//prompt for server port
	cout << "Specify a server port: \n";
	cin >> serverPort;

    //prompt for protocol type (GBN or SR)
    // cout << "What protocol? (enter \"GBN\" or \"SR\") \n";
    // cin >> protocolType;
    if (protocolType != "GBN" && protocolType != "SR") {

    }

    //prompt for packet size
    cout << "Packet size: \n";
    cin >> packetSize;
    
    //prompt for timeout interval
    cout << "Timeout interval (ms): \n";
    cin >> timeoutMS;

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
    
    // FILE VALIDATION + GET FILE SIZE
    ifstream inFile(inputFileName, ios::binary | ios::ate);
    int fileSize = inFile.tellg();
    if (fileSize < 0) {
        cout << "Cannot Read File: " << inputFileName << "\n";
        return 1;
    }

    // DETERMINE NUMBER OF CHUNKS + PACKETS
    int numChunks = (fileSize / packetSize);
    int finalChunkSize = (fileSize % packetSize);
    int numPackets = (fileSize / packetSize)+1;     //calculate numPackets (+1 for initial packet)

    // Do we have a few more bytes remaining?
    if (finalChunkSize > 0) {
        numChunks++;
    }

    cout << "FILESIZE: " << fileSize << " | NUM CHUNKS: " << numChunks << " | FINAL CHUNK: " << finalChunkSize << "\n";

    // CONNECT TO SOCKET
	if (!clientSocket.createClientSocket(serverHost, serverPort)) {
        return 1;
    }

    // First packet - provide details on the file itself (name + filesize)
    //      Note - This is a *required* first packet and will wait for successful ACK from the
    //          receiver to ensure it sent everything properly. Once good it'll move on to sending
    //          the actual file.
    sendInitialFilePacket(outputFileName, fileSize, numChunks);

    // Spin off a thread for reading ACK packets
    thread readACKMessagesThread(readACKMessages); 
    readACKMessagesThread.detach();

    // Start clock for transfer speed
    chrono::steady_clock::time_point timeSpeedStart = std::chrono::steady_clock::now();

    // Set the initial window start - this is increased in the ACK process.
    slidingWindowFront = 1;

    // Attempt to read the file in chunks
    inFile.seekg(0); // Go back to beginning of file (due to originally going to end for file size)
    cout << "Reading File...\n";

    // - Create a thread for every # based on windows size
    // - Once we hit the # of windows sizes, hold until the first thread finishes. (.join)
    // - Once that thread finishes, then get the next one in the list.
    // - Ideally we'll have X threads running at any one time
    // slidingwindowSize = 3;
    int curChunkNum = 0;
    int dataProcessed = 0;
    string curFileBuff = "";
    string nextFileBuff = "";
    int fileSizeRead = 0;
    while (!inFile.eof()) {
        curChunkNum++;

        // We run into a problem where the OS can stop the read process resulting in smaller chunks than expected. We're going to keep reading until we hit the size we want.
        int amountToRead = (curChunkNum == numChunks && finalChunkSize > 0) ? finalChunkSize : packetSize;
        int chunkLeftToRead = amountToRead - nextFileBuff.length() - curFileBuff.length();

        cout << "\n";
        while (chunkLeftToRead > 0) {

            // Create a temporary buffer for this next read
            vector <char> fileBuffTemp(chunkLeftToRead, 0);

            // Read the chunk of data
            if(!inFile.read(fileBuffTemp.data(), chunkLeftToRead)) {
                cout << "Read Failed\n";
                sleep(5);
                continue;
            }

            cout << "BUFF SIZE: " << fileBuffTemp.size() << "\n";

            streamsize strm = inFile.gcount();

            // // Determine how much we actually read
            string tempDataRead = fileBuffTemp.data();

            cout << "You asked for " << chunkLeftToRead << " but I actually read " << tempDataRead.length() << " (" << fileSizeRead << ") " << inFile.tellg() << "\n";

            // Ask for less next time
            chunkLeftToRead = chunkLeftToRead - tempDataRead.length();

            cout << "Yes | " << chunkLeftToRead << "\n";

            // Add the data to the full chunk buffer.
            curFileBuff += fileBuffTemp.data();

        }

        // If the current buffer is bigger than we expect, move part of it to the next one.
        if (curFileBuff.length() > amountToRead) {
            nextFileBuff = curFileBuff.substr(amountToRead);
            curFileBuff = curFileBuff.substr(0, amountToRead);
        } else {
            curFileBuff = "";
        }

        cout << "I ACTUALLY READ: " << amountToRead << "\n";
        cout << "OVERFLOW: " << nextFileBuff.length() << "\n";

        fileSizeRead += curFileBuff.length();

        // Process the data
        processChunkData(curFileBuff);

        // Shift and/or reset our file buffers
        curFileBuff = nextFileBuff;
        nextFileBuff = "";

        // Move our next buffer

        // // See if we are on the last chunk and have to switch our size
        // if (curChunkNum == numChunks && fileBuffFinal.size() > 0) {
        //     printf("Final Chunk: %lu\n", fileBuffFinal.size());
        //     inFile.read(fileBuffFinal.data(), fileBuffFinal.size());

        //     // Due to weird binary stuff - for now - let's just force cut the data to our assumed chunk size
        //     // TODO: Is there a way to... not do this?
        //     string fileFinalData = fileBuffFinal.data();
        //     string cutFinalData = fileFinalData.substr(0, fileBuffFinal.size());

        //     processChunkData(cutFinalData);

        // // Read the next chunk of data
        // } else {
        //     dataProcessed += fileBuff.size();
        //     inFile.read(fileBuff.data(), fileBuff.size());

        //     string fileData = fileBuff.data();
        //     cout << "Size: " << fileData.size() << " | Wanted: " << fileBuff.size() << "\n";

        //     processChunkData(fileBuff.data());
        // }

        // Check / Hold on the packet queue
        // - This is a blocker until the file can continue. 
        checkPacketQueue();

        // Safety check to ensure we don't go over our expected packet count.
        if (curChunkNum == numChunks) {
            break;
        }
    }

    cout << "\n---- HUZZAH - File All Done ----\n";

    // Wait until the queue is processed
    checkPacketQueue();

    // Close the socket
    clientSocket.closeSocket();

    // Mark end time for file transfer.
    chrono::steady_clock::time_point timeSpeedEnd = std::chrono::steady_clock::now();

    // Determine our speed
    auto timeNumMS = chrono::duration_cast<chrono::milliseconds>(timeSpeedEnd - timeSpeedStart);
    cout << "Time: " << timeNumMS.count() << "\n";
    
    printf("Number of original packets sent: %d\n", numPackets);
    printf("Number of retransmitted packets: %d\n", numRetrans);
    printf("Total elapsed time: %lldms\n", timeNumMS.count());
    printf("Total throughput (Mbps): xxxx\n");
    printf("Effective throughput: xxxx\n");
    printf("Data Transferred: %d\n", dataProcessed); // TODO: Remove

    // cout << "Bytes Per Second: " << bytesPerSecond << "\n";

    cout << "\n\n!-!-!-!-!-!-!-!- DONE -!-!-!-!-!-!-!- \n";

	// DONE!
    
    return 0;  
} 