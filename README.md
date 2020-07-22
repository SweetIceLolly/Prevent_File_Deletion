# Prevent_File_Deletion
Record & prevent file deletion in kernel mode

Huge thanks to https://0x00sec.org/t/kernel-mode-rootkits-file-deletion-protection/7616

This guy has some awesome things! Thanks to the tutorials! https://github.com/NtRaiseHardError

# Study Notes

## What to do in `DriverEntry`
1. Use `FltRegisterFilter` to register a minifilter.
2. We can use `0x%08x` format specifier in `DbgPrint` to print error codes.
3. After `FltRegisterFilter`, use `FltStartFiltering` to start the minifilter.
4. If `FltStartFiltering` fails, we should unregister the minifilter by calling `FltUnregisterFilter`.

Now here comes the tough things...

## `FltRegisterFilter` function
For `FltRegisterFilter` function, we need three parameters:

(Reference: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltregisterfilter)

The code should be like this:

```c
PFLT_FILTER Filter;
status = FltRegisterFilter(DriverObject, &FilterRegistration, &Filter);
```

The `Registration` parameter can be a little complicated. Other parameters are straightforward. The type of `Registration` is [FLT_REGISTRATION](https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/ns-fltkernel-_flt_registration), which will be something like this:

(Reference: https://github.com/NtRaiseHardError/Anti-Delete/blob/9fbe5ac0e46ba7a70b9e9983977d1bd38a3999a0/Anti%20Delete/Anti%20Delete/Driver.c#L24)

```c
const FLT_REGISTRATION FilterRegistration = {
	sizeof(FLT_REGISTRATION),	// Size
	FLT_REGISTRATION_VERSION,	// Version
	0,							// Flags
	NULL,						// ContextRegistration
	Callbacks,					// OperationRegistration
	badgirlFilterUnload,		// FilterUnloadCallback
	NULL,						// InstanceSetupCallback
	NULL,						// InstanceQueryTeardownCallback
	NULL,						// InstanceTeardownStartCallback
	NULL,						// InstanceTeardownCompleteCallback
	NULL,						// GenerateFileNameCallback
	NULL,						// NormalizeNameComponentCallback
	NULL,						// NormalizeContextCleanupCallback
	NULL,						// TransactionNotificationCallback
	NULL,						// NormalizeNameComponentExCallback
	NULL						// SectionNotificationCallback
};
```
Note that we need to specify `OperationRegistration` and `FilterUnloadCallback` in this demo. I don't know what do other callbacks do so far.

Pay attention to `OperationRegistration`. Note that `Callbacks` should be `FLT_OPERATION_REGISTRATION`, which is an array. It binds major function and callbacks. It should be like this:

(Reference: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/ns-fltkernel-_flt_operation_registration)

```c
const FLT_OPERATION_REGISTRATION Callbacks[] = {
	{ IRP_MJ_CREATE, 0, badgirlFilterAntiDelete, NULL, NULL },
	{ IRP_MJ_SET_INFORMATION, 0, badgirlFilterAntiDelete, NULL, NULL },
    
    ...

	{ IRP_MJ_OPERATION_END }
};
```

- The 1st element specifies MajorFunction of the operation.
- The 2nd element is Flags, I don't know what's that, even after reading the documentation. Leave that with 0.
- The 3rd element is PreOperation, which is the callback function that will be called before an operation.
- The 4th element is PostOperation, which is the callback function that will be called after an operation. In this demo, we don't need PostOperation callbacks, so we set them to NULLs.
- The 5th element is reserved. Set that to NULL.
- The last element in the array must be `{ IRP_MJ_OPERATION_END }`.

So... That's what you need to do to call `FltRegisterFilter`.

## Registry
When `FltRegisterFilter` returns STATUS_OBJECT_NAME_NOT_FOUND (0xc0000034), it usually indicates there's something wrong with the registry. (Reference: https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/nf-fltkernel-fltregisterfilter#return-value) After searching for this problem, I found the reason. It is because I am too lazy and I didn't create an INF file for this driver. So the minifilter is not recognized by the system. To solve this, we need to write an appropriate INF file. (Reference: https://stackoverflow.com/questions/42389211/fltregisterfilter-not-working) The INF file for a minifilter is different from INF file for other drivers. (https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver) There are several things to notice.

### Service name
The service name that the driver loader passes to `CreateService` should be the same as the `ServiceName` specified in the INF file. Otherwise, the system won't recognize the minifilter.

### INF file
1. You need to specify `CatalogFile` to `your_driver_name.cat`, even though the documentation (https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/creating-an-inf-file-for-a-minifilter-driver?redirectedfrom=MSDN#version-section-required) says minifilter drivers except antivirus minifilter drivers should leave this entry blank (This demo is not an antivirus minifilter (320000 < Altitude < 329999), but an activity monitor minifilter (Altitude = 365000)). I don't know why, but my solution refuses to compile unless I specify the value of `CatalogFile`.
2. Change `Class` and `ClassGuid` according to your purpose. (https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/file-system-filter-driver-classes-and-class-guids)
3. Change `Instance1.Altitude` to an appropriate value according to your purpose. (https://docs.microsoft.com/en-us/windows-hardware/drivers/ifs/load-order-groups-and-altitudes-for-minifilter-drivers)
4. An example INF file: https://github.com/Microsoft/Windows-driver-samples/blob/master/filesys/miniFilter/NameChanger/NameChanger.inf
5. If the solution fails to build and says `Section xxx should have an architecture decoration`, go to project properties - Driver Settings - General - Set "Target Platform" to "Desktop". (https://github.com/microsoft/Windows-driver-samples/issues/366) This may be a better solution, but I havn't tried that: https://community.osr.com/discussion/291286/wdk10-stampinf-returns-error-1420-defaultinstall-based-inf-cannot-be-processed-as-primitive

## Linking
If the compiler says "unresolved external symbol _Flt*", you need to link fltMgr.lib. Go to project properties - Linker - Input, edit "Additional Dependencies" and add `$(DDK_LIB_PATH)fltMgr.lib`.

## IRQL
I don't have much concept about IRQL. But I noticed that every minifilter callback function (like PreOperation, FilterUnloadCallback) has `PAGED_CODE();` in its body. This is related to IRQL, but I still need to understand the concept.

## Misc
1. `FalgOn(a, b)` macro can be used to determine if a includes b. (i.e. a & b != 0 indicates a includes b)
2. We can use `FltGetFileNameInformation` and `FltParseFileNameInformation` to get file name information from `FltObjects`.
3. `Data->Thread` can be used to identify the process that's requesting the operation.
4. To prevent an operation:
```c
Data->IoStatus.Status = STATUS_ACCESS_DENIED;
Data->IoStatus.Information = 0;
ret = FLT_PREOP_COMPLETE;
```

# UPDATE: Prevent file modification
To prevent file modification, we can check flags in `Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess`. The following code will prevent rename, delete, modify and property change (Not strictly tested though). If we want to prevent the file from being read, we can deny the access when related `DesiredAccess` flag is detected.

```c
if (FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_WRITE_DATA) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_WRITE_ATTRIBUTES) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_WRITE_EA) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, FILE_APPEND_DATA) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, DELETE) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, WRITE_DAC) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, WRITE_OWNER) ||
    FlagOn(Data->Iopb->Parameters.Create.SecurityContext->DesiredAccess, GENERIC_WRITE)) {

    Data->IoStatus.Status = STATUS_ACCESS_DENIED;
    Data->IoStatus.Information = 0;
    ret = FLT_PREOP_COMPLETE;
}
```

# UPDATE: Find out which process requested the operation
To find out which process requested the operation, we can use `Data->Thread`.
```c
PEPROCESS TargetProcess = IoThreadToProcess(Data->Thread);
HANDLE Pid = PsGetProcessId(TargetProcess);
PUNICODE_STRING ProcessName = NULL;
SeLocateProcessImageName(TargetProcess, &ProcessName);
```

# What to do next
1. Currently, loading the driver will return an error code that indicates `An instance of the service is already running`. I don't know why, because there is no an intance of the service is already running.
2. Loading the driver for a second time will fail to register the minifilter, I don't know why. (Update: I found that reinstall the service will solve the problem, but still don't know why).
3. I will need to figure out how to prevent file creation and modification. (Solved)
4. I found that the minifilter will be loaded even if I whatever path I specified in the driver loader. This is weird! Maybe it's because the system puts the newly installed minifilter driver into a "pending to load" list, and it will be loaded as soon as some program loads other drivers? I don't know.
5. HRSword can delete/rename protected files without the specific `DesiredAccess`. Maybe it is because it simply passes the file handle to the kernel-mode driver and let the driver to finish the job? I am not sure.
