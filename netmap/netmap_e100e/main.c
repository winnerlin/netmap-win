#include <ntifs.h>
#include <wdm.h>

#include "e1000_api.h"
#include "e100e.h"

NTSTATUS AddDevice(IN PDRIVER_OBJECT, IN PDEVICE_OBJECT);
VOID Unload(IN PDRIVER_OBJECT);
NTSTATUS Dispatch(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchPnp(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchCreate(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchClose(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchShutdown(IN PDEVICE_OBJECT, IN PIRP);
NTSTATUS DispatchDeviceControl(IN PDEVICE_OBJECT, IN PIRP);


// Access a device's configuration space.
// http://msdn.microsoft.com/en-us/library/windows/hardware/ff558707(v=vs.85).aspx
NTSTATUS
ReadWritePciConfigSpace(
	IN PDEVICE_OBJECT  targetDeviceObject,
	IN ULONG  readOrWrite,  // 0 for read, 1 for write
	IN PVOID  buffer,
	IN ULONG  offset,
	IN ULONG  length
	)
{
	KEVENT event;
	NTSTATUS status;
	PIRP irp;
	IO_STATUS_BLOCK ioStatusBlock;
	PIO_STACK_LOCATION irpStack;
	PDEVICE_OBJECT deviceObject;

	PAGED_CODE();
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	deviceObject = IoGetAttachedDeviceReference(targetDeviceObject);
	if (deviceObject == NULL) {
		status = STATUS_NO_SUCH_DEVICE;
		goto done;
	}

	irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
		deviceObject,
		NULL,
		0,
		NULL,
		&event,
		&ioStatusBlock);
	if (irp == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto done;
	}
	irpStack = IoGetNextIrpStackLocation(irp);
	if (readOrWrite == 0) {
		irpStack->MinorFunction = IRP_MN_READ_CONFIG;
	} else {
		irpStack->MinorFunction = IRP_MN_WRITE_CONFIG;
	}
	irpStack->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
	irpStack->Parameters.ReadWriteConfig.Buffer = buffer;
	irpStack->Parameters.ReadWriteConfig.Offset = offset;
	irpStack->Parameters.ReadWriteConfig.Length = length;

	// Initialize the status to error in case the bus driver does not 
	// set it correctly.
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	status = IoCallDriver(deviceObject, irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}
done:
	// Done with reference
	if (deviceObject != NULL) {
		ObDereferenceObject(deviceObject);
	}
	return status;
}

u32
pci_read_config(
	IN PDEVICE_OBJECT deviceObject,
	IN int reg,
	IN int width
	)
{
	NTSTATUS status;
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;
	u32 val;

	ASSERT(deviceExtension != NULL);
	ASSERT(width <= sizeof(value));

	status = ReadWritePciConfigSpace(deviceExtension->nextDeviceObject,
		0,
		&val,
		reg,
		width
		);
	if (status != STATUS_SUCCESS) {
		return 0xffffffff;
	}
	return val;
}

void
pci_write_config(
	IN PDEVICE_OBJECT deviceObject,
	IN int reg,	
	u32 val,
	IN int width
	)
{
	NTSTATUS status;
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;
	u32 value;

	ASSERT(width <= sizeof(value));

	status = ReadWritePciConfigSpace(deviceExtension->nextDeviceObject,
		1,
		&val,
		reg,
		width
		);
}

int
pci_find_extcap(
	IN PDEVICE_OBJECT deviceObject,
	IN int capability,
	OUT int *capreg
	)
{
	u32 status, type, ptr, capability1;

	status = pci_read_config(deviceObject,
		FIELD_OFFSET(PCI_COMMON_CONFIG, Status),
		2
		);
	if ((status & PCI_STATUS_CAPABILITIES_LIST) == 0) {
		return -1;
	}
	type = pci_read_config(deviceObject, FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType), 1);
	switch (type & 0x7f) {
	case 0x00:
		ptr = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.CapabilitiesPtr);
		break;
	case 0x01: // PCI to PCI Bridge
		ptr = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type1.CapabilitiesPtr);
		break;
	case 0x02: // PCI to CARDBUS Bridge
		ptr = FIELD_OFFSET(PCI_COMMON_CONFIG, u.type2.CapabilitiesPtr);
		break;
	default:
		return -1;
	}

	ptr = pci_read_config(deviceObject, ptr, 1);
	while (ptr != 0) {
		capability1 = pci_read_config(deviceObject,
			ptr + FIELD_OFFSET(PCI_CAPABILITIES_HEADER, CapabilityID),
			1
			);
		if (capability1 == capability) {
			if (capreg != NULL) {
				*capreg = ptr;
			}
			return 0;
		}
		ptr = pci_read_config(deviceObject,
			ptr + FIELD_OFFSET(PCI_CAPABILITIES_HEADER, Next),
			1);
	}

	return -1;
}

