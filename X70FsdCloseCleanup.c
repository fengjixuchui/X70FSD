#include "X70FsdCloseCleanup.h"
#include "X70FsdData.h"
#include "X70FsdInterface.h"
extern NPAGED_LOOKASIDE_LIST  G_FcbLookasideList;
extern NPAGED_LOOKASIDE_LIST  G_CcbLookasideList;
extern NPAGED_LOOKASIDE_LIST  G_FcbHashTableLookasideList;
extern NPAGED_LOOKASIDE_LIST  G_EResourceLookasideList;
extern DYNAMIC_FUNCTION_POINTERS gDynamicFunctions;

extern USHORT gOsServicePackMajor;
extern ULONG gOsMajorVersion;
extern ULONG gOsMinorVersion;

FLT_POSTOP_CALLBACK_STATUS
PtPostOperationClose  (
					   __inout PFLT_CALLBACK_DATA Data,
					   __in PCFLT_RELATED_OBJECTS FltObjects,
					   __in_opt PVOID CompletionContext,
					   __in FLT_POST_OPERATION_FLAGS Flags
					   )
{
	UNREFERENCED_PARAMETER( Data );
	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );
	UNREFERENCED_PARAMETER( Flags );



	return FLT_POSTOP_FINISHED_PROCESSING;
}

