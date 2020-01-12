#include <maxscript/maxscript.h>
#include <maxscript/foundation/numbers.h>
#include <maxscript/foundation/arrays.h>
#include <maxscript/util/listener.h>
#include <maxscript/compiler/thunks.h>

#include "maxscript.server.h"
#include "sendudp.h"

#include <locale.h>
#include <stdio.h>
#include <Windows.h>
#include <Iphlpapi.h>
#include <Assert.h>
#pragma comment(lib, "iphlpapi.lib")

#include "TCHAR.h"
#include "pdh.h"
#pragma comment(lib, "pdh.lib")

#define REMOTE_MAXSCRIPT_VERSION "1.0.1.0"

static PDH_HQUERY cpuQuery;
static PDH_HCOUNTER cpuTotal;

//todo: extract to some ini file
#define HEARTBEAT_RECEIVER_PORT 3000

void init() {
	PdhOpenQuery(NULL, NULL, &cpuQuery);
	// You can also use L"\\Processor(*)\\% Processor Time" and get individual CPU values with PdhGetFormattedCounterArray()
	PdhAddEnglishCounter(cpuQuery, L"\\Processor(_Total)\\% Processor Time", NULL, &cpuTotal);
	PdhCollectQueryData(cpuQuery);
}


double getCurrentValue() {
	PDH_FMT_COUNTERVALUE counterVal;

	PdhCollectQueryData(cpuQuery);
	PdhGetFormattedCounterValue(cpuTotal, PDH_FMT_DOUBLE, NULL, &counterVal);
	return counterVal.doubleValue;
}

char* getMAC() {
	PIP_ADAPTER_INFO AdapterInfo;
	DWORD dwBufLen = sizeof(IP_ADAPTER_INFO);
	char *mac_addr = new char[18];

	AdapterInfo = (IP_ADAPTER_INFO *)malloc(sizeof(IP_ADAPTER_INFO));
	if (AdapterInfo == NULL) {
		printf("Error allocating memory needed to call GetAdaptersinfo\n");
		delete[] mac_addr;
		return NULL; // it is safe to call free(NULL)
	}

	// Make an initial call to GetAdaptersInfo to get the necessary size into the dwBufLen variable
	if (GetAdaptersInfo(AdapterInfo, &dwBufLen) == ERROR_BUFFER_OVERFLOW) {
		free(AdapterInfo);
		AdapterInfo = (IP_ADAPTER_INFO *)malloc(dwBufLen);
		if (AdapterInfo == NULL) {
			printf("Error allocating memory needed to call GetAdaptersinfo\n");
			delete[] mac_addr;
			return NULL;
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
			// print them all, return the last one.
			// return mac_addr;

			printf("\n");
			pAdapterInfo = pAdapterInfo->Next;
		} while (pAdapterInfo);
	}
	free(AdapterInfo);
	return mac_addr; // caller must free.
}

static Value* gListenerText;

// establish a new system global variable, first are the getter & stter fns
Value*
get_listener_text()
{
	return gListenerText;
}

Value*
set_listener_text(Value* val)
{
	gListenerText = val;
	return gListenerText;
}

void RemoteMAXScriptInit()
{
	//Todo: Place initialization code here. This gets called when Maxscript goes live
	//during max startup.
	define_system_global(_T("ListenerText"), get_listener_text, set_listener_text);
}

DWORD WINAPI MyThreadFunction(LPVOID lpParam);
void ErrorHandler(LPTSTR lpszFunction);

// Sample custom data structure for threads to use.
// This is passed by void pointer so it can be any data type
// that can be passed using a single void pointer (LPVOID).
typedef struct MyData {
	int listenPort;
} MYDATA, *PMYDATA;

typedef struct HeartbeatParams {
	int listenPort;
	const char* destIp;
	const char* workgroup;
} HEARTBEAPARAMS, *PHEARTBEAPARAMS;

const char* getFileContents(const char* filename) {
	FILE * pFile;
	long lSize;
	char * buffer;
	size_t result;

	pFile = fopen(filename, "rb");
	if (pFile == NULL) { fputs("File error", stderr); exit(1); }

	// obtain file size:
	fseek(pFile, 0, SEEK_END);
	lSize = ftell(pFile);
	rewind(pFile);

	// allocate memory to contain the whole file:
	buffer = new char[lSize];
	if (buffer == NULL) { fputs("Memory error", stderr); exit(2); }

	// copy the file into the buffer:
	result = fread(buffer, 1, lSize, pFile);
	if (result != lSize) { fputs("Reading error", stderr); exit(3); }

	/* the whole file is now loaded in the memory buffer. */

	// terminate
	fclose(pFile);

	return buffer;
}

