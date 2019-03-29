#include <winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>
#include <chrono>
#include <string>
#include <sstream>

// Link with ws2_32.lib
#pragma comment(lib, "Ws2_32.lib")

const char* getTimestampStr() {
	using namespace std::chrono;
	milliseconds ms = duration_cast<milliseconds>(system_clock::now().time_since_epoch());
	__int64 value = ms.count();
	char* timestamp = new char[256];
	_i64toa_s(value, timestamp, 256, 10);
	return timestamp;
}

int sendUdp(LPSTR host, USHORT port, const BYTE* buf, int len) {
	int iResult;
	WSADATA wsaData;

	SOCKET SendSocket = INVALID_SOCKET;
	sockaddr_in RecvAddr;

	//----------------------
	// Initialize Winsock
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != NO_ERROR) {
		wprintf(L"WSAStartup failed with error: %d\n", iResult);
		return 1;
	}

	//---------------------------------------------
	// Create a socket for sending data
	SendSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if (SendSocket == INVALID_SOCKET) {
		wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	//---------------------------------------------
	// Set up the RecvAddr structure with the IP address of
	// the receiver (in this example case "192.168.1.1")
	// and the specified port number.
	RecvAddr.sin_family = AF_INET;
	RecvAddr.sin_port = htons(port);
	RecvAddr.sin_addr.s_addr = inet_addr(host);

	//---------------------------------------------
	// Send a datagram to the receiver
	iResult = sendto(SendSocket,
		buf, len, 0, (SOCKADDR *)& RecvAddr, sizeof(RecvAddr));
	if (iResult == SOCKET_ERROR) {
		wprintf(L"sendto failed with error: %d\n", WSAGetLastError());
		closesocket(SendSocket);
		WSACleanup();
		return 1;
	}

	//---------------------------------------------
	// When the application is finished sending, close the socket.
	//wprintf(L"Finished sending. Closing socket.\n");
	iResult = closesocket(SendSocket);
	if (iResult == SOCKET_ERROR) {
		wprintf(L"closesocket failed with error: %d\n", WSAGetLastError());
		WSACleanup();
		return 1;
	}

	//---------------------------------------------
	// Clean up and quit.
	//wprintf(L"Exiting.\n");
	WSACleanup();

	return 0;
}