FLT_POSTOP_CALLBACK_STATUS
PtPostOperationCleanup  (
						 __inout PFLT_CALLBACK_DATA Data,
						 __in PCFLT_RELATED_OBJECTS FltObjects,
						 __in_opt PVOID CompletionContext,
						 __in FLT_POST_OPERATION_FLAGS Flags
						 )
{
	UNREFERENCED_PARAMETER( Data );
	UNREFERENCED_PARAMETER( FltObjects );
	UNREFERENCED_PARAMETER( CompletionContext );
	UNREFERENCED_PARAMETER( Flags );



	return FLT_POSTOP_FINISHED_PROCESSING;
}
//���������ü��ٵ�ʱ����������
FLT_PREOP_CALLBACK_STATUS
PtPreOperationClose(
					__inout PFLT_CALLBACK_DATA Data,
					__in PCFLT_RELATED_OBJECTS FltObjects,
					__deref_out_opt PVOID *CompletionContext
					)
{

	FLT_PREOP_CALLBACK_STATUS	FltStatus;
	BOOLEAN TopLevel = FALSE;
	PIRP_CONTEXT IrpContext = NULL;

	PAGED_CODE();        

	FsRtlEnterFileSystem();

	if(!IsMyFakeFcb(FltObjects->FileObject))
	{
		FsRtlExitFileSystem();
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if(FLT_IS_IRP_OPERATION(Data)) //irp read
	{
		//
		//���ö������

		TopLevel = X70FsdIsIrpTopLevel( Data );

		//����irp������
		try
		{
			IrpContext = X70FsdCreateIrpContext( Data,FltObjects, CanFsdWait( Data ) );

			if(IrpContext == NULL)
			{
				X70FsdRaiseStatus(IrpContext,STATUS_INSUFFICIENT_RESOURCES);
			}

			FltStatus = X70FsdCommonClose(Data, FltObjects,IrpContext);

		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			
			DbgPrint("Close�����쳣ֱ�ӷ��� \n");
			X70FsdProcessException(&IrpContext,&Data,GetExceptionCode());
			FltStatus = FLT_PREOP_COMPLETE;
		}

		// �ָ�Top-Level IRP
		if (TopLevel) { IoSetTopLevelIrp( NULL ); }

	}
	else
	{
		Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		Data->IoStatus.Information = 0;
		FltStatus = FLT_PREOP_COMPLETE;
		/*FltStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;*/
		DbgPrint("�յ�������close���� \n");
	}

	FsRtlExitFileSystem();
	return FltStatus;
}


//����رյ�ʱ�������
FLT_PREOP_CALLBACK_STATUS
PtPreOperationCleanup(
					  __inout PFLT_CALLBACK_DATA Data,
					  __in PCFLT_RELATED_OBJECTS FltObjects,
					  __deref_out_opt PVOID *CompletionContext
					  )
{
	FLT_PREOP_CALLBACK_STATUS	FltStatus;
	BOOLEAN TopLevel = FALSE;
	PIRP_CONTEXT IrpContext = NULL;
	PAGED_CODE();

	FsRtlEnterFileSystem();

	if(!IsMyFakeFcb(FltObjects->FileObject))
	{
		FsRtlExitFileSystem();
		return FLT_PREOP_SUCCESS_NO_CALLBACK;
	}

	if(FLT_IS_IRP_OPERATION(Data)) //irp read
	{
		//
		//���ö������

		TopLevel = X70FsdIsIrpTopLevel( Data );
		
		try
		{
			IrpContext = X70FsdCreateIrpContext( Data,FltObjects, CanFsdWait( Data ) );

			if(IrpContext == NULL)
			{
				X70FsdRaiseStatus(IrpContext,STATUS_INSUFFICIENT_RESOURCES);
			}

			FltStatus = X70FsdCommonCleanup(Data, FltObjects,IrpContext);

		}
		__except(EXCEPTION_EXECUTE_HANDLER)
		{
			
			DbgPrint("Cleanup�����쳣ֱ�ӷ��� \n");
			X70FsdProcessException(&IrpContext,&Data,GetExceptionCode());
			FltStatus = FLT_PREOP_COMPLETE;
		}

		// �ָ�Top-Level IRP
		if (TopLevel) { IoSetTopLevelIrp( NULL ); }

	}
	else
	{
		Data->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
		Data->IoStatus.Information = 0;
		FltStatus = FLT_PREOP_COMPLETE;
		/*FltStatus = FLT_PREOP_SUCCESS_NO_CALLBACK;*/
		DbgPrint("�յ�������cleanup���� \n");
	}

	FsRtlExitFileSystem();
	return FltStatus;
}


VOID
X70FsdDecrementCleanupCounts (
								IN PFCB Fcb,
								IN BOOLEAN NonCachedHandle
								)
{
	InterlockedDecrement( &Fcb->OpenHandleCount );

	if (NonCachedHandle) {

		InterlockedDecrement( &Fcb->NonCachedCleanupCount );

	}
	return ;
}

FLT_PREOP_CALLBACK_STATUS
X70FsdCommonCleanup(
					  __inout PFLT_CALLBACK_DATA Data,
					  __in    PCFLT_RELATED_OBJECTS FltObjects,
					  __in	  PIRP_CONTEXT IrpContext)
{
	FLT_PREOP_CALLBACK_STATUS FltStatus = FLT_PREOP_COMPLETE;
	PFILE_OBJECT FileObject;
	LARGE_INTEGER TruncateSize;
	PFCB Fcb = NULL;
	PCCB Ccb = NULL;
	PFLT_FILE_NAME_INFORMATION nameInfo;
	OBJECT_ATTRIBUTES	ob;

	PFLT_CALLBACK_DATA RetNewCallbackData = NULL;
	PVOLUME_CONTEXT volCtx = NULL;
	BOOLEAN AcquireResource = FALSE;
	BOOLEAN FcbAcquireResource = FALSE;
	BOOLEAN AcquirePagingIoResource = FALSE;
	BOOLEAN FOResourceAcquired = FALSE;
	BOOLEAN NonCachedHandle = FALSE;
	BOOLEAN IsDisEncryptType = FALSE;

	CACHE_UNINITIALIZE_EVENT UninitializeCompleteEvent;
	NTSTATUS WaitStatus;
	BOOLEAN WaitCompleteEvent = FALSE;

	NTSTATUS Status = STATUS_SUCCESS;

	if(FltObjects == NULL)
	{
		FltObjects = &IrpContext->FltObjects;
	}

	if(FltObjects != NULL)
	{
		FileObject = FltObjects->FileObject;
	}
	else
	{
		FileObject = Data->Iopb->TargetFileObject;
	}

	//DbgPrint("�յ�Cleanup %ws \n",FileObject->FileName.Buffer);

	if (!FlagOn(IrpContext->Flags, IRP_CONTEXT_FLAG_WAIT)) 
	{

		DbgPrint("�첽��Cleanup \n");
		Status = X70FsdPostRequest( Data,IrpContext);
		if(Status == STATUS_PENDING)
		{
			FltStatus = FLT_PREOP_PENDING;
		}
		else
		{
			FltStatus = FLT_PREOP_COMPLETE;
		}
		return FltStatus;
	}
	try{

		Status = FltGetVolumeContext( FltObjects->Filter,
			FltObjects->Volume,
			&volCtx );
		if(!NT_SUCCESS(Status))
		{
			try_return(FltStatus = FLT_PREOP_COMPLETE);
		}

		Fcb = FileObject->FsContext;
		Ccb = FileObject->FsContext2;
		//�õ���Դ��close Create cleanupͬ��

		if (FlagOn( FileObject->Flags, FO_CLEANUP_COMPLETE )) 
		{

			if (FlagOn(FileObject->Flags, FO_FILE_MODIFIED) &&
				FileObject->SectionObjectPointer->DataSectionObject != NULL)
			{

				CcFlushCache( &Fcb->SectionObjectPointers, NULL, 0, &Data->IoStatus );

				ExAcquireResourceExclusiveLite( Fcb->Header.PagingIoResource, TRUE);
				ExReleaseResourceLite( Fcb->Header.PagingIoResource );

				Status = Data->IoStatus.Status;

				if (!NT_SUCCESS(Status)) 
				{

					X70FsdNormalizeAndRaiseStatus( IrpContext, Status );

				}
			}
			try_return(Status);
		}
#ifdef CV
		VirtualizerStart();
#endif

		//if(FlagOn(Ccb->CcbState,CCB_FLAG_FILE_CHANGED))
		//{
		//	ExAcquireResourceExclusiveLite( Fcb->Header.Resource, TRUE );
		//	FcbAcquireResource = TRUE;

		//	CleanupSetFile(FltObjects,Fcb,Ccb); //����޸��ļ���С�ڲ������CcSetFileSizes����һ��setfileinfo����
		//	ExReleaseResourceLite( Fcb->Header.Resource );
		//	FcbAcquireResource = FALSE;
		//}

		//����ȡ����Դ
		ExAcquireResourceExclusiveLite(volCtx->VolResource, TRUE);
		AcquireResource = TRUE;

		ExAcquireResourceExclusiveLite( Fcb->Header.Resource, TRUE );
		FcbAcquireResource = TRUE;

		if ((Fcb->OpenHandleCount == 1))
		{
			if(FlagOn(Fcb->FcbState, SCB_STATE_DELETE_ON_CLOSE))
			{
				(VOID)ExAcquireResourceExclusiveLite( Fcb->Header.PagingIoResource,
					TRUE );

				Fcb->Header.FileSize.QuadPart = 0;
				Fcb->Header.ValidDataLength.QuadPart = 0;
				Fcb->ValidDataToDisk.QuadPart = 0;

				ExReleaseResourceLite( Fcb->Header.PagingIoResource );

				SetFlag(Ccb->CcbState,CCB_FLAG_FILE_CHANGED);
			}
			else if ((Fcb->Header.ValidDataLength.QuadPart < Fcb->Header.FileSize.QuadPart)) 
			{

				(VOID)X70FsdZeroData( IrpContext,
					Fcb,
					FileObject,
					Fcb->Header.ValidDataLength.QuadPart,
					Fcb->Header.FileSize.QuadPart - Fcb->Header.ValidDataLength.QuadPart,
					volCtx->SectorSize);

				Fcb->Header.ValidDataLength.QuadPart = Fcb->Header.FileSize.QuadPart;

				if (CcIsFileCached( FileObject )) 
				{
					CcSetFileSizes( FileObject, (PCC_FILE_SIZES)&Fcb->Header.AllocationSize );
				}
			}
			if(FlagOn(Fcb->FcbState, SCB_STATE_DELETE_ON_CLOSE))
			{
				ClearFlag(Ccb->CcbState,CCB_FLAG_FILE_CHANGED);
				ClearFlag(Fcb->FcbState,SCB_STATE_FILE_CHANGED);
				RemoveFcbList(Fcb->HashValue,NULL);
			}
		}
		FltCheckOplock( &Fcb->Oplock,
			Data,
			IrpContext,
			NULL,
			NULL );

		if (Fcb->FileLock != NULL) 
		{

			(VOID) FsRtlFastUnlockAll( Fcb->FileLock,
				FileObject,
				FltGetRequestorProcess( Data ),
				NULL );
		}

		ExAcquireFastMutex(Fcb->Header.FastMutex);

		if ( FltOplockIsFastIoPossible(&Fcb->Oplock) )
		{
			if ( Fcb->FileLock && 
				Fcb->FileLock->FastIoIsQuestionable )
			{
				Fcb->Header.IsFastIoPossible = FastIoIsQuestionable;
			}
			else
			{
				Fcb->Header.IsFastIoPossible = FastIoIsPossible;
			}
		}
		else
		{
			Fcb->Header.IsFastIoPossible = FastIoIsNotPossible;
		}

		ExReleaseFastMutex(Fcb->Header.FastMutex);

		//����ļ��ı��޸��ļ�ͷ

		if(FlagOn(Ccb->CcbState,CCB_FLAG_FILE_CHANGED))
		{

			SetFlag(Fcb->FcbState,SCB_STATE_FILE_CHANGED); //����ļ��޸Ĺ�����fcb״̬

			CleanupSetFile(FltObjects,Fcb,Ccb); //����޸��ļ���С�ڲ������CcSetFileSizes����һ��setfileinfo����

			//ExtendingValidDataSetFile(FltObjects,Fcb,Ccb);

		}

		//����������ļ�,��vistaϵͳ�Ժ���Ҫ���ӻ����ж���Ľ���
		if(FlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE) && (IS_WINDOWSVISTA_OR_LAYER()))
		{
			ULONG WritableReferences = gDynamicFunctions.pMmDoesFileHaveUserWritableReferences(&Fcb->SectionObjectPointers);
			
			if(Fcb->CacheType == CACHE_READ && FileObject->WriteAccess)
			{
				if(Fcb->SectionObjectPointers.DataSectionObject != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,FileObject,ChangeDataControlArea,0);
				}
				if(Fcb->SectionObjectPointers.ImageSectionObject != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,FileObject,ChangeImageControlArea,0);
				}
				if(Fcb->SectionObjectPointers.SharedCacheMap != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,FileObject,ChangeSharedCacheMap,0);
				}
				SetFlag(Fcb->FcbState,SCB_STATE_CHANGE_BACKING);
			}
		}
		//��������Լ��Ļ��棬��ʱ��ᷢ��pagingio��������ʱ��֤�����ǻ����io����ʱ���²㻺��ͻ�ˢ���ˣ���������Ҳ����д�������ϣ�
		//���ʱ���²㻺����ȥ�������ݣ��ͻ����˼��ܵ����ݡ�
		if (/*(Fcb->OpenHandleCount == 1) &&*/
			(Fcb->SectionObjectPointers.DataSectionObject != NULL))
		{

			CcFlushCache( &Fcb->SectionObjectPointers, NULL, 0, NULL );  //�ڲ�����MmFlushSection ��������ڲ������빲���pagingio��Դ

			ExAcquireResourceExclusiveLite( Fcb->Header.PagingIoResource, TRUE);
			ExReleaseResourceLite( Fcb->Header.PagingIoResource );

			CcPurgeCacheSection( &Fcb->SectionObjectPointers,
				NULL,
				0,
				BooleanFlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE));	
		}

		TruncateSize = Fcb->Header.FileSize;

		if(FlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE))
		{


			KeInitializeEvent( &UninitializeCompleteEvent.Event,
				SynchronizationEvent,
				FALSE);

			CcUninitializeCacheMap( FileObject,
				NULL,
				&UninitializeCompleteEvent );

			WaitCompleteEvent = TRUE;

			if(Fcb->CacheObject == FileObject)
			{
				Fcb->CacheType = CACHE_DISABLE;
			}

		}
		else
		{

			CcUninitializeCacheMap( FileObject, 
				&TruncateSize,//&TruncateSize,
				NULL  );
		}

		IoRemoveShareAccess(FileObject, &Fcb->ShareAccess);

		if ( !FlagOn(FileObject->Flags, FO_CLEANUP_COMPLETE) )
		{
			ExAcquireResourceExclusiveLite(Ccb->StreamFileInfo.FO_Resource,TRUE);
			FOResourceAcquired = TRUE;

			NonCachedHandle = BooleanFlagOn(FileObject->Flags, FO_NO_INTERMEDIATE_BUFFERING);
			X70FsdDecrementCleanupCounts(Fcb,NonCachedHandle); //�ݼ�����

			/*if(Fcb->OpenHandleCount == 0 && 
			SetFlag(Fcb->FcbState,SCB_STATE_DISABLE_LOCAL_BUFFERING))
			{
			ClearFlag(Fcb->FcbState,SCB_STATE_DISABLE_LOCAL_BUFFERING)
			}*/

			////��ˢ��
			//if(FlagOn(Ccb->CcbState,CCB_FLAG_FILE_CHANGED))
			//{

			//		SetFlag(Fcb->FcbState,SCB_STATE_FILE_CHANGED); //����ļ��޸Ĺ�����fcb״̬

			//		CleanupSetFile(FltObjects,Fcb,Ccb);		
			//		

			//		ExtendingValidDataSetFile(FltObjects,Fcb,Ccb);

			//}

			if(FlagOn(Ccb->CcbState,CCB_FLAG_FILE_CHANGED))
			{

				LARGE_INTEGER FileBeginOffset;

				if(!Fcb->IsEnFile && BooleanFlagOn(Fcb->FileType,FILE_ACCESS_WRITE_CHANGE_TO_ENCRYPTION)) //�ļ��޸Ĺ���Ҫת��ɼ����ļ�
				{	
					//���ļ����м��� 
					DbgPrint("�ԷǼ����ļ�����\n");
					TransformFileToEncrypted(Data,FltObjects,Fcb,Ccb);
				}

				if(Fcb->IsEnFile && FlagOn(Fcb->FileType,FILE_ACCESS_WRITE_PE_DISENCRYPTION))
				{
					FileBeginOffset.QuadPart = Fcb->FileHeaderLength;

					if(NT_SUCCESS(IsDisEncryptFileType(FltObjects,Ccb->StreamFileInfo.StreamObject,&Fcb->Header.FileSize,&FileBeginOffset,TRUE,&IsDisEncryptType)) && 
						IsDisEncryptType)
					{
						TransformFileToDisEncrypt(Data,FltObjects,Fcb,Ccb);

					}
				}
			}

		}
		if ( !FlagOn(FileObject->Flags, FO_CLEANUP_COMPLETE ))
		{
			if(!BooleanFlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE))
			{
				//�����ǿ��ˢ�£�û�йرյ��ļ�ӳ�����ϵͳ���رյ�ʱ��mod writeˢ�µ������ϣ���ʱ���ļ�ͬ���������⡣
				{
					MmForceSectionClosed(&Fcb->SectionObjectPointers,TRUE);
				}

				Status = FltClose(Ccb->StreamFileInfo.StreamHandle);

				if(!NT_SUCCESS(Status))
				{
					DbgPrint("�ļ��ر�ʧ��\n");
				}
			
			}
			else
			{
				if(AcquirePagingIoResource)
				{
					ExReleaseResourceLite( Fcb->Header.PagingIoResource );
					AcquirePagingIoResource = FALSE;
				}
				if(FcbAcquireResource)
				{
					ExReleaseResourceLite( Fcb->Header.Resource );	// �ͷ�FCB����Դ ///
					FcbAcquireResource = FALSE;
				}

				if(FOResourceAcquired)
				{
					ExReleaseResourceLite(Ccb->StreamFileInfo.FO_Resource);
					FOResourceAcquired = FALSE;
				}

				if(WaitCompleteEvent)
				{
					WaitStatus = KeWaitForSingleObject( &UninitializeCompleteEvent.Event,
						Executive,
						KernelMode,
						FALSE,
						NULL);

				}
				//if(FlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE)) //�����ǿ��ˢ�£�û�йرյ��ļ�ӳ�����ϵͳ���رյ�ʱ��mod writeˢ�µ������ϣ���ʱ���ļ�ͬ���������⡣
				{
					MmForceSectionClosed(&Fcb->SectionObjectPointers,TRUE);
				}
				
			}

			SetFlag( FileObject->Flags, FO_CLEANUP_COMPLETE );

			if(	FlagOn( Fcb->FcbState, SCB_STATE_DELETE_ON_CLOSE ) && 
				Fcb->OpenHandleCount == 0)
			{	

				ExAcquireResourceExclusiveLite(Fcb->EncryptResource,TRUE);
				SetFlag(Fcb->FcbState,SCB_STATE_SHADOW_CLOSE);
				ExReleaseResourceLite( Fcb->EncryptResource );

				(VOID)ExAcquireResourceExclusiveLite( Fcb->Header.PagingIoResource,TRUE );
				AcquirePagingIoResource = TRUE;
				if(!BooleanFlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE))
				{
					Status = FltClose(Fcb->CcFileHandle);

					if(!NT_SUCCESS(Status))
					{
						DbgPrint("�ļ��ر�ʧ��\n");
					}

					if(Fcb->CcFileObject != NULL)
					{
						ObDereferenceObject(Fcb->CcFileObject);
						Fcb->CcFileObject = NULL;
					}
				}
				else
				{
					Status = FltClose(Ccb->StreamFileInfo.StreamHandle);

					if(!NT_SUCCESS(Status))
					{
						DbgPrint("�ļ��ر�ʧ��\n");
					}
				}
				if(Ccb->StreamFileInfo.StreamObject != NULL)
				{
					ObDereferenceObject(Ccb->StreamFileInfo.StreamObject); //
					Ccb->StreamFileInfo.StreamObject = NULL;
				}
				ExReleaseResourceLite( Fcb->Header.PagingIoResource );
				AcquirePagingIoResource = FALSE;

			}
		}
