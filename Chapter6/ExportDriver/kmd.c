#include <ntddk.h>
NTSTATUS DriverEntry
(  
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING regPath
)
{
	return (STATUS_SUCCESS);
}


NTSTATUS DllInitialize(IN PUNICODE_STRING RegistryPath)
{
	return (STATUS_SUCCESS);
}

NTSTATUS DllUnload()
{
	return (STATUS_SUCCESS);
}

NTSTATUS
ExportRoutine( )
{
    DbgPrint("ExportRoutine\n");
    return STATUS_SUCCESS;
}

