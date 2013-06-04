#include "ntddk.h"

#include "dbgmsg.h"
#include "ctrlcode.h"
#include "device.h"


/* MSNetDigaDeviceObject代表我们创建的设备 */
PDEVICE_OBJECT MSNetDiagDeviceObject;
/* DriverObjectRef代表我们注册的驱动 */
PDRIVER_OBJECT DriverObjectRef;


void TestCommand(PVOID inputBuffer, PVOID outputBuffer, ULONG inputBufferLength, ULONG outputBufferLength);
NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);
NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP Irp);


NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject);
NTSTATUS RegisterDriverDeviceLink();

VOID Unload(IN PDRIVER_OBJECT DriverObject)
{
	PDEVICE_OBJECT pdeviceObj;
	UNICODE_STRING unicodeString;
	DBG_TRACE("OnUnload","Received signal to unload the driver");
	pdeviceObj = (*DriverObject).DeviceObject;
	if(pdeviceObj != NULL)
	{
		DBG_TRACE("OnUnload","Unregistering driver's symbolic link");
		RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
		IoDeleteSymbolicLink(&unicodeString);
		DBG_TRACE("OnUnload","Unregistering driver's device name");
		IoDeleteDevice((*DriverObject).DeviceObject);
	}
	return ;
}
/* 
 * DriverObject相当于注册的驱动，DeviceObject为对应某个驱动设备
 * 一个驱动可以创建多个设备，然后通过DriverObject::DeviceObject和
 * DeviceObject::NextDevice遍历整个设备链表
 */
NTSTATUS DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
	int i;
	NTSTATUS ntStatus;
	DBG_TRACE("Driver Entry","Driver has benn loaded");
	for(i=0;i<IRP_MJ_MAXIMUM_FUNCTION;i++)
	{
		(*DriverObject).MajorFunction[i] = defaultDispatch;
	}
	(*DriverObject).MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatchIOControl;
	(*DriverObject).DriverUnload = Unload;

	DBG_TRACE("Driver Entry","Registering driver's device name");
	ntStatus = RegisterDriverDeviceName(DriverObject);
	if(!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry","Failed to create device");
		return ntStatus;
	}

	DBG_TRACE("Driver Entry","Registering driver's symbolic link");
	if(!NT_SUCCESS(ntStatus))
	{
		DBG_TRACE("Driver Entry","Failed to create symbolic link");
		return ntStatus;
	}
	DriverObjectRef = DriverObject;
	return STATUS_SUCCESS;
}
/*
 * IRP.IoStatus : 类型为IO_STATUS_BLOCK
 * A driver sets an IRP's I/O status block to indicate the final status of 
 * an I/O request, before calling IoCompleteRequest for the IRP.
 typedef struct _IO_STATUS_BLOCK {
  union {
    NTSTATUS Status;
    PVOID    Pointer;
  };
  ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK

  Status: This is the completion status, either STATUS_SUCCESS if the 
          requested operation was completed successfully or an informational, 
          warning, or error STATUS_XXX value. 
  Information: This is set to a request-dependent value. For example, 
          on successful completion of a transfer request, this is set 
		  to the number of bytes transferred. If a transfer request is 
		  completed with another STATUS_XXX, this member is set to zero.

 */
NTSTATUS defaultDispatch(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	/*
	 The IoCompleteRequest routine indicates that the caller has 
	 completed all processing for a given I/O request and is 
	 returning the given IRP to the I/O manager.
	 */
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return (STATUS_SUCCESS);
}
/*
 * I/O堆栈单元由IO_STACK_LOCATION定义，每一个堆栈单元都对应一个设备对象。
 * 我们知道，在一个驱动程序中，可以创建一个或多个设备对象，而这些设备对象
 * 都对应着一个IO_STACK_LOCATION结构体，而在驱动程序中的多个设备对象，而
 * 这些设备对象之间的关系为水平层次关系。
 * Parameters 为每个类型的 request 提供参数，例如：Create(IRP_MJ_CREATE 请求），
 * Read（IRP_MJ_READ 请求），StartDevice（IRP_MJ_PNP 的子类 IRP_MN_START_DEVICE）
 * 
	//
	// NtDeviceIoControlFile 参数
	//
	struct
	{
		ULONG OutputBufferLength;
		ULONG POINTER_ALIGNMENT InputBufferLength;
		ULONG POINTER_ALIGNMENT IoControlCode;
		PVOID Type3InputBuffer;
	} DeviceIoControl;
	在DriverEntry函数中，我们设置dispatchIOControl处理IRP_MJ_DEVICE_CONTROL
	类型的请求，因此在dispatchIOControl中，我们只关心IOCTL请求，Parameters中
	只包含DeviceIoControl成员
 */