VOID
IdentifyHardware(
	IN PDEVICE_OBJECT deviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;
	PCI_COMMON_CONFIG  pciConfig;
	unsigned int i = 0;

	DbgPrint("IdentifyHardware: enter\n");

	ASSERT(deviceExtension != NULL);

	RtlZeroMemory(&pciConfig, sizeof(pciConfig));
	status = ReadWritePciConfigSpace(deviceExtension->nextDeviceObject,
		0, // Read
		&pciConfig,
		FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID),
		sizeof(pciConfig)
		);
	if (status != STATUS_SUCCESS) {
		DbgPrint("ReadWriteConfigSpace=%x\n", status);
		DbgPrint("IdentifyHardware: leave#1\n");
		return;
	}

#if 1
	DbgPrint("Command => %x\n", pciConfig.Command);

	DbgPrint("VendorID => %x\n", pciConfig.VendorID);
	DbgPrint("DeviceID => %x\n", pciConfig.DeviceID);
	DbgPrint("RevisionID => %x\n", pciConfig.RevisionID);
	DbgPrint("SubVendorID => %x\n", pciConfig.u.type0.SubVendorID);
	DbgPrint("SubSystemID => %x\n", pciConfig.u.type0.SubSystemID);
#endif

	deviceExtension->hw.bus.pci_cmd_word = pciConfig.Command;
	deviceExtension->hw.vendor_id = pciConfig.VendorID;
	deviceExtension->hw.device_id = pciConfig.DeviceID;
	deviceExtension->hw.revision_id = pciConfig.RevisionID;
	deviceExtension->hw.subsystem_vendor_id = pciConfig.u.type0.SubVendorID;
	deviceExtension->hw.subsystem_device_id = pciConfig.u.type0.SubSystemID;

	if (e1000_set_mac_type(&deviceExtension->hw)) {
		DbgPrint("IdentifyHardware: leave#2\n");
		return;
	}

	DbgPrint("IdentifyHardware: leave\n");
	return;
}

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff554399(v=vs.85).aspx
NTSTATUS
AllocatePciResources(
	IN PDEVICE_OBJECT deviceObject,
	IN PCM_RESOURCE_LIST allocatedResources
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;
	unsigned int i, ii;

	DbgPrint("AllocatePciResources: enter\n");

	for (i = 0; i < allocatedResources->Count; i++) {
		DbgPrint("List[%d] => InterfaceType=%x,BusNumber=%x,Version=%x,Revision=%x\n",
			i,
			allocatedResources->List[i].InterfaceType, // PCIBus=5
			allocatedResources->List[i].BusNumber,
			allocatedResources->List[i].PartialResourceList.Version,
			allocatedResources->List[i].PartialResourceList.Revision);
		for (ii = 0; ii < allocatedResources->List[i].PartialResourceList.Count; ii++) {
			switch (allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Type) {
			case CmResourceTypePort:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypePort,u.Port={Start=%x,Length=%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Port.Start,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Port.Length
					);
				break;
			case CmResourceTypeInterrupt:
				if ((allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.MessageInterrupt={Raw: MessageCount=%x,Vector=%x,Affinity=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.MessageCount,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.Vector,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.Affinity
						);
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.MessageInterrupt={Translated: Level=%x,Vector=%x,Affinity=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Level,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Vector,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Affinity
						);
				} else {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.Interrupt={Level=%x,Vector=%x,Affinity=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Level,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Vector,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Affinity
						);
				}
				break;
			case CmResourceTypeMemory:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory={Start=%x,Length=%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Start,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Length
					);
				deviceExtension->osdep.mem_bus_physical = allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Start;
				deviceExtension->osdep.mem_bus_length = allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Length;
				break;
			case CmResourceTypeMemoryLarge:
				if ((allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_40) != 0) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory40={Start=%x,Length40=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory40.Start,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory40.Length40
						);
				} else if (
					(allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_48) != 0
					) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory48={Start=%x,Length48=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory48.Start,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory48.Length48
						);
				} else if (
					(allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_64) != 0
					) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory64={Start=%x,Length64=%x}\n",
						ii,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory64.Start,
						allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory64.Length64
						);
				}
				break;
			case CmResourceTypeDma:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDma,u.Dma={Channel=%x,Port=%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Dma.Channel,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Dma.Port
					);
				break;
			case CmResourceTypeDevicePrivate:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			case CmResourceTypeBusNumber:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeBusNumber,u.BusNumber={Start=%x,Length=%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.BusNumber.Start,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.BusNumber.Length
					);
				break;
			case CmResourceTypeDeviceSpecific:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDeviceSpecific,u.DeviceSpecificData{DataSize=%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DeviceSpecificData.DataSize
					);
				break;
			case CmResourceTypePcCardConfig:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypePcCardConfig,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			case CmResourceTypeMfCardConfig:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMfCardConfig,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			default:
				DbgPrint("\tDescriptors[%d] => Type=%x\n",
					ii,
					allocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Type
					);
			}
		}
	}

	deviceExtension->osdep.mem_bus_virtual = MmMapIoSpace(
		deviceExtension->osdep.mem_bus_physical,
		deviceExtension->osdep.mem_bus_length,
		MmNonCached
		);
	if (deviceExtension->osdep.mem_bus_virtual == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		DbgPrint("AllocatePciResources: leave#1\n");
		goto done;
	}
	deviceExtension->hw.hw_addr = (u8 *)deviceExtension->osdep.mem_bus_virtual;
	deviceExtension->hw.back = &deviceExtension->osdep;

	DbgPrint("AllocatePciResources: leave\n");