#ifdef CV
		VirtualizerEnd();
#endif
try_exit:NOTHING;


	}
	finally{

		if(AcquirePagingIoResource)
		{
			ExReleaseResourceLite( Fcb->Header.PagingIoResource );
		}
		if(FcbAcquireResource)
		{
			ExReleaseResourceLite( Fcb->Header.Resource );	// �ͷ�FCB����Դ ///
		}
		if(RetNewCallbackData != NULL)
		{
			FltFreeCallbackData(RetNewCallbackData);
		}

		if(FOResourceAcquired)
		{
			ExReleaseResourceLite(Ccb->StreamFileInfo.FO_Resource);
		}

		if(AcquireResource)
		{
			ExReleaseResourceLite(volCtx->VolResource);
		}

		if(volCtx != NULL)
		{
			FltReleaseContext(volCtx);
		}

		Data->IoStatus.Status = STATUS_SUCCESS;

		Data->IoStatus.Information = 0;

		if (!AbnormalTermination())
		{
			X70FsdCompleteRequest( &IrpContext, &Data, STATUS_SUCCESS,FALSE );
		}
	}

	return FltStatus;

}

FLT_PREOP_CALLBACK_STATUS
X70FsdCommonClose(
					__inout PFLT_CALLBACK_DATA Data,
					__in    PCFLT_RELATED_OBJECTS FltObjects,
					__in	  PIRP_CONTEXT IrpContext)
{
	FLT_PREOP_CALLBACK_STATUS FltStatus = FLT_PREOP_COMPLETE;
	PFLT_CALLBACK_DATA RetNewCallbackData = NULL;
	PFILE_OBJECT FileObject;
	PFCB Fcb = NULL;
	PCCB Ccb = NULL;
	NTSTATUS Status = STATUS_SUCCESS;

	BOOLEAN AcquireResource = FALSE;
	PVOLUME_CONTEXT volCtx = NULL;

	FileObject = FltObjects->FileObject;

	Fcb = FileObject->FsContext;
	Ccb = FileObject->FsContext2;

	try{
		//���������ر��ļ�����

		//if(FlagOn(Fcb->FcbState,FCB_STATE_NOTIFY_RESIZE_STREAM))
		//{
		//	try_return(Status);
		//}

		Status = FltGetVolumeContext( FltObjects->Filter,
			FltObjects->Volume,
			&volCtx );
		if(!NT_SUCCESS(Status))
		{
			Data->IoStatus.Status = Status;
			Data->IoStatus.Information = 0;
			try_return(FltStatus = FLT_PREOP_COMPLETE);
		}

		//����ȡ����Դ
		ExAcquireResourceExclusiveLite(volCtx->VolResource, TRUE);
		AcquireResource = TRUE;

		//DbgPrint("�յ�Close %ws fcb����%d \n",FileObject->FileName.Buffer,Fcb->ReferenceCount);
		//�ر�ԭʼ�ļ�����
		if( FlagOn( FileObject->Flags, FO_CLEANUP_COMPLETE ))
		{
			if(FlagOn(Fcb->FcbState,SCB_STATE_CHANGE_BACKING))
			{
				if(Fcb->SectionObjectPointers.DataSectionObject != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,Fcb->CacheObject,ChangeDataControlArea,0);
				}
				if(Fcb->SectionObjectPointers.ImageSectionObject != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,Fcb->CacheObject,ChangeImageControlArea,0);
				}
				if(Fcb->SectionObjectPointers.SharedCacheMap != NULL)
				{
					gDynamicFunctions.pFsRtlChangeBackingFileObject(NULL,Fcb->CacheObject,ChangeSharedCacheMap,0);
				}
			}
			if(Ccb->StreamFileInfo.StreamObject != NULL)
			{
				ExAcquireResourceExclusiveLite(Ccb->StreamFileInfo.FO_Resource,TRUE);
				
				if(BooleanFlagOn(Ccb->CcbState,CCB_FLAG_NETWORK_FILE))
				{
					Status = FltClose(Ccb->StreamFileInfo.StreamHandle);

					if(!NT_SUCCESS(Status))
					{
						DbgPrint("�ļ��ر�ʧ��\n");
					}
				}

				if(Ccb->StreamFileInfo.StreamObject != NULL)
				{
					ObDereferenceObject(Ccb->StreamFileInfo.StreamObject); //

					Ccb->StreamFileInfo.StreamObject = NULL;
				}

				ExReleaseResourceLite(Ccb->StreamFileInfo.FO_Resource); //���岻��

			}
			if(Ccb->StreamFileInfo.FO_Resource != NULL)
			{
				ExDeleteResourceLite(Ccb->StreamFileInfo.FO_Resource);
				ExFreeToNPagedLookasideList(&G_EResourceLookasideList,Ccb->StreamFileInfo.FO_Resource);
			}

			ExFreeToNPagedLookasideList(&G_CcbLookasideList,Ccb);

			FileObject->FsContext2 = NULL;
		}
		if(InterlockedDecrement( &Fcb->ReferenceCount) == 0)
		{
			//DbgPrint("������ص����� %ws\n",FileObject->FileName.Buffer);
			X70FsdFreeFcb(Fcb,IrpContext);

			FileObject->FsContext = NULL;
		}

try_exit:NOTHING;
	}
	finally{

		if(AcquireResource)
		{
			ExReleaseResourceLite(volCtx->VolResource);
		}

		if(volCtx != NULL)
		{
			FltReleaseContext(volCtx);
		}

		if(RetNewCallbackData != NULL)
		{
			FltFreeCallbackData(RetNewCallbackData);
		}
		Data->IoStatus.Status = STATUS_SUCCESS;

		Data->IoStatus.Information = 0;

		X70FsdCompleteRequest( &IrpContext, &Data, STATUS_SUCCESS,FALSE );

	}
	return FltStatus;

}