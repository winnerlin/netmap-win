#include <ntddk.h>
#include <wdm.h>

#include "e1000_defines.h"
#include "e1000_regs.h"

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
	IN PVOID Context
	)
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

// Access a device's configuration space.
// http://msdn.microsoft.com/en-us/library/windows/hardware/ff558707(v=vs.85).aspx
NTSTATUS
ReadWriteConfigSpace(
	IN PDEVICE_OBJECT  DeviceObject,
	IN ULONG  ReadOrWrite,  // 0 for read, 1 for write
	IN PVOID  Buffer,
	IN ULONG  Offset,
	IN ULONG  Length
	)
{
	KEVENT event;
	NTSTATUS status;
	PIRP irp;
	IO_STATUS_BLOCK ioStatusBlock;
	PIO_STACK_LOCATION irpStack;
	PDEVICE_OBJECT targetObject;

	PAGED_CODE();
	KeInitializeEvent(&event, NotificationEvent, FALSE);
	targetObject = IoGetAttachedDeviceReference(DeviceObject);
	irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
		targetObject,
		NULL,
		0,
		NULL,
		&event,
		&ioStatusBlock);
	if (irp == NULL) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		goto End;
	}
	irpStack = IoGetNextIrpStackLocation(irp);
	if (ReadOrWrite == 0) {
		irpStack->MinorFunction = IRP_MN_READ_CONFIG;
	} else {
		irpStack->MinorFunction = IRP_MN_WRITE_CONFIG;
	}
	irpStack->Parameters.ReadWriteConfig.WhichSpace = PCI_WHICHSPACE_CONFIG;
	irpStack->Parameters.ReadWriteConfig.Buffer = Buffer;
	irpStack->Parameters.ReadWriteConfig.Offset = Offset;
	irpStack->Parameters.ReadWriteConfig.Length = Length;

	// Initialize the status to error in case the bus driver does not 
	// set it correctly.
	irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
	status = IoCallDriver(targetObject, irp);
	if (status == STATUS_PENDING) {
		KeWaitForSingleObject(&event, Executive, KernelMode, FALSE, NULL);
		status = ioStatusBlock.Status;
	}
End:
	// Done with reference
	ObDereferenceObject(targetObject);
	return status;
}


NTSTATUS
IdentifyHardware(
	IN PDEVICE_OBJECT PhisicalDeviceObject)
{
	NTSTATUS status = STATUS_SUCCESS;
	PCI_COMMON_CONFIG  pciConfig;
	unsigned int i = 0;

	DbgPrint("IdentifyHardware: enter\n");

	RtlZeroMemory(&pciConfig, sizeof(pciConfig));
	status = ReadWriteConfigSpace(PhisicalDeviceObject,
		0,
		&pciConfig,
		FIELD_OFFSET(PCI_COMMON_CONFIG, VendorID), // == 0 ?
		sizeof(pciConfig)
		);
	if (status != STATUS_SUCCESS) {
		DbgPrint("ReadWriteConfigSpace=%x\n", status);
		DbgPrint("IdentifyHardware: leave#1\n");
		return status;
	}

	DbgPrint("Command => %x\n", pciConfig.Command);

	DbgPrint("VendorID => %x\n", pciConfig.VendorID);
	DbgPrint("DeviceID => %x\n", pciConfig.DeviceID);
	DbgPrint("RevisionID => %x\n", pciConfig.RevisionID);
	DbgPrint("SubVendorID => %x\n", pciConfig.u.type0.SubVendorID);
	DbgPrint("SubSystemID => %x\n", pciConfig.u.type0.SubSystemID);

#define EM_BAR_TYPE(v)          ((v) & EM_BAR_TYPE_MASK)
#define EM_BAR_TYPE_MASK        0x00000001
#define EM_BAR_TYPE_MMEM        0x00000000
#define EM_BAR_TYPE_IO          0x00000001
#define EM_BAR_TYPE_FLASH       0x0014
#define EM_BAR_MEM_TYPE(v)      ((v) & EM_BAR_MEM_TYPE_MASK)
#define EM_BAR_MEM_TYPE_MASK    0x00000006
#define EM_BAR_MEM_TYPE_32BIT   0x00000000
#define EM_BAR_MEM_TYPE_64BIT   0x00000004

	for (i = 0; i < 7; i++) {
		DbgPrint("BaseAddresses[%i] => val=%x,type=%x\n",
			i,
			pciConfig.u.type0.BaseAddresses[i],
			EM_BAR_TYPE(pciConfig.u.type0.BaseAddresses[i])
			);
		if (EM_BAR_TYPE(pciConfig.u.type0.BaseAddresses[i]) == EM_BAR_TYPE_MMEM) {
			DbgPrint("\tmem_type=%x\n", EM_BAR_MEM_TYPE(pciConfig.u.type0.BaseAddresses[i]));
		}
	}

	DbgPrint("IdentifyHardware: leave\n");
	return status;
}

