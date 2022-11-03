#include "sender.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <vector>
#include <queue>
#include <mutex>
#include <thread>
#include <string>
#include <unistd.h>
#include <filesystem>
#include <chrono>
#include <algorithm>
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
int timeoutMulti;       // Multiplication factor for RTT
int slidingWindowSize = -1;
int slidingWindowFront = 1; // Track where we are in the start of the sliding window.
int slidingWindowEnd = 0;   // Track where we are in the end of the sliding window
int seqNumRange = 0;        // Sequence Number Range
vector <unique_ptr<Packet> > packetList; // All of our active packets
NetSocket clientSocket; // Socket Connection
int packetSize;         // Max packet size for data
int numPackets;    // # of packets to send
string protocolType;    // Protocol used (GBN or SR)
bool keepReadACK = true;   // Do we keep reading for ACKs?
bool hasACKClosed = false; // Indicate if the ACK thread successfully closed
string artificialErrors;    // Errors: None, User, or Random
vector<int> errorDrop;      // Stores which packets the user specifies to drop (Forced error)
vector<int> errorNACK;      // Stores which packets the user specifies to receive NACK (Forced error)
vector<int> errorLostAck;   // Stores which packets the user specifies to lose ACK (Forced error)
int RTT = 0;


/**
 * @brief Show the current sliding window
 * 
 * The window shifts after each ack received.
 * Example: [1, 2, 3, 4, 5]
 */
