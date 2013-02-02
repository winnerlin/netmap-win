typedef struct _E100E_DEVICE_EXTENSION {
	PDEVICE_OBJECT deviceObject;
	PDEVICE_OBJECT physicalDeviceObject;
	PDEVICE_OBJECT nextDeviceObject;
	struct e1000_osdep osdep;
	struct e1000_hw hw;
} E100E_DEVICE_EXTENSION, *PE100E_DEVICE_EXTENSION;