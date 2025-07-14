
#define _WIN32_WINNT 0x0a00
#include <windows.h>
#include <stdio.h>

int main(int argc, char **argv)
{
  if (argc < 2)
    return 1;

  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  if (CreateProcessA(argv[1], NULL, NULL, NULL, FALSE,
        CREATE_SUSPENDED, NULL, NULL, &si, &pi))
  {
    printf("%s %s\n", argv[0], argv[1]);

    PROCESS_MACHINE_INFORMATION pmi;
    if (GetProcessInformation(pi.hProcess,
          ProcessMachineTypeInfo, &pmi, sizeof(pmi)))
      printf("GetProcessInformation(): proc=0x%x\n", pmi.ProcessMachine);
    else
      printf("GetProcessInformation() failed with error %lu\n", GetLastError());

    USHORT proc, host;
    if (IsWow64Process2(pi.hProcess, &proc, &host))
      printf("IsWow64Process2(): proc=0x%x; host=0x%x\n", proc, host);
    else
      printf("IsWow64Process2() failed with error %lu\n", GetLastError());

    ResumeThread(pi.hThread);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
  }
  else
    printf("CreateProcessA() failed with error %lu\n", GetLastError());

  return 0;
}