void showSlidingWindow() {

    // If the front of the sliding window is after the ending, then show nothing.
    if (slidingWindowFront > slidingWindowEnd) {
        return;
    }

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
 * @brief Find the packet associated with a sequence number
 * 
 * @return Packet* 
 */
vector<unique_ptr<Packet>>::iterator findPacketBySeqNum(int findSeqNum) {
    // std::lock_guard<mutex> lock(ackMutex);
    vector<unique_ptr<Packet>>::iterator iterator = packetList.begin();
    while (iterator != packetList.end()) {
        
        // Find it?
        if ((*iterator)->getSeqNum() == findSeqNum) {

            // Return the reference to the packet.
            return iterator;

            break;
        } else {
            iterator++;
        }
    }

    return packetList.end();
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
 */
void sendInitialFilePacket(string outFileName, int fileSize) {

    // Construct the initial packet
    //      Due to the difference in information, we need to know where each piece of data
    //          starts and ends for safety. So we do this by padding the numbers into defined lengths.

    // Pad the file size to 16 bytes
    string fileSizeStr = to_string(fileSize);
    string padFileSize = string(16 - fileSizeStr.length(), '0').append(fileSizeStr);

    // Pad the file size to 16 bytes
    string numPacketsStr = to_string(numPackets);
    string padNumPackets = string(16 - numPacketsStr.length(), '0').append(numPacketsStr);

    // Pad the packet size to 16 bytes
    string packetSizeStr = to_string(packetSize);
    string padPacketSize = string(16 - packetSizeStr.length(), '0').append(packetSizeStr);

    // Pad the window size to 16 bytes  (Go-Back-N uses "1" for receiver)
    string slidingWindowSizeStr = (protocolType == "GBN") ? "1" : to_string(slidingWindowSize);
    string padSlidingWindowSize = string(16 - slidingWindowSizeStr.length(), '0').append(slidingWindowSizeStr);

    // Pad the protocol to 3 bytes
    string padProtocolType = string(3 - protocolType.length(), ' ').append(protocolType);

    // Pad the packet size to 16 bytes
    string seqNumRangeStr = to_string(seqNumRange);
    string padSeqNumRange = string(16 - seqNumRangeStr.length(), '0').append(seqNumRangeStr);

    // Create the data string for the packet
    string packetData = padFileSize + padNumPackets + padPacketSize + padSlidingWindowSize + padProtocolType + padSeqNumRange + outFileName;

    // Convert the data to char
    vector<char> packetDataChar(packetData.begin(), packetData.end());

    // Append a null byte to the end
    packetDataChar.push_back('\0');

    // TODO: Filename (sometimes?) has extra character added.
    Packet initialPacket = Packet();
    initialPacket.setSeqNum(0);
    initialPacket.setData(packetDataChar);

    // Send the packet
    clientSocket.sendData(initialPacket.createPacketString());

    // Wait until we receive an acknowledgement
    while (1) {
        string socketData = clientSocket.getFromSocket(0);
		if (socketData.length() > 0) {
            
            Packet ackPacket = Packet();
            ackPacket.reversePacket(socketData);
            ackPacket.setSeqNumRange(seqNumRange);
            printf("Ack %d received\n", ackPacket.showSeqNum());

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
    while (keepReadACK) {
        string socketData = clientSocket.getFromSocket(1); // Grab the ACK
		if (socketData.length() > 0) {
            std::lock_guard<mutex> lock(ackMutex);
            Packet ackPacket = Packet();
            ackPacket.reversePacket(socketData);
            ackPacket.setSeqNumRange(seqNumRange);

            // check if sequence number of the received packet is in the errorLostAck vector
            if (!errorLostAck.empty() && ackPacket.getSeqNum() == errorLostAck.front()) {
                //erase first element of error vector
                errorLostAck.erase(errorLostAck.begin());

                // pretend that the Ack was lost
                continue;
            } 

            // Random Lost ACK error?
            if (artificialErrors == "Random") {
                if ((rand() % 51) < 1) {
                    printf("(Force Lost ACK): %d\n", ackPacket.showSeqNum());
                    continue;
                }
            }

            // Find the packet associated with our ACK'd response
            vector<unique_ptr<Packet>>::iterator thePacket = findPacketBySeqNum(ackPacket.getSeqNum());

            // Make sure we found the packet
            if (thePacket != packetList.end()) {

                // Reset the timeout (we got the packet)
                (*thePacket)->setTimeout(500);

                if (ackPacket.getAck() == Packet::ACK_OK) {
                    printf("Ack %d received\n", ackPacket.showSeqNum());

                    // Mark that we got the ack - we'll delete it and shift the sliding window in checkPacketQueue()
                    // - This way we handle if the ACKs come out of order.
                    (*thePacket)->setAck(1);
                    
                // // ACK failure - retransmit.
                } else {
                    printf("Failure ack %d received\n", ackPacket.showSeqNum());

                    // Retransmit the packet
                    clientSocket.sendData((*thePacket)->createPacketString());
                    printf("Packet %d Re-transmitted \n", (*thePacket)->showSeqNum());

                    numRetrans++;
                }
            }
        }
    }
    hasACKClosed = true;
}

/**
 * @brief Process chunk data
 * 
 * This function is what handles taking pieces of the broken up file and sending it through the socket.
 * @param chunkData 
 */
void processChunkData(vector <char> chunkData) {
    std::lock_guard<mutex> lock(ackMutex);

    // Increase the sequence number
    curSeqNum++; // Increase the sequence number

    // Move the ending window size - we trust other code to keep this in check.
    slidingWindowEnd++;

    // Create the packet we want to send to the receiver
    unique_ptr<Packet> newPacket(new Packet());
    newPacket->setTimeout(timeoutMS);
    newPacket->setSeqNum(curSeqNum);        // Set the sequence number
    newPacket->setSeqNumRange(seqNumRange);  // Set the sequence range

    // Set the data
    newPacket->setData(chunkData);

    // Force errorNACK
    bool forceNACK = false;

    // check if sequence number of the new packet is in the errorNACK vector
    if (!errorNACK.empty() && newPacket->getSeqNum() == errorNACK.front()) {
        // erase the desired error from the errorNACK vector 
        errorNACK.erase(errorNACK.begin());
        forceNACK = true; // Force the error
    }

    // Random Lost ACK error?
    if (artificialErrors == "Random") {
        if ((rand() % 51) < 1) {
            printf("(Force NACK): %d\n", newPacket->showSeqNum());
            forceNACK = true;
        }
    }

    // Keep track if we actually send packet data for error forcing.
    bool sendPacketData = true;

    // Did the packet get "dropped"? (FORCED ERROR)
    if (!errorDrop.empty() && newPacket->getSeqNum() == errorDrop.front()) {
        // erase the desired error from the errorNACK vector 
        errorDrop.erase(errorDrop.begin());

        sendPacketData = false;
    } else if (artificialErrors == "Random") {
        if ((rand() % 51) < 1) {
            printf("(Force Drop): %d\n", newPacket->showSeqNum());
            sendPacketData = false;
        }
    }

    // Do we send the data?
    if (sendPacketData) {
        // Compile and send the packet data through the socket.
        clientSocket.sendData(newPacket->createPacketString(forceNACK));
    }
    printf("Packet %d sent\n", newPacket->showSeqNum());

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
void checkPacketQueue(bool waitTillFinish = false) {

    // Keep going until we decide to move forward.
    while (1) {
        // TODO: Determine if locking will cause issues in infinite loop...
        // We do need to lock due to use from the ACK thread constantly making updates.
        std::lock_guard<mutex> lock(ackMutex);

        // Process to see if we need to retransmit any packets
        vector<unique_ptr<Packet>>::iterator iterator = packetList.begin();
        while (iterator != packetList.end()) {

            // Is this packet next in our list for our sliding window *and* it has a marked ACK?
            // - Then we can adjust the sliding window and remove it.
            if ((*iterator)->getSeqNum() == slidingWindowFront && (*iterator)->getAck() == 1) {
                slidingWindowFront++;

                // Erase the packet from the list
                iterator = packetList.erase(iterator);

                // Show the current sliding window.
                showSlidingWindow();

                continue;

            // Did we hit the timeout?
            } else if ((*iterator)->hasTimedOut()) {
                printf("Packet %d ***** Timed Out *****\n", (*iterator)->showSeqNum());

                // Set a new timeout
                (*iterator)->setTimeout(timeoutMS);

                // TODO: Add max retransmission (just a constant?)

                // TODO: Go-Back-N - Do we just let packets timeout that get sent after the 'missing' one, or do we have to indicate why we are retransmitting.

                // Retransmit the packet
                clientSocket.sendData((*iterator)->createPacketString());
                printf("Packet %d Re-transmitted\n", (*iterator)->showSeqNum());
                numRetrans++;

                // TODO: If this is Go-Back-N and this was the beginning of the sliding window, we 
                //      need to throw away all other following packets and run them again.
            }

            ++iterator;
        }

        // If we're waiting until we finish, then just see if we have any packets remaining.
        if (waitTillFinish) {
            // Do we still have packets remaining? 
            if (packetList.size() == 0) {
                break;
            }

        // We wait until the sliding window moves
        } else {

            // Are we allowed to continue to the next packet?
            int slidingWindowMax = slidingWindowFront + slidingWindowSize - 1;
            if (slidingWindowEnd < slidingWindowMax) {
                break;
            }
        }
    }
}

/**
 * @brief Calculate the timeout dynamically
 * 
 * Ping the server 3 times and keep track of the time involved. 
 * - Once successful, get the average time and use that as our timeout.
 */
void calculateDynamicTimeout() {

    chrono::steady_clock::time_point timeRTTStart;
    chrono::steady_clock::time_point timeRTTEnd;
    int totalTimeMS = 0;
    

    // Contact the socket three times
    for (int i = 0; i < 3; i++) {
        
        // Run the next attempt.
        timeRTTStart = std::chrono::steady_clock::now();
        
        clientSocket.sendData("PING");

        // Wait until we receive an acknowledgement
        while (1) {
            string socketData = clientSocket.getFromSocket(0);
            if (socketData.length() > 0) {     

                // Did we get the PING?       
                if (socketData.substr(0, 4) == "PING") {
                    break;
                }
            }
        }
        timeRTTEnd = std::chrono::steady_clock::now();

        // Convert the time to milliseconds and add it to our total time.
        auto timeRTTMS = chrono::duration_cast<chrono::milliseconds>(timeRTTEnd - timeRTTStart);
        auto timeRTTSec = chrono::duration_cast<chrono::seconds>(timeRTTEnd - timeRTTStart);
        RTT = timeRTTSec.count();

        // RTT so fast we did it in less than a millisecond? Default to a millisecond * factor.
        if (timeRTTMS.count() == 0) {
            totalTimeMS += timeoutMulti;
        } else {
            totalTimeMS += (timeRTTMS.count() * timeoutMulti);
        }
    }

    // Calculate the final time, with a minimum timeout of 1ms
    timeoutMS = max(1, totalTimeMS / 3);

    printf("Dynamic timeout set to %d milliseconds.\n", timeoutMS);
}

/**
 * @brief Prompt for user-supplied errors
 * 
 * Types:
 * - Drop: What packets do not make it to the receiver?
 * - Checksum: What packets receive a forced failure?
 * - ACK: What packets do not receive an ACK?
 */
void promptForUserErrors() {
    
    string rawInput;
    // packets dropped
    cout << "\nFor the following 3 prompts, input a list of integers separated by a space corresponding to the desired packets. \"Return\" means No Errors of that kind. *\n";
    cout << "Which packets should drop? (ints separated by spaces) \n> ";
    cin.ignore();
    getline(cin, rawInput);

    // Parse the numbers out
    string curNumber = "";
    for (int i = 0; i < rawInput.length(); i++) {
        char curChar = rawInput[i];
        if (isspace(curChar)) {
            errorDrop.push_back(atoi(curNumber.c_str()));
            curNumber = "";
        } else {
            curNumber += rawInput[i];
        }
    }
    // Add the last number, if any, to the list
    if (curNumber.length() > 0) {
        errorDrop.push_back(atoi(curNumber.c_str()));
    }
    sort(errorDrop.begin(), errorDrop.end());
    
    // NACKs (checksum failure)
    cout << "Which packets should fail checksum? \n> ";
    getline(cin, rawInput);

    // Parse the numbers out
    curNumber = "";
    for (int i = 0; i < rawInput.length(); i++) {
        char curChar = rawInput[i];
        if (isspace(curChar)) {
            errorNACK.push_back(atoi(curNumber.c_str()));
            curNumber = "";
        } else {
            curNumber += rawInput[i];
        }
    }
    // Add the last number, if any, to the list
    if (curNumber.length() > 0) {
        errorNACK.push_back(atoi(curNumber.c_str()));
    }
    sort(errorNACK.begin(), errorNACK.end());
    
    // ACKs lost
    cout << "Which packets should lose ACK? \n> ";
    getline(cin, rawInput);

    // Parse the numbers out
    curNumber = "";
    for (int i = 0; i < rawInput.length(); i++) {
        char curChar = rawInput[i];
        if (isspace(curChar)) {
            errorLostAck.push_back(atoi(curNumber.c_str()));
            curNumber = "";
        } else {
            curNumber += rawInput[i];
        }
    }
    // Add the last number, if any, to the list
    if (curNumber.length() > 0) {
        errorLostAck.push_back(atoi(curNumber.c_str()));
    }
    sort(errorLostAck.begin(), errorLostAck.end());

    cout << "\n";
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
    //int packetSize;       //now a global variable

    // TODO: Validate all the things!

    cout << "What is the name of the file you'd like to send? \n> ";
    cin >> inputFileName;

    cout << "What would you like the file saved as? \n> ";
    cin >> outputFileName;
     
    //prompt for server host(s)
    cout << "Specify a server host (IP Address): \n> ";
    cin >> serverHost;
	
	//prompt for server port
	cout << "Specify a server port: \n> ";
	cin >> serverPort;

    //prompt for protocol type (GBN or SR)
    cout << "What protocol? (enter \"GBN\" or \"SR\") \n> ";
    cin >> protocolType;
    if (protocolType != "GBN" && protocolType != "SR") {
        cout << "Please enter GBN (Go-Back-N) or SR (Selective Repeat)\n";
        return 1;
    }

    //prompt for packet size
    cout << "Packet size: \n> ";
    cin >> packetSize;
    
    //prompt for timeout interval
    cout << "Timeout interval (ms) (0 = Dynamic): \n> ";
    cin >> timeoutMS;

    if (timeoutMS == 0) {
        cout << "Timeout Multiplication Factor (RTT): \n> ";
        cin >> timeoutMulti;

        // Enforce a factor of at least 1.
        if (timeoutMulti < 1) {
            timeoutMulti = 1;
        }
    }

    //prompt for sliding window size
    cout << "Sliding window size: \n> ";
    cin >> slidingWindowSize;

    // Enforce a size of at least 1
    if (slidingWindowSize < 1) {
        slidingWindowSize = 1;
    }

    //prompt for sequence range #
    cout << "Sequence range number: (0 = No range) \n> ";
    cin >> seqNumRange;

    //prompt for artificial errors
    cout << "What is the type of the error? (\"None\" or \"Random\" or \"User\") \n> ";
    cin >> artificialErrors;

    if (artificialErrors == "User") {
        promptForUserErrors();

    // Quick validation - default to "None"
    } else if (artificialErrors != "Random" && artificialErrors != "None") {
        artificialErrors = "None";
    }

	/* Process the file  */
    
    // FILE VALIDATION + GET FILE SIZE
    ifstream inFile(inputFileName, ios::binary | ios::ate);
    int fileSize = inFile.tellg();
    if (fileSize < 0) {
        cout << "Cannot Read File: " << inputFileName << "\n";
        return 1;
    }

    // DETERMINE NUMBER OF CHUNKS + PACKETS
    numPackets = (fileSize / packetSize) + 1;  
    int finalChunkSize = (fileSize % packetSize);

    // Do we have a few more bytes remaining?
    if (finalChunkSize > 0) {
        numPackets++;
    }

    cout << "FILESIZE: " << fileSize << " | NUM PACKETS: " << numPackets << " | FINAL CHUNK: " << finalChunkSize << "\n";

    // CONNECT TO SOCKET
	if (!clientSocket.createClientSocket(serverHost, serverPort)) {
        return 1;
    }

    // No timeout specified? Calculate the timeout
    if (timeoutMS == 0) {
        calculateDynamicTimeout();
    }

    // First packet - provide details on the file itself (name + filesize)
    //      Note - This is a *required* first packet and will wait for successful ACK from the
    //          receiver to ensure it sent everything properly. Once good it'll move on to sending
    //          the actual file.
    sendInitialFilePacket(outputFileName, fileSize);

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
    int curChunkNum = 1; // Starts at 1 due to initial packet
    int dataProcessed = 0;
    string curFileBuff = "";
    string nextFileBuff = "";
    int fileSizeRead = 0;
    while (!inFile.eof()) {
        curChunkNum++;

        int amountToRead = (curChunkNum == numPackets && finalChunkSize > 0) ? finalChunkSize : packetSize;

        // Create a char buffer to old the read file data.
        vector <char> fileBuff(amountToRead, 0);

        // Read the chunk of data
        if(!inFile.read(fileBuff.data(), amountToRead)) {
            printf("Read Failed\n");
            return 1;
        }

        // Process the data
        processChunkData(fileBuff);

        // Check / Hold on the packet queue
        // - This is a blocker until the file can continue. 
        checkPacketQueue();

        // Safety check to ensure we don't go over our expected packet count.
        if (curChunkNum == numPackets) {
            break;
        }
    }

    // Wait until the queue is processed
    checkPacketQueue(true);

    // Indicate we no longer need the ACK thread and wait for it to close.
    keepReadACK = false;
    while (!hasACKClosed);

    // Close the socket
    clientSocket.closeSocket();
    printf("Session successfully terminated\n");

    // Mark end time for file transfer.
    chrono::steady_clock::time_point timeSpeedEnd = std::chrono::steady_clock::now();

    // Determine our speed
    auto timeNumMS = chrono::duration_cast<chrono::milliseconds>(timeSpeedEnd - timeSpeedStart);
    int timeNumMin = (timeNumMS.count() / 60000);

    // Calculate the total throughput (Mbps)
    double throughputBPS = (fileSize / timeNumMS.count()) * 1000;  // Bits Per Second
    double throughputMbps = (throughputBPS / 1024 / 1024) * 8; // Megabits Per Second
    
    // Calculate effective throughput
    // double effecThroughputbPS = fileSize / ((numPackets + numRetrans) * RTT);
    // double effecThroughputbPS =  (effecThroughput * 1000) * 8;   // no idea how to actually calculate this, so we gave it our best guess ??
// double effecThroughputbPS = 0.0;
    printf("\n");
    printf("Number of original packets sent: %d\n", numPackets);
    printf("Number of retransmitted packets: %d\n", numRetrans);
    printf("Total elapsed time: %lldms = ~%dmin\n", timeNumMS.count(), timeNumMin);
    printf("Total throughput (Mbps): %f\n", throughputMbps);
    // printf("Effective throughput: %f (bits/sec)\n\n", effecThroughputbPS); // TODO: Implement Effect Throughput (w/ packets)

	// DONE!
    return 0;  
} 
