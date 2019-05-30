/*++

Module Name:

	FsFilter.c

Abstract:

	This is the main module of the FsFilter miniFilter driver.

Environment:

	Kernel mode

--*/

#include "FsFilter.h"

#pragma prefast(disable:__WARNING_ENCODE_MEMBER_FUNCTION_POINTER, "Not valid for kernel mode drivers")

//  Structure that contains all the global data structures used throughout the driver.

EXTERN_C_START

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
);

DRIVER_INITIALIZE DriverEntry;

EXTERN_C_END

//
//  Constant FLT_REGISTRATION structure for our filter.  This
//  initializes the callback routines our filter wants to register
//  for.  This is only used to register with the filter manager
//

CONST FLT_OPERATION_REGISTRATION Callbacks[] = {
	{IRP_MJ_CREATE, 0, FSPreOperation, FSPostOperation},
	//{IRP_MJ_CLOSE, 0, FSPreOperation, FSPostOperation},
	{IRP_MJ_READ, 0, FSPreOperation, FSPostOperation},
	{IRP_MJ_CLEANUP, 0, FSPreOperation, NULL},
	{IRP_MJ_WRITE, 0, FSPreOperation, NULL},
	{IRP_MJ_SET_INFORMATION, 0, FSPreOperation, NULL},
	{ IRP_MJ_OPERATION_END }
};

/*++

FilterRegistration Defines what we want to filter with the driver

--*/
CONST FLT_REGISTRATION FilterRegistration = {
	sizeof(FLT_REGISTRATION),			//  Size
	FLT_REGISTRATION_VERSION,           //  Version
	0,                                  //  Flags
	NULL,								//  Context Registration.
	Callbacks,                          //  Operation callbacks
	FSUnloadDriver,                     //  FilterUnload
	FSInstanceSetup,					//  InstanceSetup
	FSInstanceQueryTeardown,			//  InstanceQueryTeardown
	FSInstanceTeardownStart,            //  InstanceTeardownStart
	FSInstanceTeardownComplete,         //  InstanceTeardownComplete
	NULL,                               //  GenerateFileName
	NULL,                               //  GenerateDestinationFileName
	NULL                                //  NormalizeNameComponent
};

////////////////////////////////////////////////////////////////////////////
//
//    Filter initialization and unload routines.
//
////////////////////////////////////////////////////////////////////////////

NTSTATUS
DriverEntry(
	_In_ PDRIVER_OBJECT DriverObject,
	_In_ PUNICODE_STRING RegistryPath
)
/*++

Routine Description:

	This is the initialization routine for the Filter driver.  This
	registers the Filter with the filter manager and initializes all
	its global data structures.

Arguments:

	DriverObject - Pointer to driver object created by the system to
		represent this driver.

	RegistryPath - Unicode string identifying where the parameters for this
		driver are located in the registry.

Return Value:

	Returns STATUS_SUCCESS.
--*/
{
	UNREFERENCED_PARAMETER(RegistryPath);
	NTSTATUS status;

	//
	//  Default to NonPagedPoolNx for non paged pool allocations where supported.
	//

	ExInitializeDriverRuntime(DrvRtPoolNxOptIn);

	//
	//  Register with filter manager.
	//

	driverData = new DriverData(DriverObject);
	if (driverData == NULL) {
		return STATUS_MEMORY_NOT_ALLOCATED;
	}


	PFLT_FILTER* FilterAdd = driverData->getFilterAdd();

	status = FltRegisterFilter(DriverObject,
		&FilterRegistration,
		FilterAdd);


	if (!NT_SUCCESS(status)) {
		delete driverData;
		return status;
	}

	commHandle = new CommHandler(driverData->getFilter());
	if (commHandle == NULL) {
		delete driverData;
		return STATUS_MEMORY_NOT_ALLOCATED;
	}

	status = InitCommData();

	if (!NT_SUCCESS(status)) {
		FltUnregisterFilter(driverData->getFilter());
		delete driverData;
		delete commHandle;
		return status;
	}
	//
	//  Start filtering I/O.
	//
	status = FltStartFiltering(driverData->getFilter());

	if (!NT_SUCCESS(status)) {


		CommClose();
		FltUnregisterFilter(driverData->getFilter());
		delete driverData;
		delete commHandle;
		return status;
	}
	driverData->setFilterStart();
	DbgPrint("loaded scanner successfully");
	return STATUS_SUCCESS;
}