void HandleRequest(SOCKET clientSocket, const char* data);

maxscript_server::MAXScriptServer server((maxscript_server::MAXScriptOutputCallback)&HandleRequest);

DWORD WINAPI MyThreadFunction(LPVOID lpParam)
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

	server.Listen(pDataArray->listenPort);

	return 0;
}

bool serviceRunning = false;
int heartbeatCount = 0;
DWORD WINAPI HeartbeatFunction(LPVOID lpParam) {
	PHEARTBEAPARAMS pDataArray;

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_HIGHEST);

	// Cast the parameter to the correct data type.
	// The pointer is known to be valid because 
	// it was checked for NULL before the thread was created.
	pDataArray = (PHEARTBEAPARAMS)lpParam;

	int listenPort = pDataArray->listenPort;
	const char* destIp = pDataArray->destIp;
	const char* workgroup = pDataArray->workgroup;

	init();
	auto mac = getMAC();

	while (serviceRunning) {
		char msgBuf[1024];
		MEMORYSTATUSEX memInfo;
		memInfo.dwLength = sizeof(MEMORYSTATUSEX);
		GlobalMemoryStatusEx(&memInfo);
		// DWORDLONG totalVirtualMem = memInfo.ullTotalPageFile;
		// DWORDLONG virtualMemUsed = memInfo.ullTotalPageFile - memInfo.ullAvailPageFile;
		DWORDLONG totalPhysMem = memInfo.ullTotalPhys;
		DWORDLONG physMemUsed = memInfo.ullTotalPhys - memInfo.ullAvailPhys;

		DWORD pid = GetCurrentProcessId();

		auto cpuLoad = getCurrentValue();
		setlocale(LC_ALL, "C");
		int msgLen = sprintf_s(msgBuf, 1024, "{\"id\":%d, \"type\":\"heartbeat\", \"sender\":\"remote-maxscript\", \"version\":\"%s\", \"workgroup\": \"%s\", \"pid\":%d, \"mac\":\"%s\", \"port\":%d, \"cpu_usage\":%3.3f, \"ram_usage\":%3.3f, \"total_ram\":%3.3f }",
			++heartbeatCount,
			REMOTE_MAXSCRIPT_VERSION,
			workgroup,
			pid,
			mac,
			listenPort,
			cpuLoad,
			physMemUsed/1024.0f/1024/1024,
			totalPhysMem/1024.0f/1024/1024);

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

	// traceUdp("threejs.api", "ERROR", "failed");

	// Free error-handling buffer allocations.

	LocalFree(lpMsgBuf);
	LocalFree(lpDisplayBuf);
}

typedef struct MyRequest {
	SOCKET clientSocket;
	const char*  data;
} MYREQUEST, *PMYREQUEST;

void _handleRequest(UINT_PTR param) {
	PMYREQUEST pMyRequest = (PMYREQUEST)param;

	// traceUdp("_handleRequest", "DEBUG", pMyRequest->data);

	wchar_t maxscriptBuf[65536];
	mbstowcs(maxscriptBuf, pMyRequest->data, 65536);

	char responseBuf[65536];
	ExecuteMAXScriptScript(L"clearListener()");
	if (!ExecuteMAXScriptScript(maxscriptBuf)) {
		ExecuteMAXScriptScript(L"setListenerSel #(0,-1); ListenerText=getListenerSelText(); setListenerSel #(-1,-1)");
		auto s0 = gListenerText->to_string();
		wcstombs(responseBuf, s0, 65536);

		// traceUdp("HandleRequest", "ERROR", responseBuf);
		server.Send(pMyRequest->clientSocket, responseBuf);
	}
	else {
		ExecuteMAXScriptScript(L"setListenerSel #(0,-1); ListenerText=getListenerSelText(); setListenerSel #(-1,-1)");
		auto s0 = gListenerText->to_string();
		if (!s0 || wcslen(s0) == 0) {
			// traceUdp("HandleRequest", "DEBUG", "OK");
			server.Send(pMyRequest->clientSocket, "OK");
		}
		else {
			wcstombs(responseBuf, s0, 65536);
			// traceUdp("HandleRequest", "DEBUG", responseBuf);
			server.Send(pMyRequest->clientSocket, responseBuf);
		}
	}

	HeapFree(GetProcessHeap(), 0, pMyRequest);
}

#include <queue>
std::queue<PMYREQUEST> requests;

#include <mutex>
std::mutex requestsSync;

