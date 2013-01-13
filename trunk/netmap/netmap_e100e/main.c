#include <ntddk.h>

NTSTATUS AddDevice(IN PDRIVER_OBJECT, IN PDEVICE_OBJECT);
VOID Unload(IN PDRIVER_OBJECT);
NTSTATUS DispatchPnp(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchCreate(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchClose(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchShutdown(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchDeviceControl(IN PDEVICE_OBJECT, IN PIRP);

PDEVICE_OBJECT MyDeviceObject = NULL;
PDEVICE_OBJECT MyPhysicalDeviceObject = NULL;
PDEVICE_OBJECT LowerDeviceObject = NULL;


NTSTATUS
ForwardIrpAndWaitCompletionRoutine(
	IN PDEVICE_OBJECT Fdo,
	IN PIRP Irp,
	IN PVOID Context)
{
	PKEVENT eventPtr = Context;

	if (Irp->PendingReturned) {
		KeSetEvent(eventPtr, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ForwardIrpAndWait(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	KEVENT event;
	NTSTATUS status;

	KeInitializeEvent(&event, NotificationEvent, FALSE);
	IoCopyCurrentIrpStackLocationToNext(Irp);
	IoSetCompletionRoutine(Irp,
		ForwardIrpAndWaitCompletionRoutine, &event,
		TRUE, TRUE, TRUE);
	status = IoCallDriver(DeviceObject, Irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = Irp->IoStatus.Status;
	}
	return status;
}


VOID
GetProperty(
	IN PDEVICE_OBJECT PhisicalDeviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG size = 0;
	INTERFACE_TYPE busType;

	DbgPrint("GetProperty: enter\n");
	size = sizeof(busType);
	status = IoGetDeviceProperty(PhisicalDeviceObject,
		DevicePropertyLegacyBusType,
		size, &busType, &size);
	DbgPrint("GetProperty: busType=%x\n", busType);

	DbgPrint("GetProperty: leave\n");
}

NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;

	DbgPrint("DriverEntry: enter\n");

	DriverObject->DriverUnload = Unload;
	DriverObject->DriverExtension->AddDevice = AddDevice;
	DriverObject->MajorFunction[IRP_MJ_PNP] = DispatchPnp;

	DriverObject->MajorFunction[IRP_MJ_CREATE] = DispatchCreate;
	DriverObject->MajorFunction[IRP_MJ_CLOSE] = DispatchClose;
	DriverObject->MajorFunction[IRP_MJ_SHUTDOWN] = DispatchShutdown;
	DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = DispatchDeviceControl;

	DbgPrint("DriverEntry: leave\n");
	return STATUS_SUCCESS;
}


NTSTATUS
AddDevice(
	IN PDRIVER_OBJECT DriverObject,
	IN PDEVICE_OBJECT PhysicalDeviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	UNICODE_STRING devName;

	DbgPrint("AddDevice: enter\n");

	RtlInitUnicodeString(&devName, L"\\Device\\TestDriver");
	status = IoCreateDevice(DriverObject,
		0,
		&devName, FILE_DEVICE_NAMED_PIPE,
		0, FALSE,
		&MyDeviceObject);
	if (status != STATUS_SUCCESS) {
		DbgPrint("AddDevice: leave#1\n");
		goto done;
	}
	ASSERT(DeviceObject != NULL);

	LowerDeviceObject = IoAttachDeviceToDeviceStack(MyDeviceObject,
		PhysicalDeviceObject);
	MyPhysicalDeviceObject = PhysicalDeviceObject;

	DbgPrint("AddDevice: leave\n");
done:
	return status;
}

VOID
Unload(
	IN PDRIVER_OBJECT DriverObject)
{
	DbgPrint("Unload: enter\n");
	DbgPrint("Unload: leave\n");

}

NTSTATUS
DispatchPnp(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);

	DbgPrint("DispatchPnp: enter\n");

	DbgPrint("DispatchPnp: MinorFunction=%x\n", Stack->MinorFunction);
	switch (Stack->MinorFunction) {
	case IRP_MN_START_DEVICE:
		status = ForwardIrpAndWait(LowerDeviceObject, Irp);
		if (status == STATUS_SUCCESS) {
			GetProperty(MyPhysicalDeviceObject);
		}
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		DbgPrint("DispatchPnp: leave#1\n");
		return status;
	case IRP_MN_REMOVE_DEVICE:
		status = ForwardIrpAndWait(LowerDeviceObject, Irp);
		if (status == STATUS_SUCCESS) {
			IoCompleteRequest(Irp, IO_NO_INCREMENT);
			return status;
			DbgPrint("DispatchPnp: leave#2\n");
		}
		IoDetachDevice(LowerDeviceObject);
		IoDeleteDevice(MyDeviceObject);
		DbgPrint("DispatchPnp: leave#3\n");
		return status;
	default:
		break;
	}

	IoSkipCurrentIrpStackLocation(Irp);
	DbgPrint("DispatchPnp: leave\n");
	return IoCallDriver(LowerDeviceObject, Irp);
}

NTSTATUS
DispatchCreate(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	DbgPrint("DispatchCreate: enter\n");

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	DbgPrint("DispatchCreate: leave\n");
	return STATUS_SUCCESS;
}

NTSTATUS
DispatchClose(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	DbgPrint("DispatchClose: enter\n");

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	DbgPrint("DispatchClose: leave\n");
	return STATUS_SUCCESS;
}

NTSTATUS
DispatchShutdown(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	DbgPrint("DispatchShutdown: enter\n");

	Irp->IoStatus.Status = STATUS_SUCCESS;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	DbgPrint("DispatchShutdown: leave\n");
	return STATUS_SUCCESS;
}

NTSTATUS
DispatchDeviceControl(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	NTSTATUS status = STATUS_NOT_SUPPORTED;
	DbgPrint("DispatchDeviceControl: enter\n");

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	DbgPrint("DispatchDeviceControl: leave\n");
	return status;
}
