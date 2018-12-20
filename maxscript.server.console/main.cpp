#include "stdafx.h"

#include "maxscript.server.h"

using namespace maxscript_server;

void HandleRequest(SOCKET clientSocket, const char* data) {
	printf("Received: %s\n", data);
	MAXScriptServer::Send(clientSocket, "OK");
}

int main(int argc, char **argv)
{
	MAXScriptServer server((MAXScriptOutputCallback)&HandleRequest);
	server.Listen(29207);
	return 0;
}

