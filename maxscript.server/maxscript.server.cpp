#include "maxscript.server.h"

#undef UNICODE

#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>

#include <ws2tcpip.h>
#include <stdlib.h>
#include <stdio.h>

#include <xutility>

#define LOGGER_NAME ("MAXScriptServer::Listen")

// Need to link with Ws2_32.lib
#pragma comment (lib, "Ws2_32.lib")
// #pragma comment (lib, "Mswsock.lib")

namespace maxscript_server {

	// DWORD WINAPI MyThreadFunction(LPVOID lpParam);
	void ErrorHandler(LPTSTR lpszFunction);

	// Data structure for threads to use.
	// This is passed by void pointer so it can be any data type
	// that can be passed using a single void pointer (LPVOID).
	typedef struct MyData {
		MAXScriptServer* pServer;
		MAXScriptOutputCallback callback;
		SOCKET clientSocket;
	} MYDATA, *PMYDATA;

	DWORD WINAPI ClientWorker(LPVOID lpParam)
	{
		HANDLE hStdout;
		PMYDATA pDataArray;

		// Make sure there is a console to receive output results. 
		hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
		if (hStdout == INVALID_HANDLE_VALUE)
			return 1;

		// Cast the parameter to the correct data type.
		// The pointer is known to be valid because 
		// it was checked for NULL before the thread was created.
		pDataArray = (PMYDATA)lpParam;
		auto clientSocket = pDataArray->clientSocket;
		auto callback = pDataArray->callback;
		auto pServer = pDataArray->pServer;

		int iSendResult;
		int iResult;

		const int recvbuflen = 65536;
		char* recvbuf = new char[recvbuflen];

		const int msgBufLen = 1024;
		char msgBuf[msgBufLen];

		do {
			iResult = recv(clientSocket, recvbuf, recvbuflen - 1, 0);

			if (iResult > 0) {
				recvbuf[min(iResult, recvbuflen - 1)] = '\0';
				callback(clientSocket, recvbuf);
			}
			else if (iResult == 0) {
				//logDebug(LOGGER_NAME, "Connection closing...");
			} else {
				sprintf_s(msgBuf, msgBufLen, "Client disconnected: %d", WSAGetLastError());
				//logDebug(LOGGER_NAME, msgBuf);
				pServer->DropClient(clientSocket);
				return 1;
			}

		} while (iResult > 0);

		return 0;
	}