done:
	return status;
}

VOID
FreePciResource(
	IN PDEVICE_OBJECT deviceObject
	)
{
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;

	DbgPrint("FreePciResource: leave\n");

	if (deviceExtension->osdep.mem_bus_virtual != NULL) {
		MmUnmapIoSpace(deviceExtension->osdep.mem_bus_virtual,
			deviceExtension->osdep.mem_bus_length
			);
		deviceExtension->osdep.mem_bus_virtual = NULL;
	}

	DbgPrint("FreePciResource: leave\n");
}


NTSTATUS
ForwardIrpAndWaitCompletionRoutine(
	IN PDEVICE_OBJECT fdo,
	IN PIRP irp,
	IN PVOID context
	)
{
	PKEVENT eventPtr = (PKEVENT)context;

	if (irp->PendingReturned) {
		KeSetEvent(eventPtr, IO_NO_INCREMENT, FALSE);
	}
	return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
ForwardIrpAndWait(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp
	)
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

NTSTATUS
DriverEntry(
	IN PDRIVER_OBJECT DriverObject,
	IN PUNICODE_STRING RegistryPath)
{
	NTSTATUS status;
	int i;

	DbgPrint("DriverEntry: enter\n");

	for (i = 0; i < IRP_MJ_MAXIMUM_FUNCTION; i++) {
		DriverObject->MajorFunction[i] = Dispatch;
	}
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
	IN PDRIVER_OBJECT driverObject,
	IN PDEVICE_OBJECT physicalDeviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	PDEVICE_OBJECT deviceObject = NULL;
	PE100E_DEVICE_EXTENSION deviceExtension = NULL;

	DbgPrint("AddDevice: enter\n");

	status = IoCreateDevice(driverObject,
		sizeof(E100E_DEVICE_EXTENSION),
		NULL,
		FILE_DEVICE_NETWORK,
		FILE_DEVICE_SECURE_OPEN,
		FALSE,
		&deviceObject);
	if (status != STATUS_SUCCESS) {
		DbgPrint("AddDevice: leave#1\n");
		goto done;
	}
	ASSERT(deviceObject != NULL);
	
	deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;
	RtlZeroMemory(deviceExtension, sizeof(E100E_DEVICE_EXTENSION));

	deviceExtension->deviceObject = deviceExtension->osdep.dev = deviceObject;
	deviceExtension->physicalDeviceObject = physicalDeviceObject;

	deviceExtension->nextDeviceObject = IoAttachDeviceToDeviceStack(
		deviceObject,
		physicalDeviceObject
		);
	if (deviceExtension->nextDeviceObject == NULL) {
		IoDeleteDevice(deviceObject);
		status = STATUS_NO_SUCH_DEVICE;
		DbgPrint("AddDevice: leave#2\n");
		goto done;
	}

	deviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

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

// http://msdn.microsoft.com/ja-JP/library/windows/hardware/ff563856(v=vs.85).aspx
NTSTATUS
DispatchPnpStartDevice(
	IN PDEVICE_OBJECT deviceObject,
	IN PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;

	DbgPrint("DispatchPnpStartDevice: enter\n");

	status = ForwardIrpAndWait(deviceExtension->nextDeviceObject, irp);
	if (status != STATUS_SUCCESS) {
		DbgPrint("DispatchPnpStartDevice: leave#1\n");
		goto done;
	}
	IdentifyHardware(deviceObject);
	status = AllocatePciResources(deviceObject,
		irpSp->Parameters.StartDevice.AllocatedResourcesTranslated
		);
	if (status != STATUS_SUCCESS) {
		DbgPrint("DispatchPnpStartDevice: leave#2\n");
		goto done;
	}

	if (e1000_setup_init_funcs(&deviceExtension->hw, TRUE)) {
		status = STATUS_UNSUCCESSFUL;
		DbgPrint("DispatchPnpStartDevice: leave#3\n");
		goto done;
	}
	e1000_get_bus_info(&deviceExtension->hw);
	e1000_reset_hw(&deviceExtension->hw);

	if (e1000_validate_nvm_checksum(&deviceExtension->hw) < 0) {
		if (e1000_validate_nvm_checksum(&deviceExtension->hw) < 0) {
			DbgPrint("The EEPROM Checksum Is Not Valid\n");
			status = STATUS_UNSUCCESSFUL;
			DbgPrint("DispatchPnpStartDevice: leave#3\n");
			goto done;
		}
	}

	if (e1000_read_mac_addr(&deviceExtension->hw) < 0) {
		DbgPrint("EEPROM read error while reading MAC address\n");
		status = STATUS_UNSUCCESSFUL;
		DbgPrint("DispatchPnpStartDevice: leave#4\n");
		goto done;
	}

	DbgPrint("MAC address: %02x-%02x-%02x-%02x-%02x-%02x\n",
		deviceExtension->hw.mac.addr[0],
		deviceExtension->hw.mac.addr[1],
		deviceExtension->hw.mac.addr[2],
		deviceExtension->hw.mac.addr[3],
		deviceExtension->hw.mac.addr[4],
		deviceExtension->hw.mac.addr[5]
	);
	DbgPrint("DispatchPnpStartDevice: leave\n");
done:
	irp->IoStatus.Status = status;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

// http://msdn.microsoft.com/ja-JP/library/windows/hardware/ff561056(v=vs.85).aspx
NTSTATUS
DispatchPnpStopDevice(
	IN PDEVICE_OBJECT deviceObject,
	IN PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;

	DbgPrint("DispatchPnpStopDevice: enter\n");

	FreePciResource(deviceObject);

	status = ForwardIrpAndWait(deviceExtension->nextDeviceObject, irp);

	IoDetachDevice(deviceExtension->nextDeviceObject);
	IoDeleteDevice(deviceObject);

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	DbgPrint("DispatchPnpStopDevice: leave\n");
	return status;
}

NTSTATUS
DispatchPnp(
	IN PDEVICE_OBJECT deviceObject,
	IN PIRP irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(irp);
	PE100E_DEVICE_EXTENSION deviceExtension = (PE100E_DEVICE_EXTENSION)deviceObject->DeviceExtension;

	DbgPrint("DispatchPnp: enter\n");

	ASSERT(deviceExtension != NULL);

	DbgPrint("DispatchPnp: MinorFunction=%x\n", irpSp->MinorFunction);
	switch (irpSp->MinorFunction) {
	case IRP_MN_START_DEVICE:
		status = DispatchPnpStartDevice(deviceObject, irp);
		DbgPrint("DispatchPnp: leave#1\n");
		return status;
	case IRP_MN_REMOVE_DEVICE:
		status = DispatchPnpStopDevice(deviceObject, irp);
		DbgPrint("DispatchPnp: leave#2\n");
		return status;
	default:
		break;
	}

	IoSkipCurrentIrpStackLocation(irp);
	DbgPrint("DispatchPnp: leave\n");
	return IoCallDriver(deviceExtension->nextDeviceObject, irp);
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

NTSTATUS
Dispatch(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	NTSTATUS status = STATUS_NOT_SUPPORTED;
	DbgPrint("Dispatch: enter\n");

	Irp->IoStatus.Status = status;
	Irp->IoStatus.Information = 0;

	IoCompleteRequest(Irp, IO_NO_INCREMENT);

	DbgPrint("Dispatch: leave\n");
	return status;
}

#if 0
ULONG delay_usec = 50;
USHORT opcode_bits = 3;
USHORT address_bits = 0;
ULONG word_size = 0;

VOID
RaiseEECClock(
	IN PUCHAR Register0,
	IN OUT PULONG eecd
	)
{
	ULONG val;
	DbgPrint("RaiseEECClock: enter\n");

	*eecd = *eecd | E1000_EECD_SK;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		eecd,
		1
		);
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_STATUS),
		&val,
		1
		);
	KeStallExecutionProcessor(50);
	DbgPrint("RaiseEECClock: leave\n");
}

VOID
LowerEECClock(
	IN PUCHAR Register0,
	IN OUT PULONG eecd
	)
{
	ULONG val;
	DbgPrint("LowerEECClock: enter\n");

	*eecd = *eecd & ~E1000_EECD_SK;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		eecd,
		1
		);
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_STATUS),
		&val,
		1
		);
	KeStallExecutionProcessor(50);
	DbgPrint("LowerEECClock: leave\n");
}

