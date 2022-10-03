#include "Image.h"
#include "Inject.h"
#include "apc.h"


UNICODE_STRING g_kernel32 = RTL_CONSTANT_STRING(L"\\SystemRoot\\System32\\kernel32.dll");
wchar_t g_Ntkernel32[MAX_PATH] = {0};
UNICODE_STRING g_Ntkernel32Path = {0};
wchar_t g_DosKernel32[MAX_PATH] = {0};
UNICODE_STRING g_DosKernel32Path = {0};

UNICODE_STRING g_kernelWow64 = RTL_CONSTANT_STRING(L"\\SystemRoot\\SysWOW64\\kernel32.dll");
wchar_t g_NtkernelWow64[MAX_PATH] = {0};
UNICODE_STRING g_NtkernelWow64Path = {0};
wchar_t g_DosKernelWow64[MAX_PATH] = {0};
UNICODE_STRING g_DosKernelWow64Path = {0};

SIZE_T LoadLibraryExWFn;//某个进程中的L"\\SystemRoot\\System32\\kernel32.dll"里的LoadLibraryExW地址。
#ifdef _WIN64
SIZE_T LoadLibraryExWWow64Fn;//某个WOW64进程中的L"\\SystemRoot\\SysWOW64\\kernel32.dll"里的LoadLibraryExW地址。
#endif


SIZE_T LoadLibraryWFn;//某个进程中的L"\\SystemRoot\\System32\\kernel32.dll"里的LoadLibraryW地址。
#ifdef _WIN64
SIZE_T LoadLibraryWWow64Fn;//某个WOW64进程中的L"\\SystemRoot\\SysWOW64\\kernel32.dll"里的LoadLibraryW地址。
#endif


//////////////////////////////////////////////////////////////////////////////////////////////////


void GetKernel32FullPath()
{
    RtlInitUnicodeString(&g_Ntkernel32Path, g_Ntkernel32);
    g_Ntkernel32Path.MaximumLength = sizeof(g_Ntkernel32);
    RtlInitUnicodeString(&g_DosKernel32Path, g_DosKernel32);
    g_DosKernel32Path.MaximumLength = sizeof(g_DosKernel32);

    GetSystemRootPathName(&g_kernel32, &g_Ntkernel32Path, &g_DosKernel32Path);

#ifdef _WIN64
    RtlInitUnicodeString(&g_NtkernelWow64Path, g_NtkernelWow64);
    g_NtkernelWow64Path.MaximumLength = sizeof(g_NtkernelWow64);
    RtlInitUnicodeString(&g_DosKernelWow64Path, g_DosKernelWow64);
    g_DosKernelWow64Path.MaximumLength = sizeof(g_DosKernelWow64);

    GetSystemRootPathName(&g_kernelWow64, &g_NtkernelWow64Path, &g_DosKernelWow64Path);
#endif
}


BOOL IsLoadKernel32(HANDLE UniqueProcess)
/*
功能：判断一个进程（包括WOW64)是否加载kernel32.dll。

标准的办法是用一个进程上下文在进程回调和IMAGE回调里做统计。

这里用一个简单的办法，即：读取LoadLibraryEx的第一个指令的第一个字节，看看会否发生异常。
*/
{
    PPS_APC_ROUTINE UserRoutine = (PPS_APC_ROUTINE)LoadLibraryExWFn;
    BOOL ret = FALSE;
    NTSTATUS Status;
    PEPROCESS  Process = NULL;
    HANDLE  KernelHandle = NULL;
    KAPC_STATE   ApcState;

    if (IsWow64Process(UniqueProcess)) {
        UserRoutine = (PPS_APC_ROUTINE)LoadLibraryExWWow64Fn;
    }

    __try {
        Status = PsLookupProcessByProcessId(UniqueProcess, &Process);
        if (!NT_SUCCESS(Status)) {
            Print(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "0x%#x", Status);
            __leave;
        }

        Status = ObOpenObjectByPointer(Process,
                                       OBJ_KERNEL_HANDLE,
                                       NULL,
                                       GENERIC_READ,
                                       *PsProcessType,
                                       KernelMode,
                                       &KernelHandle);
        if (!NT_SUCCESS(Status)) {
            Print(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "0x%#x", Status);
            __leave;
        }

        KeStackAttachProcess(Process, &ApcState);

        ret = TRUE;

        __try {
            PCHAR Temp = (PCHAR)*(PCHAR)UserRoutine;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            ret = FALSE;
        }

        KeUnstackDetachProcess(&ApcState);
    } __finally {
        if (KernelHandle) {
            ZwClose(KernelHandle);
        }

        if (Process) {
            ObDereferenceObject(Process);
        }
    }

    return ret;
}


