#include "pch.h"

#include "Tools.h"

std::wstring InjectionModeToString(INJECTION_MODE mode);
std::wstring LaunchMethodToString(LAUNCH_METHOD method);

bool FileExists(const wchar_t * szFile)
{
	return (GetFileAttributesW(szFile) != INVALID_FILE_ATTRIBUTES);
}

DWORD ValidateFile(const wchar_t * szFile, DWORD desired_machine)
{
	std::ifstream File(szFile, std::ios::binary | std::ios::ate);
	if (!File.good())
	{
		return FILE_ERR_CANT_OPEN_FILE;
	}

	auto FileSize = File.tellg();
	if (FileSize < 0x1000)
	{
		return FILE_ERR_INVALID_FILE_SIZE;
	}

	BYTE * headers = new BYTE[0x1000];
	File.seekg(0, std::ios::beg);
	File.read(ReCa<char*>(headers), 0x1000);
	File.close();

	auto * pDos = ReCa<IMAGE_DOS_HEADER*>(headers);
	auto * pNT	= ReCa<IMAGE_NT_HEADERS*>(headers + pDos->e_lfanew); //no need for correct nt headers type

	WORD magic		= pDos->e_magic;
	DWORD signature = pNT->Signature;
	WORD machine	= pNT->FileHeader.Machine;

	delete[] headers;

	if (magic != 0x5A4D || signature != 0x4550 || machine != desired_machine) //"MZ" & "PE"
	{
		return FILE_ERR_INVALID_FILE;
	}

	return 0;
}

bool GetOwnModulePathA(char * pOut, size_t BufferCchSize)
{
	DWORD mod_ret = GetModuleFileNameA(g_hInjMod, pOut, (DWORD)BufferCchSize);
	if (!mod_ret || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		return false;
	}

	HRESULT hr = StringCchLengthA(pOut, BufferCchSize, &BufferCchSize);
	if (FAILED(hr) || !BufferCchSize)
	{
		return false;
	}

	pOut += BufferCchSize;
	while (*(--pOut - 1) != '\\');
	*pOut = '\0';

	return true;
}

bool GetOwnModulePathW(wchar_t * pOut, size_t BufferCchSize)
{
	DWORD mod_ret = GetModuleFileNameW(g_hInjMod, pOut, (DWORD)BufferCchSize);
	if (!mod_ret || GetLastError() == ERROR_INSUFFICIENT_BUFFER)
	{
		return false;
	}

	HRESULT hr = StringCchLengthW(pOut, BufferCchSize, &BufferCchSize);
	if (FAILED(hr) || !BufferCchSize)
	{
		return false;
	}

	pOut += BufferCchSize;
	while (*(--pOut - 1) != '\\');
	*pOut = '\0';

	return true;	
}

bool IsNativeProcess(HANDLE hTargetProc)
{
	BOOL bWOW64 = FALSE;
	IsWow64Process(hTargetProc, &bWOW64);

	return (bWOW64 == FALSE);
}

ULONG GetSessionId(HANDLE hTargetProc, NTSTATUS & ntRetOut)
{	
	if (!NT::NtQueryInformationProcess)
	{
		return (ULONG)-1;
	}

	PROCESS_SESSION_INFORMATION psi{ 0 };
	ntRetOut = NT::NtQueryInformationProcess(hTargetProc, PROCESSINFOCLASS::ProcessSessionInformation, &psi, sizeof(psi), nullptr);
	if (NT_FAIL(ntRetOut))
	{
		return (ULONG)-1;
	}

	return psi.SessionId;
}

bool IsElevatedProcess(HANDLE hTargetProc)
{
	HANDLE hToken = nullptr;
	if (!OpenProcessToken(hTargetProc, TOKEN_QUERY, &hToken))
	{
		return false;
	}

	TOKEN_ELEVATION te{ 0 };
	DWORD SizeOut = 0;
	GetTokenInformation(hToken, TokenElevation, &te, sizeof(te), &SizeOut);

	CloseHandle(hToken);
	
	return (te.TokenIsElevated != 0);
}