VOID
ShiftOutEEC(
	IN PUCHAR Register0,
	IN ULONG data,
	IN USHORT count
	)
{
	ULONG eecd;
	ULONG mask;
	ULONG val;

	DbgPrint("ShiftOutEEC: enter\n");

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	mask = 0x01 << (count - 1);
	eecd |= E1000_EECD_DO;

	do {
		eecd &= ~E1000_EECD_DI;
		if ((data & mask) != 0)
			eecd |= E1000_EECD_DI;

		WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
			&eecd,
			1
			);
		READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_STATUS),
			&val,
			1
			);
		KeStallExecutionProcessor(50);
		RaiseEECClock(Register0, &eecd);
		LowerEECClock(Register0, &eecd);

		mask >>= 1;
	} while (mask > 0);

	eecd &= ~E1000_EECD_DI;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);

	DbgPrint("ShiftOutEEC: leave\n");
}

USHORT
ShiftInEEC(
	IN PUCHAR Register0,
	IN USHORT count
	)
{
	ULONG eecd;
	ULONG i;
	USHORT data;

	DbgPrint("ShiftInEEC: enter\n");

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);

	eecd &= ~(E1000_EECD_DO | E1000_EECD_DI);
	data = 0;

	for (i = 0; i < count; i++) {
		data <<= 1;
		RaiseEECClock(Register0, &eecd);

		READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
			&eecd,
			1
			);

		eecd &= ~E1000_EECD_DI;
		if ((eecd & E1000_EECD_DO) != 0)
			data |= 1;

		LowerEECClock(Register0, &eecd);
	}

	DbgPrint("ShiftInEEC: leave\n");
	return data;
}