// http://msdn.microsoft.com/en-us/library/windows/hardware/ff554399(v=vs.85).aspx
NTSTATUS
AllocatePciResource(
	IN PCM_RESOURCE_LIST AllocatedResources)
{
	NTSTATUS status = STATUS_SUCCESS;
	unsigned int i, ii;
	PHYSICAL_ADDRESS memPhysAddress;
	SIZE_T memPhysLength = 0;
	PUCHAR virtualAddress = NULL;
	ULONG e1000Status = 0;

	DbgPrint("AllocatePciResource: enter\n");
	for (i = 0; i < AllocatedResources->Count; i++) {
		DbgPrint("List[%d] => InterfaceType=%x,BusNumber=%x,Version=%x,Revision=%x\n",
			i,
			AllocatedResources->List[i].InterfaceType, // PCIBus=5
			AllocatedResources->List[i].BusNumber,
			AllocatedResources->List[i].PartialResourceList.Version,
			AllocatedResources->List[i].PartialResourceList.Revision);
		for (ii = 0; ii < AllocatedResources->List[i].PartialResourceList.Count; ii++) {
			switch (AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Type) {
			case CmResourceTypePort:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypePort,u.Port={Start=%x,Length=%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Port.Start,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Port.Length
					);
				break;
			case CmResourceTypeInterrupt:
				if ((AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.MessageInterrupt={Raw: MessageCount=%x,Vector=%x,Affinity=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.MessageCount,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.Vector,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Raw.Affinity
						);
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.MessageInterrupt={Translated: Level=%x,Vector=%x,Affinity=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Level,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Vector,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.MessageInterrupt.Translated.Affinity
						);
				} else {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeInterrupt,u.Interrupt={Level=%x,Vector=%x,Affinity=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Level,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Vector,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Interrupt.Affinity
						);
				}
				break;
			case CmResourceTypeMemory:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory={Start=%x,Length=%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Start,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Length
					);
				memPhysAddress = AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Start;
				memPhysLength = AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory.Length;
				virtualAddress = MmMapIoSpace(memPhysAddress,
					memPhysLength,
					MmNonCached
					);
				break;
			case CmResourceTypeMemoryLarge:
				if ((AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_40) != 0) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory40={Start=%x,Length40=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory40.Start,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory40.Length40
						);
				} else if (
					(AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_48) != 0
					) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory48={Start=%x,Length48=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory48.Start,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory48.Length48
						);
				} else if (
					(AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Flags & CM_RESOURCE_MEMORY_LARGE_64) != 0
					) {
					DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMemory,u.Memory64={Start=%x,Length64=%x}\n",
						ii,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory64.Start,
						AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Memory64.Length64
						);
				}
				break;
			case CmResourceTypeDma:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDma,u.Dma={Channel=%x,Port=%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Dma.Channel,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.Dma.Port
					);
				break;
			case CmResourceTypeDevicePrivate:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			case CmResourceTypeBusNumber:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeBusNumber,u.BusNumber={Start=%x,Length=%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.BusNumber.Start,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.BusNumber.Length
					);
				break;
			case CmResourceTypeDeviceSpecific:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeDeviceSpecific,u.DeviceSpecificData{DataSize=%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DeviceSpecificData.DataSize
					);
				break;
			case CmResourceTypePcCardConfig:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypePcCardConfig,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			case CmResourceTypeMfCardConfig:
				DbgPrint("\tDescriptors[%d] => Type=CmResourceTypeMfCardConfig,u.DevicePrivate={%x,%x,%x}\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[0],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[1],
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].u.DevicePrivate.Data[2]
					);
				break;
			default:
				DbgPrint("\tDescriptors[%d] => Type=%x\n",
					ii,
					AllocatedResources->List[i].PartialResourceList.PartialDescriptors[ii].Type
					);
			}
		}
	}

	if (virtualAddress != NULL) {
		READ_REGISTER_BUFFER_ULONG((PULONG)(virtualAddress + E1000_STATUS),
			&e1000Status,
			1
			);
		DbgPrint("bus => %s\n",
			(e1000Status & E1000_STATUS_PCIX_MODE) ? "PCIX" : "PCI"
			);
		if ((e1000Status & E1000_STATUS_PCIX_MODE) != 0) {
			DbgPrint("speed => %x\n",
				(e1000Status & E1000_STATUS_PCIX_SPEED)
				);
		} else {
			DbgPrint("speed => %s\n",
				(e1000Status & E1000_STATUS_PCI66) ? "66" : "33"
				);

		}
		DbgPrint("width => %d\n",
			(e1000Status & E1000_STATUS_BUS64) ? 64 : 32
			);
		MmUnmapIoSpace(virtualAddress, memPhysLength);
	}
	DbgPrint("AllocatePciResource: leave\n");
	return status;
}


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
		eecd,
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
		eecd,
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
			&eecd,
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
	address_bits = (eecd & E1000_EECD_SIZE) != 0 ? 8 : 6;
	word_size = (eecd & E1000_EECD_SIZE) != 0 ? 256 : 64;

	DbgPrint("InitNvmParam: leave\n");
}

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

	AcquireNvm(Register0);
	PrepareNvm(Register0);

	for (i = 0; i < words; i++) {
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
	if (LowerDeviceObject != NULL) {
		IoDetachDevice(LowerDeviceObject);
	}
	if (MyDeviceObject != NULL) {
		IoDeleteDevice(MyDeviceObject);
	}
	DbgPrint("Unload: leave\n");

}