void HandleRequest(SOCKET clientSocket, const char* data) {
	// traceUdp("HandleRequest", "DEBUG", "here 1");

	auto pMyRequest = (PMYREQUEST)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MYREQUEST));

	if (pMyRequest == NULL)
	{
		// If the array allocation fails, the system is out of memory
		// so there is no point in trying to print an error message.
		// Just terminate execution.
		// traceUdp("HandleRequest", "ERROR", "HeapAlloc failed");
		return;
	}

	pMyRequest->clientSocket = clientSocket;
	pMyRequest->data = data;

	requestsSync.lock();
	requests.push(pMyRequest);
	requestsSync.unlock();
}

#include <atomic>
std::atomic<bool> operationPending = false;
void handleTimerFunc(HWND hwnd, UINT param1, UINT_PTR param2, DWORD param3)
{
	requestsSync.lock();

	if (operationPending || requests.empty()) {
		requestsSync.unlock();
		return;
	}
	else {
		operationPending = true;

		auto request = requests.front();
		requests.pop();

		requestsSync.unlock();

		_handleRequest((UINT_PTR)request);

		operationPending = false;
	}
}

// Declare C++ function and register it with MAXScript
#include <maxscript\macros\define_instantiation_functions.h>
	def_visible_primitive(threejsApiStart, "threejsApiStart");
	def_visible_primitive(threejsApiStop, "threejsApiStop");

bool StartHeartbeatThread(int listenPort, const char* destIp, const char* workgroup) {

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
	pDataArray->workgroup = workgroup;

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

bool StartServerThread(int listenPort) {
	PMYDATA pDataArray = (PMYDATA)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(MYDATA));

	if (pDataArray == NULL)
	{
		// If the array allocation fails, the system is out of memory
		// so there is no point in trying to print an error message.
		// Just terminate execution.
		return false;
	}

	// Generate unique data for each thread to work with.
	pDataArray->listenPort = listenPort;

	// Create the thread to begin execution on its own.

	DWORD dwThreadIdArray;
	HANDLE hThreadArray = CreateThread(
		NULL,                   // default security attributes
		0,                      // use default stack size  
		MyThreadFunction,       // thread function name
		pDataArray,             // argument to thread function 
		0,                      // use default creation flags 
		&dwThreadIdArray);      // returns the thread identifier 


								// Check the return value for success.
								// If CreateThread fails, terminate execution. 
								// This will automatically clean up threads and memory. 

	if (hThreadArray == NULL)
	{
		ErrorHandler(TEXT("CreateThread"));
		return false;
	}

	return true;
}

Value* threejsApiStart_cf(Value **arg_list, int count)
{
	if (serviceRunning) {
		return &false_value;
	}

	check_arg_count(cmdexRun2, 3, count);
	Value* pListenPort = arg_list[0];
	Value* pDestIp = arg_list[1];
	Value* pWorkgroup = arg_list[2];

	//First example of how to type check an argument
	if (!(is_number(pListenPort)))
	{
		throw RuntimeError(_T("Expected a Number for the first argument pListenPort"));
	}

	//First example of how to type check an argument
	if (!(is_string(pDestIp)))
	{
		throw RuntimeError(_T("Expected a String for the second argument pDestIp"));
	}

	//First example of how to type check an argument
	if (!(is_string(pWorkgroup)))
	{
		throw RuntimeError(_T("Expected a String for the third argument pWorkgroup"));
	}

	heartbeatCount = 0;
	serviceRunning = true;

	int listenPort = pListenPort->to_int();

	if (!StartServerThread(listenPort)) {
		serviceRunning = false;
		return &false_value;
	}

	const wchar_t* wDestIp = pDestIp->to_string();
	char* destIp = new char[256];
	wcstombs(destIp, wDestIp, 256);

	const wchar_t* wWorkgroup = pWorkgroup->to_string();
	char* workgroup = new char[256];
	wcstombs(workgroup, wWorkgroup, 256);

	if (!StartHeartbeatThread(listenPort, destIp, workgroup)) {
		server.Close();
		serviceRunning = false;
		return &false_value;
	}

	SetTimer(GetCOREInterface()->GetMAXHWnd(), 777, 25, (TIMERPROC)handleTimerFunc);

	return &true_value;
}

Value* threejsApiStop_cf(Value **arg_list, int count)
{
	if (!serviceRunning) {
		return &false_value;
	}

	KillTimer(GetCOREInterface()->GetMAXHWnd(), 777);

	server.Close();
	// traceUdp("threejs.api", "DEBUG", "api stop");

	serviceRunning = false;
	return &true_value;
}