VOID
StopNvm(
	IN PUCHAR Register0
	)
{
	ULONG eecd = 0;

	DbgPrint("StopEvm: enter\n");
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd &= ~(E1000_EECD_CS | E1000_EECD_DI);
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	RaiseEECClock(Register0, &eecd);
	LowerEECClock(Register0, &eecd);

	DbgPrint("StopEvm: leaver\n");
}

VOID
InitNvmParam(
	IN PUCHAR Register0
	)
{
	ULONG eecd = 0;

	DbgPrint("InitNvmParam: enter\n");
	delay_usec = 50;
	opcode_bits = 3;

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
/*
 * EEPROM Size
 * 0b = 1024-bit (64 word) NM93C46 compatible EEPROM
 * 1b = 4096-bit (256 word) NM93C66 compatible EEPROM
 * This bit indicates the EEPROM size, based on acknowledges seen
 * during EEPROM scans of different addresses. This bit is read-only.
 * Note: This is a reserved bit for the 82541xx and 82547GI/EI
 */
	address_bits = (eecd & E1000_EECD_SIZE) != 0 ? 8 : 6;
	word_size = (eecd & E1000_EECD_SIZE) != 0 ? 256 : 64;

	DbgPrint("address_bits=%u, word_size=%u\n", address_bits, word_size);
	address_bits = 6;
	word_size = 64;
	DbgPrint("InitNvmParam: leave\n");
}