NTSTATUS
DispatchPnp(
	IN PDEVICE_OBJECT DeviceObject,
	IN PIRP Irp)
{
	NTSTATUS status = STATUS_SUCCESS;
	PIO_STACK_LOCATION irpSp = IoGetCurrentIrpStackLocation(Irp);

	DbgPrint("DispatchPnp: enter\n");

	DbgPrint("DispatchPnp: MinorFunction=%x\n", irpSp->MinorFunction);
	switch (irpSp->MinorFunction) {
	case IRP_MN_START_DEVICE:
		status = ForwardIrpAndWait(LowerDeviceObject, Irp);
		if (status == STATUS_SUCCESS) {
			IdentifyHardware(MyPhysicalDeviceObject);
			AllocatePciResource(irpSp->Parameters.StartDevice.AllocatedResourcesTranslated);
/*
Command => 7
VendorID => 8086
DeviceID => 100e
RevisionID => 2
SubVendorID => 8086
SubSystemID => 1e
BaseAddresses[0] => val=f0820000,type=0
	mem_type=0
BaseAddresses[1] => val=0,type=0
	mem_type=0
BaseAddresses[2] => val=d061,type=1
BaseAddresses[3] => val=0,type=0
	mem_type=0
BaseAddresses[4] => val=0,type=0
	mem_type=0
BaseAddresses[5] => val=0,type=0
	mem_type=0
BaseAddresses[6] => val=0,type=0
	mem_type=0
IdentifyHardware: leave
AllocatedResources:
AllocatePciResource: enter
List[0] => InterfaceType=5,BusNumber=0,Version=1,Revision=1
	Descriptors[0] => Type=CmResourceTypeMemory,u.Memory={Start=f0820000,Length=0}
	Descriptors[1] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={1,0,0}
	Descriptors[2] => Type=CmResourceTypePort,u.Port={Start=d060,Length=0}
	Descriptors[3] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={1,2,0}
	Descriptors[4] => Type=CmResourceTypeInterrupt,u.Interrupt={Level=9,Vector=9,Affinity=ffffffff}
AllocatePciResource: leave
AllocatedResourcesTranslated:
AllocatePciResource: enter
List[0] => InterfaceType=5,BusNumber=0,Version=1,Revision=1
	Descriptors[0] => Type=CmResourceTypeMemory,u.Memory={Start=f0820000,Length=0}
	Descriptors[1] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={1,0,0}
	Descriptors[2] => Type=CmResourceTypePort,u.Port={Start=d060,Length=0}
	Descriptors[3] => Type=CmResourceTypeDevicePrivate,u.DevicePrivate={1,2,0}
	Descriptors[4] => Type=CmResourceTypeInterrupt,u.Interrupt={Level=12,Vector=39,Affinity=1}
*/
		}
		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		DbgPrint("DispatchPnp: leave#1\n");
		return status;
	case IRP_MN_REMOVE_DEVICE:
		status = ForwardIrpAndWait(LowerDeviceObject, Irp);

		IoDetachDevice(LowerDeviceObject);
		LowerDeviceObject = NULL;
		IoDeleteDevice(MyDeviceObject);
		MyDeviceObject = NULL;

		IoCompleteRequest(Irp, IO_NO_INCREMENT);
		DbgPrint("DispatchPnp: leave#2\n");
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
