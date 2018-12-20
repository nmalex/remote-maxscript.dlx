#pragma once

#include <winsock2.h>
#include <vector>

namespace maxscript_server {
	class MAXScriptServer;

	typedef   void(*MAXScriptOutputCallback)  (SOCKET, const char*);

	class MAXScriptServer {
	private:
		bool isRunning;

		SOCKET m_listenSocket;
		std::vector<SOCKET> m_clientSockets;

		char* m_recvbuf;
		const int m_recvbuflen = 65536;

		MAXScriptOutputCallback m_callback;

		void SpawnClientThread(SOCKET clientSocket);
		void CloseClients();

	public:
		static void MAXScriptServer::GetClientInfo(SOCKET clientSocket, char* ipstr, size_t ipstrSize, int* pPort);
		static void Send(SOCKET clientSocket, const char * response);

		MAXScriptServer(MAXScriptOutputCallback callback);
		virtual ~MAXScriptServer();

		int Listen(int listenPort);
		void Close();
		void DropClient(SOCKET clientSocket);
	};
}