/*
 * To directly access the EEPROM, software should follow these steps:
 * ==> in AcquireNvm()
 *  1. Write a 1b to the EEPROM Request bit (EEC.EE_REQ).
 *  2. Read the EEPROM Grant bit (EEC.EE_GNT) until it becomes 1b. It remains 0b as long as the
 *     hardware is accessing the EEPROM. 
 * <== 
 *  3. Write or read the EEPROM using the direct access to the 4-wire interface as defined in the
 *     EEPROM/FLASH Control & Data Register (EEC). The exact protocol used depends on the
 *     EEPROM placed on the board and can be found in the appropriate data sheet.
 *  4. Write a 0b to the EEPROM Request bit (EEC.EE_REQ).
 */
NTSTATUS
AcquireNvm(
	IN PUCHAR Register0
	)
{
	ULONG eecd = 0;
	ULONG timeout = E1000_NVM_GRANT_ATTEMPTS;

	DbgPrint("AcquireNvm: enter\n");
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd |= E1000_EECD_REQ;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	while (timeout > 0) {
		if ((eecd & E1000_EECD_GNT) != 0)
			break;
		KeStallExecutionProcessor(delay_usec);
		READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
			&eecd,
			1
			);
		timeout--;
	}
	if (timeout == 0) {
		eecd &= ~E1000_EECD_REQ;
		WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
			&eecd,
			1
			);
		DbgPrint("AcquireNvm: leave#1\n");
		return STATUS_INVALID_PARAMETER;
	}

	DbgPrint("AcquireNvm: leave\n");
	return STATUS_SUCCESS;
}

/* Microwire EEPROM in the case of 82543 */
NTSTATUS
PrepareNvm(
	IN PUCHAR Register0)
{
	ULONG eecd = 0;

	DbgPrint("PrepateNvm: enter\n");

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd &= ~(E1000_EECD_DI | E1000_EECD_SK);
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd |= E1000_EECD_CS;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);

	DbgPrint("PrepateNvm: enter\n");
	return STATUS_SUCCESS;
}

VOID
StandbyNvm(
	IN PUCHAR Register0
	)
{
	ULONG eecd = 0;

	DbgPrint("StandbyNvm: enter\n");
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd &= ~(E1000_EECD_CS | E1000_EECD_SK);
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd |= E1000_EECD_CS;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);

	RaiseEECClock(Register0, &eecd);
	eecd |= E1000_EECD_CS;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd |= E1000_EECD_CS;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	KeStallExecutionProcessor(delay_usec);

	LowerEECClock(Register0, &eecd);

	DbgPrint("StandbyNvm: leave\n");
}

VOID
ReleaseNvm(
	IN PUCHAR Register0
	)
{
	ULONG eecd = 0;

	DbgPrint("ReleaseNvm: enter\n");

	StopNvm(Register0);
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	eecd &= ~E1000_EECD_REQ;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_EECD),
		&eecd,
		1
		);
	DbgPrint("ReleaseNvm: leave\n");
}