NTSTATUS
FSUnloadDriver(
	_In_ FLT_FILTER_UNLOAD_FLAGS Flags
)
/*++

Routine Description:

	This is the unload routine for the Filter driver.  This unregisters the
	Filter with the filter manager and frees any allocated global data
	structures.

Arguments:

	None.

Return Value:

	Returns the final status of the deallocation routines.

--*/
{
	UNREFERENCED_PARAMETER(Flags);

	//
	//  Close the server port.
	//
	driverData->setFilterStop();
	CommClose();

	//
	//  Unregister the filter
	//

	FltUnregisterFilter(driverData->getFilter());

	delete driverData;
	delete commHandle;

	return STATUS_SUCCESS;
}

NTSTATUS
FSInstanceSetup(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_SETUP_FLAGS Flags,
	_In_ DEVICE_TYPE VolumeDeviceType,
	_In_ FLT_FILESYSTEM_TYPE VolumeFilesystemType
)
/*++

Routine Description:

This routine is called whenever a new instance is created on a volume. This
gives us a chance to decide if we need to attach to this volume or not.

If this routine is not defined in the registration structure, automatic
instances are always created.

Arguments:

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance and its associated volume.

Flags - Flags describing the reason for this attach request.

Return Value:

STATUS_SUCCESS - attach
STATUS_FLT_DO_NOT_ATTACH - do not attach

--*/
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(VolumeDeviceType);
	UNREFERENCED_PARAMETER(VolumeFilesystemType);

	DbgPrint("FSFIlter: Entered FSInstanceSetup\n");

	return STATUS_SUCCESS;
}

NTSTATUS
FSInstanceQueryTeardown(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_QUERY_TEARDOWN_FLAGS Flags
)
/*++

Routine Description:

This is called when an instance is being manually deleted by a
call to FltDetachVolume or FilterDetach thereby giving us a
chance to fail that detach request.

If this routine is not defined in the registration structure, explicit
detach requests via FltDetachVolume or FilterDetach will always be
failed.

Arguments:

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance and its associated volume.

Flags - Indicating where this detach request came from.

Return Value:

Returns the status of this operation.

--*/
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	DbgPrint("FSFIlter: Entered FSInstanceQueryTeardown\n");

	return STATUS_SUCCESS;
}

VOID
FSInstanceTeardownStart(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
/*++

Routine Description:

This routine is called at the start of instance teardown.

Arguments:

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance and its associated volume.

Flags - Reason why this instance is being deleted.

Return Value:

None.

--*/
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);

	DbgPrint("FSFIlter: Entered FSInstanceTeardownStart\n");
}

VOID
FSInstanceTeardownComplete(
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_ FLT_INSTANCE_TEARDOWN_FLAGS Flags
)
/*++

Routine Description:

This routine is called at the end of instance teardown.

Arguments:

FltObjects - Pointer to the FLT_RELATED_OBJECTS data structure containing
opaque handles to this filter, instance and its associated volume.

Flags - Reason why this instance is being deleted.

Return Value:

None.

--*/
{
	UNREFERENCED_PARAMETER(FltObjects);
	UNREFERENCED_PARAMETER(Flags);
	DbgPrint("FSFIlter: Entered FSInstanceTeardownComplete\n");
}

