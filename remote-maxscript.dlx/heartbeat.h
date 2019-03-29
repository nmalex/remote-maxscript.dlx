#pragma once

#include <locale.h>
#include <stdio.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <Assert.h>
#pragma comment(lib, "iphlpapi.lib")

#include "pdh.h"
#pragma comment(lib, "pdh.lib")

#define REMOTE_MAXSCRIPT_VERSION "1.0.0.0"
#define HEARTBEAT_RECEIVER_PORT 3000

namespace heartbeat {

	class heartbeat;

	typedef struct HeartbeatParams {
		int listenPort;
		LPTSTR destIp;
		heartbeat* pHeartbeat;
	} HEARTBEAPARAMS, *PHEARTBEAPARAMS;

	DWORD WINAPI HeartbeatFunction(LPVOID lpParam);
	void ErrorHandler(LPTSTR lpszFunction);

	class heartbeat
	{
		LPTSTR m_macAddr;
		PDH_HQUERY m_cpuQuery;
		PDH_HCOUNTER m_cpuTotal;

		static void heartbeat::getMac(LPTSTR mac_addr, int bufferSize);
	public:
		heartbeat();
		virtual ~heartbeat();

		const LPTSTR getMac() const {
			return this->m_macAddr;
		}

		double heartbeat::getCurrentValue();
	};
}