	void ErrorHandler(LPTSTR lpszFunction)
	{
		// Retrieve the system error message for the last-error code.

		LPVOID lpMsgBuf;
		LPVOID lpDisplayBuf;
		DWORD dw = GetLastError();

		FormatMessage(
			FORMAT_MESSAGE_ALLOCATE_BUFFER |
			FORMAT_MESSAGE_FROM_SYSTEM |
			FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL,
			dw,
			MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
			(LPTSTR)&lpMsgBuf,
			0, NULL);

		// Display the error message.

		lpDisplayBuf = (LPVOID)LocalAlloc(LMEM_ZEROINIT,
			(lstrlen((LPCTSTR)lpMsgBuf) + lstrlen((LPCTSTR)lpszFunction) + 40) * sizeof(TCHAR));

		// traceUdp("threejs.api", "ERROR", "failed");

		// Free error-handling buffer allocations.

		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);
	}

	MAXScriptServer::MAXScriptServer(MAXScriptOutputCallback callback) {
		this->m_callback = callback;

		this->m_listenSocket = INVALID_SOCKET;

		this->m_recvbuf = new char[m_recvbuflen];
	}

	MAXScriptServer::~MAXScriptServer() {
		delete[] this->m_recvbuf;
	}

	int MAXScriptServer::Listen(int port) {

		WSADATA wsaData;
		int iResult;

		this->m_listenSocket = INVALID_SOCKET;

		struct addrinfo *result = NULL;
		struct addrinfo hints;

		const int msgBufLen = 1024;
		char msgBuf[msgBufLen];

		// Initialize Winsock
		iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
		if (iResult != 0) {
			sprintf_s(msgBuf, msgBufLen, "WSAStartup failed with error: %d", iResult);
			//logError(LOGGER_NAME, msgBuf);
			return 1;
		}

		ZeroMemory(&hints, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_protocol = IPPROTO_TCP;
		hints.ai_flags = AI_PASSIVE;

		// Resolve the server address and port
		char pServiceName[256];
		sprintf_s(pServiceName, 256, "%d", port);
		iResult = getaddrinfo(NULL, pServiceName, &hints, &result);
		if (iResult != 0) {
			sprintf_s(msgBuf, "getaddrinfo failed with error: %d", iResult);
			//logError(LOGGER_NAME, msgBuf);
			WSACleanup();
			return 1;
		}

		// Create a SOCKET for connecting to server
		this->m_listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
		if (this->m_listenSocket == INVALID_SOCKET) {
			sprintf_s(msgBuf, msgBufLen, "socket failed with error: %ld", WSAGetLastError());
			//logError(LOGGER_NAME, msgBuf);
			freeaddrinfo(result);
			WSACleanup();
			return 1;
		}

		// Setup the TCP listening socket
		iResult = bind(this->m_listenSocket, result->ai_addr, (int)result->ai_addrlen);
		if (iResult == SOCKET_ERROR) {
			sprintf_s(msgBuf, msgBufLen, "bind failed with error: %d", WSAGetLastError());
			//logError(LOGGER_NAME, msgBuf);
			freeaddrinfo(result);
			closesocket(this->m_listenSocket);
			WSACleanup();
			return 1;
		}

		freeaddrinfo(result);

		this->isRunning = true;

		iResult = listen(this->m_listenSocket, SOMAXCONN);
		if (iResult == SOCKET_ERROR) {
			sprintf_s(msgBuf, msgBufLen, "listen failed with error: %d", WSAGetLastError());
			//logError(LOGGER_NAME, msgBuf);
			closesocket(this->m_listenSocket);
			WSACleanup();
			return 1;
		}

		sprintf_s(msgBuf, msgBufLen, "Listening on port: %d\n", port);
		//logDebug(LOGGER_NAME, msgBuf);

		while (this->isRunning) {
			// Accept a client socket
			SOCKET clientSocket = accept(this->m_listenSocket, NULL, NULL);
			if (clientSocket == INVALID_SOCKET) {
				sprintf_s(msgBuf, msgBufLen, "accept failed with error: %d", WSAGetLastError());
				//logError(LOGGER_NAME, msgBuf);
				continue;
			}
			else {
				this->m_clientSockets.push_back(clientSocket);

				char ipstr[256];
				int clientport;
				MAXScriptServer::GetClientInfo(clientSocket, ipstr, 256, &clientport);
				sprintf_s(msgBuf, msgBufLen, "Accepted new client: %s:%d", ipstr, clientport);
				//logDebug(LOGGER_NAME, msgBuf);
			}

			this->SpawnClientThread(clientSocket);
		}

		// No longer need server socket
		closesocket(this->m_listenSocket);

		// shutdown all client connections since we're done
		this->CloseClients();

		WSACleanup();

		sprintf_s(msgBuf, msgBufLen, "Listener stopped");
		//logDebug(LOGGER_NAME, msgBuf);

		return 0;
	}

	void MAXScriptServer::SpawnClientThread(SOCKET clientSocket)
	{
		PMYDATA pDataArray;
		DWORD   dwThreadIdArray;
		HANDLE  hThreadArray;

		pDataArray = (PMYDATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MYDATA));

		if (pDataArray == NULL)
		{
			// If the array allocation fails, the system is out of memory
			// so there is no point in trying to print an error message.
			// Just terminate execution.
			return;
		}

		// Generate unique data for each thread to work with.

		pDataArray->clientSocket = clientSocket;
		pDataArray->callback = this->m_callback;
		pDataArray->pServer = this;

		// Create the thread to begin execution on its own.

		hThreadArray = CreateThread(
			NULL,                   // default security attributes
			0,                      // use default stack size  
			ClientWorker,           // thread function name
			pDataArray,             // argument to thread function 
			0,                      // use default creation flags 
			&dwThreadIdArray);      // returns the thread identifier 


									// Check the return value for success.
									// If CreateThread fails, terminate execution. 
									// This will automatically clean up threads and memory. 

		if (hThreadArray == NULL)
		{
			const int msgBufLen = 1024;
			char msgBuf[msgBufLen];
			sprintf_s(msgBuf, msgBufLen, "CreateThread failed with error: %d", GetLastError());
			//logError(LOGGER_NAME, msgBuf);
			return;
		}
	}

	void MAXScriptServer::Send(SOCKET clientSocket, const char* response) {
		//send the buffer back to the sender

		int iSendResult = send(clientSocket, response, strlen(response), 0);
		if (iSendResult == SOCKET_ERROR) {
			const int msgBufLen = 1024;
			char msgBuf[msgBufLen];
			sprintf_s(msgBuf, msgBufLen, "send failed with error: %d\n", WSAGetLastError());
			//logError(LOGGER_NAME, msgBuf);
			closesocket(clientSocket);
			return;
		}
	}

	void MAXScriptServer::GetClientInfo(SOCKET clientSocket, char* ipstr, size_t ipstrSize, int* pPort) {
		struct sockaddr client_info;
		int addrsize = sizeof(client_info);

		// or get it from the socket itself at any time
		//getpeername(clientSocket, &client_info, &addrsize);

		struct sockaddr_in addr;
		socklen_t addr_len = sizeof(addr);
		int err = getpeername(clientSocket, (struct sockaddr *) &addr, &addr_len);
		if (err != 0) {
			// error
		}

		if (addr.sin_family == AF_INET) {
			struct sockaddr_in *s = (struct sockaddr_in *)&addr;
			*pPort = ntohs(s->sin_port);
			inet_ntop(AF_INET, &s->sin_addr, ipstr, ipstrSize);
		}
		else {
			struct sockaddr_in6 *s = (struct sockaddr_in6 *)&addr;
			*pPort = ntohs(s->sin6_port);
			inet_ntop(AF_INET6, &s->sin6_addr, ipstr, ipstrSize);
		}
	}

	void MAXScriptServer::DropClient(SOCKET clientSocket) {
		const int msgBufLen = 1024;
		char msgBuf[msgBufLen];
		auto it = std::find<std::vector<SOCKET>::iterator, SOCKET>(this->m_clientSockets.begin(), this->m_clientSockets.end(), clientSocket);
		if (it != this->m_clientSockets.end()) {
			closesocket(*it);
			this->m_clientSockets.erase(it);
		}
	}

	void MAXScriptServer::CloseClients() {
		const int msgBufLen = 1024;
		char msgBuf[msgBufLen];

		for (auto it = this->m_clientSockets.begin(); it != this->m_clientSockets.end(); ++it) {
			auto clientSocket = *it;

			auto iResult = shutdown(clientSocket, SD_SEND);
			if (iResult == SOCKET_ERROR) {
				sprintf_s(msgBuf, msgBufLen, "shutdown failed with error: %d", WSAGetLastError());
				//logError(LOGGER_NAME, msgBuf);
				closesocket(clientSocket);
			}
		}

		m_clientSockets.clear();
	}

	void MAXScriptServer::Close() {
		this->isRunning = false;

		if (this->m_listenSocket != INVALID_SOCKET) {
			closesocket(this->m_listenSocket);
			this->m_listenSocket = INVALID_SOCKET;
		}

		WSACleanup();
	}

}