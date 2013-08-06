#include <ntddk.h>
#include "dbgmsg.h"
#include "datatype.h"

DWORD LockAcquired;
DWORD nCPUsLocked;
extern void NOP_FUNC(void);

KIRQL RaiseIRQL()
{
	KIRQL curr;
	KIRQL prev;
	/* 获取当前的中断优先级别 */
	curr = KeGetCurrentIrql();
	prev = curr;
	/* 
	 * windows中的普通线程中端级别低于DISPATCH_LEVEL， 
	 * 中断调度线程的级别为DISPATCH_LEVEL，任何等于或
	 * 高于DISPATCH_LEVEL的线程都不能被抢占。
	 */
	if(curr < DISPATCH_LEVEL)
	{
		/* 提升当前线程的中端级别为DISPATCH_LEVEL，这样当前线程就不会被抢占 */
		KeRaiseIrql(DISPATCH_LEVEL, &prev);
	}
	return (prev);
}

void lockRoutine
(
    IN PKDPC dpc, 
	IN PVOID context,
	IN PVOID arg1,
    IN PVOID arg2
)
{
	DBG_PRINT2("[lockRoutine]: begin-CPU[%u]", KeGetCurrentProcessorNumber());
	/* 原子的增加nCPUsLocked的值,代表又有一个CPU进入nop循环 */
	InterlockedIncrement(&nCPUsLocked);
	/* 
	 * if(LockAcquired == 1)
	 *  {
	 *      LockAcquired = 1;
	 *      return 1;
	 *  }
	 *  else
	 *  {
	 *     return 0;
	 *  } 
	 *  检测LockAcquired是否设置为1，若设置为1，则说明访问共享资源的线程已经
	 *  退出了访问，那么就可以跳出nop循环  
	 */
	while(InterlockedCompareExchange(&LockAcquired, 1, 1) == 0)
	{
		NOP_FUNC();
	}
	/* 退出nop循环 */
	InterlockedDecrement(&nCPUsLocked);
	DBG_PRINT2("[lockRoutine]: end-CPU[%u]", KeGetCurrentProcessorNumber());
	return;
}

PKDPC AcquireLock()
{
	PKDPC dpcArray;
	DWORD cpuID;
	DWORD i;
	DWORD nOtherCPUs;
	if(KeGetCurrentIrql()!= DISPATCH_LEVEL)
		return (NULL);
	DBG_TRACE("AcquireLock,","Executing at IRQL == DISPATCH_LEVEL");
	/* 初始化LockAcquired和nCPUsLocked为0 */
	InterlockedAnd(&LockAcquired, 0);
	InterlockedAnd(&nCPUsLocked, 0);
	/* keNumberProcessors是个内核变量，表示CPU的数量 */
	DBG_PRINT2("[AcquiredLock]: nCPUs=%u\n", KeNumberProcessors);
	/* 
	 * 调用ExAllocatePoolWithTag是从内核堆中分配内存的标准方式，由于当前线程的中端级别为
	 * DISPATCH_LEVLE，因此当前线程将无法处理中断，那么将无法处理缺页异常，因此分配的内存
	 * 要在NonPage Memory上
	 */
	dpcArray = (PKDPC)ExAllocatePoolWithTag(NonPagedPool, 
	           KeNumberProcessors * sizeof(KDPC),0xABCD);
	if(dpcArray == NULL)
		return(NULL);
	cpuID = KeGetCurrentProcessorNumber();
	DBG_PRINT2("[AcquireLock]: cpuID=%u\n", cpuID);
	for(i=0;i<KeNumberProcessors;i++)
	{
		PKDPC dpcPtr = &(dpcArray[i]);
		if(i != cpuID)
		{
			KeInitializeDpc(dpcPtr, lockRoutine, NULL);
			/* 指定在某一个CPU上执行这个DPC */
			KeSetTargetProcessorDpc(dpcPtr, i);
			KeInsertQueueDpc(dpcPtr, NULL, NULL);
		}
	}
	nOtherCPUs = KeNumberProcessors - 1;
	InterlockedCompareExchange(&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	/* 等待其他CPU上的线程都进入nop循环,在LockRoutine中，nCPUsLocked会加一然后进入nop循环，
	 * 在其他CPU进入nop循环后，再就可以安全的访问共享资源了
	 * */
	while(nCPUsLocked != nOtherCPUs)
	{
		NOP_FUNC();
		InterlockedCompareExchange(&nCPUsLocked, nOtherCPUs, nOtherCPUs);
	}
	DBG_TRACE("AcquireLock","All CPUs have been elevated");
	return (dpcArray);
}

NTSTATUS ReleaseLock(PVOID dpcPtr)
{
	InterlockedIncrement(&LockAcquired);
	InterlockedCompareExchange(&nCPUsLocked, 0, 0);
	while(nCPUsLocked != 0)
	{
		NOP_FUNC();
		InterlockedCompareExchange(&nCPUsLocked, 0, 0);
	}
	if(dpcPtr != NULL)
	{
		ExFreePool(dpcPtr);
	}
	DBG_TRACE("ReleaseLock","All CPUs have benn released");
	return (STATUS_SUCCESS);
}

void LowerIRQL(KIRQL prev)
{
	KeLowerIrql(prev);
	return;
}

void Unload(IN PDRIVER_OBJECT pDriverObject)
{
	DBG_TRACE("Unload","Received signal to unload the driver");
	return;
}

NTSTATUS DriverEntry(IN PDRIVER_OBJECT pDriverObject, IN PUNICODE_STRING regPath)
{
	NTSTATUS ntStatus;
	KIRQL irql;
	PKDPC dpcPtr;
	DBG_TRACE("Driver Entry","Establishing other DriverObject function pointrers");
	(*pDriverObject).DriverUnload = Unload;

	DBG_TRACE("Driver Entry","Raising IRQL");
	irql = RaiseIRQL();

	DBG_TRACE("Driver Entry","Acquiring Lock");
	dpcPtr = AcquireLock();

	DBG_TRACE("Driver Entry","Releasing Lock");
	ReleaseLock(dpcPtr);

	DBG_TRACE("Driver Entry","Lowering IRQL");
	LowerIRQL(irql);
	return (STATUS_SUCCESS);

}


