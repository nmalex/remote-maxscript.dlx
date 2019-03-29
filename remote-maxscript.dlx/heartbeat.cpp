#include "heartbeat.h"

#include "sendudp.h"

namespace heartbeat {

	heartbeat::heartbeat()
	{
		this->m_macAddr = new WCHAR[32];

		PdhOpenQuery(NULL, NULL, &this->m_cpuQuery);
		PdhAddEnglishCounter(this->m_cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &this->m_cpuTotal);
		PdhCollectQueryData(this->m_cpuQuery);
	}

	heartbeat::~heartbeat()
	{
		delete[] this->m_macAddr;
	}

	double heartbeat::getCurrentValue() {
		PDH_FMT_COUNTERVALUE counterVal;
		PdhCollectQueryData(this->m_cpuQuery);
		PdhGetFormattedCounterValue(this->m_cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
		return counterVal.doubleValue;
	}

	void heartbeat::getMac(LPTSTR mac_addr, int bufferSize) {
		PIP_ADAPTER_INFO AdapterInfo;
		DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);

		AdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
		if (AdapterInfo == NULL) {
			printf("Error allocating memory needed to call GetAdaptersinfo\n");
			mac_addr[0] = '\0';
			return; // it is safe to call free(NULL)
		}

		// Make an initial call to GetAdaptersInfo to get the necessary size into the dwBufLen variable
		if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
			free(AdapterInfo);
			AdapterInfo = (IP_ADAPTER_INFO *)malloc(dwBufLen);
			if (AdapterInfo == NULL) {
				printf("Error allocating memory needed to call GetAdaptersinfo\n");
				mac_addr[0] = '\0';
				return;
			}
		}

		if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == NO_ERROR) {
			// Contains pointer to current adapter info
			PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;
			do {
				// technically should look at pAdapterInfo->AddressLength
				//   and not assume it is 6.
				sprintf(mac_addr, "%02x%02x%02x%02x%02x%02x",
					pAdapterInfo->Address[0], pAdapterInfo->Address[1],
					pAdapterInfo->Address[2], pAdapterInfo->Address[3],
					pAdapterInfo->Address[4], pAdapterInfo->Address[5]);

				printf("Address: %s, mac: %s\n", pAdapterInfo->IpAddressList.IpAddress.String, mac_addr);

				pAdapterInfo = pAdapterInfo->Next;
			} while (pAdapterInfo);
		}
		free(AdapterInfo);
	}

	bool StartHeartbeatThread(int listenPort, LPTSTR destIp) {

		PHEARTBEAPARAMS pDataArray = (PHEARTBEAPARAMS)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HEARTBEAPARAMS));

		if (pDataArray == NULL)
		{
			// If the array allocation fails, the system is out of memory
			// so there is no point in trying to print an error message.
			// Just terminate execution.
			return false;
		}

		// Generate unique data for each thread to work with.
		pDataArray->destIp = destIp;
		pDataArray->listenPort = listenPort;

		DWORD dwThreadIdArray;
		HANDLE hThreadArray = CreateThread(
			NULL,                   // default security attributes
			0,                      // use default stack size  
			HeartbeatFunction,      // thread function name
			pDataArray,             // argument to thread function 
			0,                      // use default creation flags 
			&dwThreadIdArray);      // returns the thread identifier 

		if (hThreadArray == NULL)
		{
			ErrorHandler(TEXT("CreateThread"));
			return false;
		}

		return true;
	}

	DWORD WINAPI HeartbeatFunction(LPVOID lpParam) {
		PHEARTBEAPARAMS pHeartbeatParams;

		SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

		// Cast the parameter to the correct data type.
		// The pointer is known to be valid because 
		// it was checked for NULL before the thread was created.
		pHeartbeatParams = (PHEARTBEAPARAMS)lpParam;

		int listenPort = pHeartbeatParams->listenPort;
		LPTSTR destIp = pHeartbeatParams->destIp;

		LPTSTR mac = pHeartbeatParams->pHeartbeat->getMac();

		DWORD pid = GetCurrentProcessId();

		int heartbeatCount = 0;

		while (true) { // todo: // let it be stopped
			char msgBuf[1024];

			MEMORYSTATUSEX memInfo;
			memInfo.dwLength = sizeof(MEMORYSTATUSEX);
			GlobalMemoryStatusEx(&memInfo);

			DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
			DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

			auto cpuLoad = pHeartbeatParams->pHeartbeat->getCurrentValue();

			setlocale(LC_ALL, "C");
			int msgLen = sprintf_s(msgBuf, 1024, "{\"id\":%d, \"type\":\"heartbeat\", \"sender\":\"remote-maxscript\", \"version\":\"%s\", \"pid\":%d, \"mac\":\"%s\", \"port\":%d, \"cpu_usage\":%3.3f, \"ram_usage\":%3.3f, \"total_ram\":%3.3f }",
				++heartbeatCount,
				REMOTE_MAXSCRIPT_VERSION,
				pid,
				mac,
				listenPort,
				cpuLoad,
				physMemUsed / 1024.0f / 1024 / 1024,
				totalPhysMem / 1024.0f / 1024 / 1024);

			sendUdp(destIp, HEARTBEAT_RECEIVER_PORT, msgBuf, msgLen);

			Sleep(1000);
		}

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

		// Free error-handling buffer allocations.
		LocalFree(lpMsgBuf);
		LocalFree(lpDisplayBuf);
	}
}