PVOID GetLoadLibraryExWAddressByPid(HANDLE UniqueProcess)
/*
功能：获取一个进程的LoadLibraryExW函数的地址。

这个函数所在的文件是：
L"\\SystemRoot\\System32\\kernel32.dll"
L"\\SystemRoot\\SysWOW64\\kernel32.dll"
*/
{
    PVOID UserRoutine = NULL;

    //方法一的测试。
    //UserRoutine = GetUserFunctionAddress(UniqueProcess, g_Ntkernel32Path.Buffer, "LoadLibraryExW");
    //if (!UserRoutine) {
    //#ifdef _WIN64 //为了这个API费时，为了加快速度，最好判断是不是WOW64进程.
    //    UserRoutine = GetUserFunctionAddress(UniqueProcess, g_NtkernelWow64Path.Buffer, "LoadLibraryExW");
    //    if (UserRoutine) {
    //        LoadLibraryExWWow64Fn = (SIZE_T)UserRoutine;
    //    }
    //#endif
    //} else {
    //    LoadLibraryExWFn = (SIZE_T)UserRoutine;
    //}

    if (IsWow64Process(UniqueProcess)) {
        LoadLibraryWWow64Fn = (SIZE_T)GetUserFunctionAddress(UniqueProcess, g_NtkernelWow64Path.Buffer, "LoadLibraryW");
        LoadLibraryExWWow64Fn = (SIZE_T)GetUserFunctionAddress(UniqueProcess, g_NtkernelWow64Path.Buffer, "LoadLibraryExW");
    } else {
        LoadLibraryWFn = (SIZE_T)GetUserFunctionAddress(UniqueProcess, g_Ntkernel32Path.Buffer, "LoadLibraryW");
        LoadLibraryExWFn = (SIZE_T)GetUserFunctionAddress(UniqueProcess, g_Ntkernel32Path.Buffer, "LoadLibraryExW");
    }

    //方法二的测试：不支持WOW64。
    //UserRoutine = GetUserFunctionAddressByPeb(UniqueProcess, g_DosKernel32Path.Buffer, "LoadLibraryExW");
    //if (!UserRoutine) {
    //#ifdef _WIN64 //为了这个API费时，为了加快速度，最好判断是不是WOW64进程.
    //    UserRoutine = GetUserFunctionAddressByPeb(UniqueProcess, g_DosKernelWow64Path.Buffer, "LoadLibraryExW");
    //#endif
    //}

    return UserRoutine;
}


NTSTATUS WINAPI GetLoadLibraryExWAddressCallBack(_In_ HANDLE UniqueProcessId, _In_opt_ PVOID Context)
{
    PEPROCESS Process = NULL;
    NTSTATUS  status = STATUS_UNSUCCESSFUL;

    UNREFERENCED_PARAMETER(Context);

    status = PsLookupProcessByProcessId(UniqueProcessId, &Process);
    if (!NT_SUCCESS(status)) {

        return status;
    }

    __try {
        status = STATUS_UNSUCCESSFUL;

        if (0 == UniqueProcessId) {

            __leave;
        }

        if (PsGetProcessId(PsInitialSystemProcess) == UniqueProcessId) {

            __leave;
        }

        //if (PsIsSystemProcess(Process)) {//这个太多。
        //    Print(DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "SystemProcess:%d, %s",
        //          HandleToUlong(UniqueProcessId), PsGetProcessImageFileName(Process));
        //    __leave;
        //}

        if (PsIsProtectedProcess(Process)) {
            Print(DPFLTR_DEFAULT_ID, DPFLTR_WARNING_LEVEL, "ProtectedProcess:%d, %s",
                  HandleToUlong(UniqueProcessId), PsGetProcessImageFileName(Process));
            __leave;
        }

        //IsSecureProcess

        PVOID UserRoutine = GetLoadLibraryExWAddressByPid(UniqueProcessId);
        if (LoadLibraryExWFn && LoadLibraryExWWow64Fn && LoadLibraryWFn && LoadLibraryWWow64Fn) {
            status = STATUS_SUCCESS;//停止遍历。
        }
    } __finally {
        ObDereferenceObject(Process);
    }

    return status;
}


NTSTATUS GetLoadLibraryExWAddressByEnum()
{
    return EnumProcess(GetLoadLibraryExWAddressCallBack, NULL);
}


//////////////////////////////////////////////////////////////////////////////////////////////////


VOID ImageNotifyRoutine(_In_opt_ PUNICODE_STRING FullImageName,
                        _In_ HANDLE ProcessId,
                        _In_ PIMAGE_INFO ImageInfo
)
/*
功能：在加载kernel32（包括WOW64的\Windows\SysWOW64\kernel32.dll）的时候，利用APC注入一个DLL。

为了使注入更稳定，兼容win8.1，建议在一个workiten中进行。
*/
{
    if (ImageInfo->SystemModeImage) {
        return;
    }

    if (0 == LoadLibraryExWFn || 0 == LoadLibraryExWWow64Fn) {
        GetLoadLibraryExWAddressByPid(ProcessId);
    }

    InjectOneProcess(ProcessId, NULL);//总会成功的吧！不选择时机了，失败了也无所谓。

#ifdef false
    UNICODE_STRING LoadImageFullName = {0};
    BOOL IsKernel32 = FALSE;

    RtlGetLoadImageFullName(&LoadImageFullName, FullImageName, ProcessId, ImageInfo);

#ifdef _WIN64
    if (RtlCompareUnicodeString(&LoadImageFullName, &g_DosKernel32Path, TRUE) == 0 ||
        RtlCompareUnicodeString(&LoadImageFullName, &g_DosKernelWow64Path, TRUE) == 0) {
        IsKernel32 = TRUE;
    }
#else
    if (RtlCompareUnicodeString(&LoadImageFullName, &g_DosKernel32Path, TRUE) == 0) {
        IsKernel32 = TRUE;
    }
#endif

    if (IsKernel32) {
        InjectOneProcess(ProcessId, NULL);
    }

    FreeUnicodeString(&LoadImageFullName);
#endif 
}


//////////////////////////////////////////////////////////////////////////////////////////////////