NTSTATUS dispatchIOControl(IN PDEVICE_OBJECT DeviceObject, IN PIRP IRP)
{
	PIO_STACK_LOCATION irpStack;
	PVOID inputBuffer;
	PVOID outputBuffer;
	ULONG inBufferLength;
	ULONG outBufferLength;
	ULONG ioctrlcode;
	NTSTATUS ntStatus;
	ntStatus = STATUS_SUCCESS;
	((*IRP).IoStatus).Status = STATUS_SUCCESS;
	((*IRP).IoStatus).Information = 0;
	inputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	outputBuffer = (*IRP).AssociatedIrp.SystemBuffer;
	irpStack = IoGetCurrentIrpStackLocation(IRP);
	inBufferLength = (*irpStack).Parameters.DeviceIoControl.InputBufferLength;
	outBufferLength = (*irpStack).Parameters.DeviceIoControl.OutputBufferLength;
	ioctrlcode = (*irpStack).Parameters.DeviceIoControl.IoControlCode;

	DBG_TRACE("dispatchIOControl","Received a command");
	switch(ioctrlcode)
	{
	case IOCTL_TEST_CMD:
		{
			TestCommand(inputBuffer, outputBuffer, inBufferLength, outBufferLength);
			((*IRP).IoStatus).Information = outBufferLength;
		}
		break;
	default:
		{
			DBG_TRACE("dispatchIOControl","control code not recognized");
		}
		break;
	}
	/* 在处理完请求后，调用IoCompleteRequest */
	IoCompleteRequest(IRP, IO_NO_INCREMENT);
	return(ntStatus);
}

void TestCommand(PVOID inputBuffer, PVOID outputBuffer, ULONG inputBufferLength, ULONG outputBufferLength)
{
	char *ptrBuffer;
	DBG_TRACE("dispathIOControl","Displaying inputBuffer");
	ptrBuffer = (char*)inputBuffer;
	DBG_PRINT2("[dispatchIOControl]: inputBuffer=%s\n", ptrBuffer);
	DBG_TRACE("dispatchIOControl","Populating outputBuffer");
	ptrBuffer = (char*)outputBuffer;
	ptrBuffer[0] = '!';
	ptrBuffer[1] = '1';
	ptrBuffer[2] = '2';
	ptrBuffer[3] = '3';
	ptrBuffer[4] = '!';
	ptrBuffer[5] = '\0';
	DBG_PRINT2("[dispatchIOControl]:outputBuffer=%s\n", ptrBuffer);
	return;
}

NTSTATUS RegisterDriverDeviceName(IN PDRIVER_OBJECT DriverObject)
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	/* 利用DeviceNameBuffer来初始化unicodeString */
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	/*
	 * 创建一个设备，设备类型为FILE_DEVICE_RK（由我们自己在device.h中定义)，
	 * 创建的设备保存在MSNetDiagDeviceObject中
	 */
	ntStatus = IoCreateDevice
		(
		    DriverObject,
			0,
			&unicodeString,
			FILE_DEVICE_RK,
			0,
			TRUE,&MSNetDiagDeviceObject
		);
	return (ntStatus);
}

NTSTATUS RegisterDriverDeviceLink()
{
	NTSTATUS ntStatus;
	UNICODE_STRING unicodeString;
	UNICODE_STRING unicodeLinkString;
	RtlInitUnicodeString(&unicodeString, DeviceNameBuffer);
	RtlInitUnicodeString(&unicodeString, DeviceLinkBuffer);
	/*
	 * IoCreateSymbolicLink创建一个设备链接。驱动程序中虽然注册了设备，
	 * 但它只能在内核中可见，为了使应用程序可见，驱动需哟啊暴露一个符号
	 * 链接，该链接指向真正的设备名
	 */
	ntStatus = IoCreateSymbolicLink(&unicodeLinkString, &unicodeString);
	return (ntStatus);
}