FLT_PREOP_CALLBACK_STATUS
FSPreOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
)
/*++

Routine Description:

	Pre operations callback

Arguments:

	Data - The structure which describes the operation parameters.

	FltObject - The structure which describes the objects affected by this
		operation.

	CompletionContext - Output parameter which can be used to pass a context
		from this pre-create callback to the post-create callback.

Return Value:

   FLT_PREOP_SUCCESS_WITH_CALLBACK - If this is not our user-mode process.
   FLT_PREOP_SUCCESS_NO_CALLBACK - All other threads.

--*/
{
	NTSTATUS hr;

	//  See if this create is being done by our user process.

	if (FltGetRequestorProcessId(Data) == driverData->getPID()) {

		DbgPrint("!!! FSFilter: Allowing pre op for trusted process, no post op\n");

		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	//UNICODE_STRING name;
	//WCHAR strBuffer[(sizeof(UNICODE_STRING) / sizeof(WCHAR)) + 260];
	//name = (UNICODE_STRING)&strBuffer;
	//name.Buffer = &strBuffer[sizeof(UNICODE_STRING) / sizeof(WCHAR)];
	//name.Length = 0x0;
	//name.MaximumLength = 260 * sizeof(WCHAR);

	//PEPROCESS proc = FltGetRequestorProcess(Data);
	//hr = GetProcessImageName(proc, &name);
	
	//print process name and pid
	//if (NT_SUCCESS(hr)) {
	DbgPrint("Enter preop for irp: %s, pid of process: %u, requestorMode: %d \n", FltGetIrpName(Data->Iopb->MajorFunction), FltGetRequestorProcessId(Data), Data->RequestorMode);
		//ExFreePoolWithTag(name, 'PUR');
	//}

	if (FltObjects->FileObject == NULL) { //no file object
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// create tested only on post op, cant check here
	if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
		return FLT_PREOP_SUCCESS_WITH_CALLBACK;
	}

	hr = FSProcessPreOperartion(Data, FltObjects, CompletionContext);
	if (hr == FLT_PREOP_SUCCESS_WITH_CALLBACK) return FLT_PREOP_SUCCESS_WITH_CALLBACK;

	return FLT_PREOP_SUCCESS_NO_CALLBACK;
}

NTSTATUS
FSProcessPreOperartion(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_Flt_CompletionContext_Outptr_ PVOID *CompletionContext
) 
{

	NTSTATUS hr = FLT_PREOP_SUCCESS_NO_CALLBACK;

	PFLT_FILE_NAME_INFORMATION nameInfo;
	hr = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, &nameInfo);
	if (!NT_SUCCESS(hr)) {
		DbgPrint("!!! FSFilter: Failed to get name information from file\n");
		return hr;
	}

	BOOLEAN isDir;
	hr = FltIsDirectory(Data->Iopb->TargetFileObject, Data->Iopb->TargetInstance, &isDir);
	if (!NT_SUCCESS(hr)) {
		return hr;
	}
	if (isDir)
	{
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if (driverData->isFilterClosed()) {
		DbgPrint("!!! FSFilter: Filter is closed, skipping data\n");
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	// no communication
	/*
	if (IsCommClosed()) {
		DbgPrint("!!! FSFilter: PreOp skipped due to port closed \n");
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}*/

	PIRP_ENTRY newEntry = new IRP_ENTRY();
	if (newEntry == NULL) {
		return hr;
	}
	PDRIVER_MESSAGE newItem = &newEntry->data;

	//get pid
	newItem->PID = FltGetRequestorProcessId(Data);

	// get file id
	FILE_OBJECTID_INFORMATION fileInformation;
	hr = FltQueryInformationFile(Data->Iopb->TargetInstance,
		Data->Iopb->TargetFileObject,
		&fileInformation,
		sizeof(FILE_OBJECTID_INFORMATION),
		FileInternalInformation,
		NULL);
	RtlCopyMemory(newItem->FileID, fileInformation.ObjectId, FILE_OBJECT_ID_SIZE);

	hr = FltParseFileNameInformation(nameInfo);
	if (!NT_SUCCESS(hr)) {
		delete newEntry;
		return hr;
	}
	
	UNICODE_STRING FilePath;
	FilePath.MaximumLength = 2 * MAX_FILE_NAME_LENGTH;
	hr = FSAllocateUnicodeString(&FilePath);
	if (!NT_SUCCESS(hr)) {
		delete newEntry;
		return hr;
	}

	hr = FSEntrySetFileName(FltObjects->Volume, nameInfo, &FilePath);
	if (!NT_SUCCESS(hr)) {
		DbgPrint("!!! FSFilter: Failed to set file name on irp\n");
		delete newEntry;
		return hr;
	}/*
	if (!FSIsFileNameInScanDirs(FilePath)) {
		DbgPrint("!!! FSFilter: Skipping uninterented file, not in scan area \n");
		delete newEntry;
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}*/
	FSFreeUnicodeString(&FilePath);

	hr = FLT_PREOP_SUCCESS_NO_CALLBACK;
	// reset
	newItem->FileChange = FILE_CHANGE_NOT_SET;
	newItem->Entropy = 0;
	newItem->isEntropyCalc = FALSE;
	newItem->MemSizeUsed = 0;
	
	switch (Data->Iopb->MajorFunction) {

		//create is handled on post operation, read is created here but calculated on post(data avilable
	case IRP_MJ_READ:
	{
		newItem->IRP_OP = IRP_READ;
		if (Data->Iopb->Parameters.Read.Length == 0) // no data to read
		{
			break;
		}
		// save context for post, we calculate the entropy of read, we pass the irp to application on post op
		*CompletionContext = newEntry;
		return FLT_PREOP_SUCCESS_WITH_CALLBACK;
	}
	case IRP_MJ_CLEANUP:
		newItem->IRP_OP = IRP_CLEANUP;
		break;
	case IRP_MJ_WRITE:
	{
		newItem->IRP_OP = IRP_WRITE;
		PVOID writeBuffer = NULL;
		if (Data->Iopb->Parameters.Write.Length == 0) // no data to write
		{
			break;
		}

		// prepare buffer for entropy calc
		if (Data->Iopb->Parameters.Write.MdlAddress == NULL) { //there's mdl buffer, we use it
			writeBuffer = Data->Iopb->Parameters.Write.WriteBuffer;
		}
		else {
			writeBuffer = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Write.MdlAddress, NormalPagePriority | MdlMappingNoExecute);
		}
		if (writeBuffer == NULL) { // alloc failed
			delete newEntry;
			// fail the irp request
			Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
			Data->IoStatus.Information = 0;
			return FLT_PREOP_COMPLETE;
		}
		newItem->MemSizeUsed = Data->Iopb->Parameters.Write.Length;
		// we catch EXCEPTION_EXECUTE_HANDLER so to prevent crash when calculating
		__try {
			newItem->Entropy = shannonEntropy((PUCHAR)writeBuffer, newItem->MemSizeUsed);
			newItem->isEntropyCalc = TRUE;

		}
		__except (EXCEPTION_EXECUTE_HANDLER) {
			DbgPrint("!!! FSFilter: Failed to calc entropy\n");
			delete newEntry;
			// fail the irp request
			Data->IoStatus.Status = STATUS_INTERNAL_ERROR;
			Data->IoStatus.Information = 0;
			return FLT_PREOP_COMPLETE;
		}
	}
		break;
	case IRP_MJ_SET_INFORMATION:
	{
		newItem->IRP_OP = IRP_SETINFO;
		// we check for delete later and renaming
		FILE_INFORMATION_CLASS fileInfo = Data->Iopb->Parameters.SetFileInformation.FileInformationClass;

		if (fileInfo == FileDispositionInformation && // handle delete later
			(((PFILE_DISPOSITION_INFORMATION)(Data->Iopb->Parameters.SetFileInformation.InfoBuffer))->DeleteFile))
		{
			newItem->FileChange = FILE_CHANGE_DELETE_FILE;
		}
		else if (fileInfo == FileDispositionInformationEx &&
			FlagOn(((PFILE_DISPOSITION_INFORMATION_EX)(Data->Iopb->Parameters.SetFileInformation.InfoBuffer))->Flags, FILE_DISPOSITION_DELETE)) {
			newItem->FileChange = FILE_CHANGE_DELETE_FILE;
		}
		else if (fileInfo == FileRenameInformation || fileInfo == FileRenameInformationEx) {
			newItem->FileChange = FILE_CHANGE_RENAME_FILE;
			// TODO: get new name?
		}
		else {
			delete newEntry;
			return FLT_PREOP_SUCCESS_NO_CALLBACK;
		}
	}
		break;
	default :
		delete newEntry;
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}
	//delete newEntry;
	DbgPrint("!!! FSFilter: Addung entry to irps\n");
	driverData->AddIrpMessage(newEntry);
	
	return hr;
}

FLT_POSTOP_CALLBACK_STATUS
FSPostOperation(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
/*++

Routine Description:

	Post opeartion callback. we reach here in case of IRP_MJ_CREATE or IRP_MJ_READ

Arguments:

	Data - The structure which describes the operation parameters.

	FltObject - The structure which describes the objects affected by this
		operation.

	CompletionContext - The operation context passed fron the pre-create
		callback.

	Flags - Flags to say why we are getting this post-operation callback.

Return Value:

	FLT_POSTOP_FINISHED_PROCESSING - ok to open the file or we wish to deny
									 access to this file, hence undo the open

--*/
{
	UNREFERENCED_PARAMETER(CompletionContext);
	UNREFERENCED_PARAMETER(Flags);
	FLT_POSTOP_CALLBACK_STATUS returnStatus = FLT_POSTOP_FINISHED_PROCESSING;

	//
	//  If this create was failing anyway, don't bother scanning now.
	//
	DbgPrint("!!! FSFilter: Enter post op for irp: %s, pid of process: %u\n", FltGetIrpName(Data->Iopb->MajorFunction), FltGetRequestorProcessId(Data));
	if (!NT_SUCCESS(Data->IoStatus.Status) ||
		(STATUS_REPARSE == Data->IoStatus.Status)) {
		DbgPrint("!!! FSFilter: finished post operation, already failed \n");
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (Data->Iopb->MajorFunction == IRP_MJ_CREATE) {
		returnStatus = FSProcessCreateIrp(Data, FltObjects);
	}
	else if (Data->Iopb->MajorFunction == IRP_MJ_READ) {
		returnStatus = FSProcessPostReadIrp(Data, FltObjects, CompletionContext, Flags);
	}
	else {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	return returnStatus;
}

FLT_POSTOP_CALLBACK_STATUS
FSProcessCreateIrp(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects
)
{
	//FIXME: add check for connection to user
	NTSTATUS hr;
	if (FlagOn(Data->Iopb->OperationFlags, SL_OPEN_TARGET_DIRECTORY) || FlagOn(Data->Iopb->OperationFlags, SL_OPEN_PAGING_FILE)) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	BOOLEAN isDir;
	hr = FltIsDirectory(Data->Iopb->TargetFileObject, Data->Iopb->TargetInstance, &isDir);
	if (!NT_SUCCESS(hr)) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	if (isDir || driverData->isFilterClosed() /*|| IsCommClosed()*/)
	{
		DbgPrint("!!! FSFilter: Dir, filter closed or comm closed, return \n");
		return FLT_POSTOP_FINISHED_PROCESSING;
	}


	PIRP_ENTRY newEntry = new IRP_ENTRY();
	if (newEntry == NULL) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	PDRIVER_MESSAGE newItem = &newEntry->data;



	newItem->PID = FltGetRequestorProcessId(Data);
	newItem->IRP_OP = IRP_CREATE;

	// get file id
	FILE_OBJECTID_INFORMATION fileInformation;
	hr = FltQueryInformationFile(Data->Iopb->TargetInstance,
		Data->Iopb->TargetFileObject,
		&fileInformation,
		sizeof(FILE_OBJECTID_INFORMATION),
		FileInternalInformation,
		NULL);
	RtlCopyMemory(newItem->FileID, fileInformation.ObjectId, FILE_OBJECT_ID_SIZE);

	PFLT_FILE_NAME_INFORMATION nameInfo;
	hr = FltGetFileNameInformation(Data, FLT_FILE_NAME_OPENED | FLT_FILE_NAME_QUERY_ALWAYS_ALLOW_CACHE_LOOKUP, &nameInfo);
	if (!NT_SUCCESS(hr))
	{
		delete newEntry;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	hr = FltParseFileNameInformation(nameInfo);
	if (!NT_SUCCESS(hr)) {
		delete newEntry;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	UNICODE_STRING FilePath;
	FilePath.MaximumLength = 2 * MAX_FILE_NAME_LENGTH;
	hr = FSAllocateUnicodeString(&FilePath);
	if (!NT_SUCCESS(hr)) {
		delete newEntry;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	hr = FSEntrySetFileName(FltObjects->Volume, nameInfo, &FilePath);
	if (!NT_SUCCESS(hr)) {
		DbgPrint("!!! FSFilter: Failed to set file name on irp\n");
		delete newEntry;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}/*
	if (!FSIsFileNameInScanDirs(FilePath)) {
		DbgPrint("!!! FSFilter: Skipping uninterented file, not in scan area \n");
		delete newEntry;
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}*/
	
	// reset entropy
	newItem->FileChange = FILE_CHANGE_NOT_SET;
	newItem->Entropy = 0;
	newItem->isEntropyCalc = FALSE;

	if ((Data->IoStatus.Information) == FILE_OVERWRITTEN) {
		newItem->FileChange = FILE_CHANGE_DELETE_NEW_FILE;
	} else  if (FlagOn(Data->Iopb->Parameters.Create.Options, FILE_DELETE_ON_CLOSE)) {
		newItem->FileChange = FILE_CHANGE_DELETE_FILE;
		if ((Data->IoStatus.Information) == FILE_CREATED) {
			newItem->FileChange = FILE_CHANGE_DELETE_NEW_FILE;
		}
	}
	else {
		if ((Data->IoStatus.Information) == FILE_CREATED) {
			newItem->FileChange = FILE_CHANGE_NEW_FILE;
		}
	}
	DbgPrint("!!! FSFilter: Addung entry to irps\n");
	driverData->AddIrpMessage(newEntry);
	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
FSProcessPostReadIrp(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	//FIXME: add check for connection to user
	if (CompletionContext == NULL) {
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	if (driverData->isFilterClosed() /*|| IsCommClosed()*/) {
		DbgPrint("!!! FSFilter: Post op read, comm or filter closed\n");
		return FLT_POSTOP_FINISHED_PROCESSING;
	}

	PIRP_ENTRY entry = (PIRP_ENTRY)CompletionContext;

	FLT_POSTOP_CALLBACK_STATUS status = FLT_POSTOP_FINISHED_PROCESSING;
	
	PVOID ReadBuffer = NULL;

	// prepare buffer for entropy calc
	if (Data->Iopb->Parameters.Read.MdlAddress != NULL) { //there's mdl buffer, we use it
		ReadBuffer = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Read.MdlAddress, NormalPagePriority | MdlMappingNoExecute);

	}
	else if (FlagOn(Data->Flags, FLTFL_CALLBACK_DATA_SYSTEM_BUFFER)) //safe 
	{
		ReadBuffer = Data->Iopb->Parameters.Read.ReadBuffer;
	} else
	{
		if (FltDoCompletionProcessingWhenSafe(Data, FltObjects, CompletionContext, Flags, FSProcessPostReadSafe, &status)) { //post to worker thread or run if irql is ok
			return FLT_POSTOP_FINISHED_PROCESSING;
		}
		else {
			Data->IoStatus.Status = STATUS_INTERNAL_ERROR;
			Data->IoStatus.Information = 0;
			return status;
		}
	}
	if (!ReadBuffer)
	{
		delete entry;
		Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		Data->IoStatus.Information = 0;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	entry->data.MemSizeUsed = (ULONG)Data->IoStatus.Information; //successful read data
	// we catch EXCEPTION_EXECUTE_HANDLER so to prevent crash when calculating
	__try {
		entry->data.Entropy = shannonEntropy((PUCHAR)ReadBuffer, Data->IoStatus.Information);
		entry->data.isEntropyCalc = TRUE;

	}
	__except (EXCEPTION_EXECUTE_HANDLER) {
		delete entry;
		// fail the irp request
		Data->IoStatus.Status = STATUS_INTERNAL_ERROR;
		Data->IoStatus.Information = 0;
		return FLT_POSTOP_FINISHED_PROCESSING;
	}
	DbgPrint("!!! FSFilter: Addung entry to irps\n");
	driverData->AddIrpMessage(entry);
	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
FSProcessPostReadSafe(
	_Inout_ PFLT_CALLBACK_DATA Data,
	_In_ PCFLT_RELATED_OBJECTS FltObjects,
	_In_opt_ PVOID CompletionContext,
	_In_ FLT_POST_OPERATION_FLAGS Flags
)
{
	UNREFERENCED_PARAMETER(Flags);
	UNREFERENCED_PARAMETER(FltObjects);

	NTSTATUS status = STATUS_SUCCESS;
	PIRP_ENTRY entry = (PIRP_ENTRY)CompletionContext;
	ASSERT(entry != nullptr);
	status = FltLockUserBuffer(Data);
	if (NT_SUCCESS(status))
	{
		PVOID ReadBuffer = MmGetSystemAddressForMdlSafe(Data->Iopb->Parameters.Read.MdlAddress, NormalPagePriority | MdlMappingNoExecute);
		if (ReadBuffer != NULL) {
			__try {
				entry->data.Entropy = shannonEntropy((PUCHAR)ReadBuffer, Data->IoStatus.Information);
				entry->data.MemSizeUsed = Data->IoStatus.Information;
				entry->data.isEntropyCalc = TRUE;
				DbgPrint("!!! FSFilter: Addung entry to irps\n");
				driverData->AddIrpMessage(entry);
				return FLT_POSTOP_FINISHED_PROCESSING;
			}
			__except(EXCEPTION_EXECUTE_HANDLER) {
				status = STATUS_INTERNAL_ERROR;
			}

		}
		status = STATUS_INSUFFICIENT_RESOURCES;

	}
	delete entry;
	Data->IoStatus.Status = status;
	Data->IoStatus.Information = 0;

	return FLT_POSTOP_FINISHED_PROCESSING;
}

BOOLEAN
FSIsFileNameInScanDirs(
	CONST LPCWSTR path
) 
{
	//ASSERT(driverData != NULL);
	return driverData->IsContainingDirectory(path);
}

NTSTATUS
FSEntrySetFileName(
	CONST PFLT_VOLUME Volume,
	PFLT_FILE_NAME_INFORMATION nameInfo,
	PUNICODE_STRING uString
)
{
	//UNREFERENCED_PARAMETER(Volume);
	//UNREFERENCED_PARAMETER(nameInfo);
	//UNREFERENCED_PARAMETER(uString);
	NTSTATUS hr = STATUS_SUCCESS;
	PDEVICE_OBJECT devObject;
	USHORT volumeDosNameSize;
	USHORT finalNameSize;
	USHORT volumeNameSize = nameInfo->Volume.Length; // in bytes
	USHORT origNameSize = nameInfo->Name.Length; // in bytes
	
	WCHAR newTemp[MAX_FILE_NAME_LENGTH];

	UNICODE_STRING volumeData;
	volumeData.MaximumLength = 2 * MAX_FILE_NAME_LENGTH;
	volumeData.Buffer = newTemp;
	volumeData.Length = 0;
	
	hr = FltGetDiskDeviceObject(Volume, &devObject);
	if (!NT_SUCCESS(hr)) {
		return hr;
	}
	hr = IoVolumeDeviceToDosName(devObject, &volumeData);
	if (!NT_SUCCESS(hr)) {
		return hr;
	}

	volumeDosNameSize = volumeData.Length;
	finalNameSize = origNameSize - volumeNameSize + volumeDosNameSize; // not null terminated, in bytes

	DbgPrint("Volume name: %wZ, Size: %d, finalNameSize: %d, volumeNameSize: %d\n", volumeData, volumeDosNameSize, finalNameSize, volumeNameSize);
	DbgPrint("Name buffer: %wZ\n", nameInfo->Name);
	
	if (uString == NULL) {
		return STATUS_INVALID_ADDRESS;
	}
	if (volumeNameSize == origNameSize) { // file is the volume, don't need to do anything

		return RtlUnicodeStringCopy(uString, &nameInfo->Name);
	}
	
	if (NT_SUCCESS(hr = RtlUnicodeStringCopy(uString, &volumeData))) {// prefix of volume e.g. C:

		DbgPrint("File name: %wZ\n", uString);
		RtlCopyMemory(uString->Buffer + (volumeDosNameSize / 2),
			nameInfo->Name.Buffer + (volumeNameSize / 2),
			((finalNameSize - volumeDosNameSize > 2 * MAX_FILE_NAME_LENGTH - volumeDosNameSize) ? ( 2* MAX_FILE_NAME_LENGTH - volumeDosNameSize) : (finalNameSize - volumeDosNameSize))
		);
		uString->Length = (finalNameSize > 2 * MAX_FILE_NAME_LENGTH) ? 2 * MAX_FILE_NAME_LENGTH : finalNameSize;
		DbgPrint("File name: %wZ\n", uString);	
	}
	return hr;
}

NTSTATUS
GetProcessImageName(
    PEPROCESS eProcess,
    PUNICODE_STRING ProcessImageName
    )
{
    NTSTATUS status = STATUS_UNSUCCESSFUL;
    ULONG returnedLength;
    HANDLE hProcess = NULL;

    PAGED_CODE(); // this eliminates the possibility of the IDLE Thread/Process

    if (eProcess == NULL)
    {
        return STATUS_INVALID_PARAMETER_1;
    }

    status = ObOpenObjectByPointer(eProcess,
        0, NULL, 0, 0, KernelMode, &hProcess);
    if (!NT_SUCCESS(status))
    {
        DbgPrint("ObOpenObjectByPointer Failed: %08x\n", status);
        return status;
    }

    if (ZwQueryInformationProcess == NULL)
    {
        UNICODE_STRING routineName = RTL_CONSTANT_STRING(L"ZwQueryInformationProcess");

        ZwQueryInformationProcess =
            (QUERY_INFO_PROCESS)MmGetSystemRoutineAddress(&routineName);

        if (ZwQueryInformationProcess == NULL)
        {
            DbgPrint("Cannot resolve ZwQueryInformationProcess\n");
            status = STATUS_UNSUCCESSFUL;
            goto cleanUp;
        }
    }

    /* Query the actual size of the process path */
    status = ZwQueryInformationProcess(hProcess,
        ProcessImageFileName,
        NULL, // buffer
        0,    // buffer size
        &returnedLength);

    if (STATUS_INFO_LENGTH_MISMATCH != status) {
        DbgPrint("ZwQueryInformationProcess status = %x\n", status);
        goto cleanUp;
    }
	//*ProcessImageName = (PUNICODE_STRING)ExAllocatePoolWithTag(NonPagedPool, 512, 'PUR');
	
    if (ProcessImageName == NULL)
    {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto cleanUp;
    }

    /* Retrieve the process path from the handle to the process */
    status = ZwQueryInformationProcess(hProcess,
        ProcessImageFileName,
        ProcessImageName,
        returnedLength,
        &returnedLength);

    //if (!NT_SUCCESS(status)) ExFreePoolWithTag(*ProcessImageName, 'PUR');

cleanUp:

    ZwClose(hProcess);

    return status;
}