NTSTATUS
ReadNvm(
	IN PUCHAR Register0,
	IN USHORT offset,
	IN USHORT words,
	OUT PUSHORT data
	)
{
	NTSTATUS status = STATUS_SUCCESS;
	ULONG i;
	UCHAR readOpcode = NVM_READ_OPCODE_MICROWIRE;

	DbgPrint("ReadNvm: enter\n");
	DbgPrint("ReadNvm: offset=%u, words=%u\n", offset, words);

	AcquireNvm(Register0);
	PrepareNvm(Register0);

	for (i = 0; i < words; i++) {
		/* opcode_bits = 3 implies that width of readOpcode is 3bit?*/
		ShiftOutEEC(Register0,
			readOpcode,
			opcode_bits
			);
		ShiftOutEEC(Register0,
			(USHORT)(offset + i),
			address_bits
			);
		data[i] = ShiftInEEC(Register0, 16);
		StandbyNvm(Register0);
	}
	ReleaseNvm(Register0);

	DbgPrint("ReadNvm: leave\n");
	return STATUS_SUCCESS;
}

VOID
ReloadNvm(
	IN PUCHAR Register0
	)
{
	ULONG val = 0;

	KeStallExecutionProcessor(10);

/* This register and the Device Control register (CTRL) controls the major operational modes for the
 * Ethernet controller. CTRL_EXT provides extended control of the Ethernet controller functionality
 * over the Device Control register (CTRL).
 */
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_CTRL_EXT),
		&val,
		1
		);
	val |= E1000_CTRL_EXT_EE_RST;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_CTRL_EXT),
		&val,
		1
		);
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_STATUS),
		&val,
		1
		);
}

VOID
ResetHardware(
	IN PUCHAR Register0
	)
{
	unsigned long val = 0;

	/* Masking off all interrupts */
	val = 0xffffffff;
/* Software blocks interrupts by clearing the corresponding mask bit. This is accomplished by writing
 * a 1b to the corresponding bit in this register. Bits written with 0b are unchanged (their mask status
 * does not change).
 */
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_IMC),
		&val,
		1
		);

	val = 0;
/* This register controls all Ethernet controller receiver functions. */
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_RCTL),
		&val,
		1
		);
	val = E1000_TCTL_PSP;
/* This register controls all transmit functions for the Ethernet controller.*/
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_TCTL),
		&val,
		1
		);

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_STATUS),
		&val,
		1
		);

	KeStallExecutionProcessor(10);

	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_CTRL),
		&val,
		1
		);
	val |= E1000_CTRL_RST;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_CTRL),
		&val,
		1
		);

	ReloadNvm(Register0);
	KeStallExecutionProcessor(2);

	val = 0xffffffff;
	WRITE_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_IMC),
		&val,
		1
		);
/* All register bits are cleared upon read. As a result, reading this register implicitly acknowledges
 * any pending interrupt events. Writing a 1b to any bit in the register also clears that bit. Writing a 0b
 * to any bit has no effect on that bit.
 */
	READ_REGISTER_BUFFER_ULONG((PULONG)(Register0 + E1000_ICR),
		&val,
		1
		);
}

VOID
ReadMacAddr(
	IN PUCHAR Register0
	)
{
	int i;
	unsigned short offset, nvm_data;

/* Ethernet Address (Words 00h-02h)
 * The Ethernet Individual Address (IA) is a six-byte field that must be unique for each Ethernet port
 * (and unique for each copy of the EEPROM image). The first three bytes are vendor specific. The
 * value from this field is loaded into the Receive Address Register 0 (RAL0/RAH0). For a MAC
 * address of 12-34-56-78-90-AB, words 2:0 load as follows (note that these words are byteswapped):
 * Word 0 = 3412
 * Word 1 = 7856
 * Word 2 - AB90
 */
	DbgPrint("MacAddr =>");
	for (i = 0; i < ETH_ADDR_LEN; i += 2) {
		offset = i >> 1;
		ReadNvm(Register0, offset, 1, &nvm_data);
		DbgPrint(" %02x %02x", nvm_data & 0xFF, (nvm_data >> 8));
	}
	DbgPrint("\n");
}
#endif