void ErrorLog(ERROR_INFO * info)
{
	wchar_t pPath[MAX_PATH * 2]{ 0 };
	if (!GetOwnModulePathW(pPath, sizeof(pPath) / sizeof(pPath[0])))
	{
		return;
	}

	wchar_t ErrorLogName[] = L"GH_Inj_Log.txt";

	wchar_t FullPath[MAX_PATH]{ 0 };
	StringCbCopyW(FullPath, sizeof(FullPath), pPath);
	StringCbCatW(FullPath, sizeof(FullPath), ErrorLogName);
		
	time_t time_raw	= time(nullptr);
	tm time_info;
	localtime_s(&time_info, &time_raw);
	wchar_t szTime[30]{ 0 };
	wcsftime(szTime, 30, L"%d-%m-%Y %H:%M:%S", &time_info);

	wchar_t szWinProductName	[100]{ 0 };
	wchar_t szWinReleaseId		[100]{ 0 };
	wchar_t szWinCurrentBuild	[100]{ 0 };

	HKEY hKey = nullptr;
	LSTATUS reg_status = RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", NULL, KEY_READ, &hKey);
	if (reg_status == ERROR_SUCCESS)
	{
		DWORD Type = REG_SZ;

		DWORD SizeOut = sizeof(szWinProductName);
		RegQueryValueExW(hKey, L"ProductName",	nullptr, &Type, ReCa<BYTE*>(szWinProductName),	&SizeOut);

		SizeOut = sizeof(szWinReleaseId);
		RegQueryValueExW(hKey, L"ReleaseId",	nullptr, &Type, ReCa<BYTE*>(szWinReleaseId),	&SizeOut);

		SizeOut = sizeof(szWinCurrentBuild);
		RegQueryValueExW(hKey, L"CurrentBuild", nullptr, &Type, ReCa<BYTE*>(szWinCurrentBuild), &SizeOut);

		RegCloseKey(hKey);
	}

	wchar_t szFlags			[9]{ 0 };
	wchar_t szErrorCode		[9]{ 0 };
	wchar_t szAdvErrorCode	[9]{ 0 };
	wchar_t szHandleValue	[9]{ 0 };
	StringCchPrintfW(szFlags,			9, L"%08X", info->Flags);
	StringCchPrintfW(szErrorCode,		9, L"%08X", info->ErrorCode);
	StringCchPrintfW(szAdvErrorCode,	9, L"%08X", info->AdvErrorCode);
	StringCchPrintfW(szHandleValue,		9, L"%08X", info->HandleValue);
	
	std::wofstream error_log(FullPath, std::ios_base::out | std::ios_base::app);
	if (!error_log.good())
	{
		return;
	}

	error_log << szTime																															<< std::endl;
	error_log << L"Version            : "	<< L"GH Injector V" << GH_INJ_VERSION																<< std::endl;
	error_log << L"OS                 : "	<< szWinProductName << L" " << szWinReleaseId << L" (Build " << szWinCurrentBuild << L")"			<< std::endl;
	error_log << L"File               : "	<< (info->szDllFileName ? info->szDllFileName : L"(nullptr)")										<< std::endl;
	error_log << L"Target             : "	<< (info->szTargetProcessExeFileName[0] ? info->szTargetProcessExeFileName : L"(undetermined)")		<< std::endl;
	error_log << L"Target PID         : "	<< info->TargetProcessId																			<< std::endl;
	error_log << L"Source             : "	<< info->szSourceFile << L" in " << info->szFunctionName << L" at line " << info->Line				<< std::endl;
	error_log << L"Errorcode          : 0x"	<< szErrorCode																						<< std::endl;
	error_log << L"Advanced errorcode : 0x"	<< szAdvErrorCode																					<< std::endl;
	error_log << L"Injectionmode      : "	<< InjectionModeToString(info->InjectionMode)														<< std::endl;
	error_log << L"Launchmethod       : "	<< LaunchMethodToString(info->LaunchMethod)															<< std::endl;
	error_log << L"Platform           : "	<< (info->bNative > 0 ? L"x64/x86 (native)" : (info->bNative == 0 ? L"wow64" : L"---"))				<< std::endl;
	error_log << L"HandleValue        : 0x"	<< szHandleValue																					<< std::endl;
	error_log << L"Flags              : 0x"	<< szFlags																							<< std::endl;
	error_log << std::endl;

	error_log.close();
}

std::wstring InjectionModeToString(INJECTION_MODE mode)
{
	switch (mode)
	{
		case INJECTION_MODE::IM_LoadLibraryExW:
			return std::wstring(L"LoadLibraryExW");

		case INJECTION_MODE::IM_LdrLoadDll:
			return std::wstring(L"LdrLoadDll");

		case INJECTION_MODE::IM_LdrpLoadDll:
			return std::wstring(L"LdrpLoadDll");

		case INJECTION_MODE::IM_ManualMap:
			return std::wstring(L"ManualMap");

		default:
			break;
	}

	return std::wstring(L"bruh moment");
}

std::wstring LaunchMethodToString(LAUNCH_METHOD method)
{
	switch (method)
	{
		case LAUNCH_METHOD::LM_NtCreateThreadEx:
			return std::wstring(L"NtCreateThreadEx");

		case LAUNCH_METHOD::LM_HijackThread:
			return std::wstring(L"HijackThread");

		case LAUNCH_METHOD::LM_SetWindowsHookEx:
			return std::wstring(L"SetWindowsHookEx");

		case LAUNCH_METHOD::LM_QueueUserAPC:
			return std::wstring(L"QueueUserAPC");

		default:
			break;
	}

	return std::wstring(L"bruh moment");
}