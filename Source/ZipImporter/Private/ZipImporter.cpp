
#include "ZipImporter.h"

#include "AssetImportTask.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "DesktopPlatformModule.h"
#include "EditorReimportHandler.h"
#include "FileHelpers.h"
#include "FileUtilities/ZipArchiveReader.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "IDesktopPlatform.h"
#include "Input/DragAndDrop.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "ToolMenus.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"


namespace ZipImporter
{
	static const TCHAR* ArchivePathKey = TEXT("ZipImporter.ArchivePath");
	static const TCHAR* ArchiveFileKey = TEXT("ZipImporter.ArchiveFile");
	static const TCHAR* ArchiveChainKey = TEXT("ZipImporter.ArchiveChain");

	static constexpr int32 MaxArchiveDepth = 8;
	static constexpr int32 MaxArchiveFiles = 10000;
	static constexpr int64 MaxSingleFileSize = 512ll * 1024ll * 1024ll;

	struct FImportSource
	{
		FString DiskPath;
		FString DestinationPath;
		TArray<FString> ArchiveChain;
	};

	struct FImportContext
	{
		FString ArchivePath;
		FString StagingDirectory;
		TArray<FImportSource> Sources;
		TArray<FString> FailedPaths;
		TSet<FString> VisitedArchives;
		int32 ProcessedFiles = 0;
		int32 ImportedObjects = 0;
	};

	bool IsSafeRelativePath(const FString& InputPath, FString& OutRelativePath)
	{
		OutRelativePath = InputPath;
		OutRelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));

		if (OutRelativePath.IsEmpty() ||
			OutRelativePath.StartsWith(TEXT("/")) ||
			OutRelativePath.Contains(TEXT(":")) ||
			OutRelativePath.Contains(TEXT("|")) ||
			OutRelativePath.Contains(TEXT("\n")) ||
			OutRelativePath.Contains(TEXT("\r")))
		{
			return false;
		}

		TArray<FString> Segments;
		OutRelativePath.ParseIntoArray(Segments, TEXT("/"), false);

		if (Segments.IsEmpty())
		{
			return false;
		}

		for (const FString& Segment : Segments)
		{
			if (Segment.IsEmpty() || Segment == TEXT("..") || Segment == TEXT("."))
			{
				return false;
			}
		}

		return true;
	}

	bool GetContentRelativePath(const FString& Directory, FString& OutRelativePath)
	{
		FString ContentDirectory = FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir());
		FString SelectedDirectory = FPaths::ConvertRelativePathToFull(Directory);

		FPaths::NormalizeDirectoryName(ContentDirectory);
		FPaths::NormalizeDirectoryName(SelectedDirectory);

		if (SelectedDirectory.Equals(ContentDirectory, ESearchCase::IgnoreCase))
		{
			OutRelativePath.Reset();
			return true;
		}

		ContentDirectory /= TEXT("");

		if (!SelectedDirectory.StartsWith(ContentDirectory, ESearchCase::IgnoreCase))
		{
			return false;
		}

		OutRelativePath = SelectedDirectory.RightChop(ContentDirectory.Len());
		OutRelativePath.ReplaceInline(TEXT("\\"), TEXT("/"));

		return true;
	}

	FString MakePackagePath(const FString& RelativePath)
	{
		FString Result = TEXT("/Game");

		if (!RelativePath.IsEmpty())
		{
			Result /= RelativePath;
			Result.ReplaceInline(TEXT("\\"), TEXT("/"));
		}

		return Result;
	}

	bool IsZipFile(const FString& FilePath)
	{
		return FPaths::GetExtension(FilePath).Equals(TEXT("zip"), ESearchCase::IgnoreCase);
	}

	void ShowMessage(const FString& Message)
	{
		FMessageDialog::Open(EAppMsgType::Ok, FText::FromString(Message));
	}

	FString SerializeChain(const TArray<FString>& Chain)
	{
		return FString::Join(Chain, TEXT("\n"));
	}

	bool DeserializeChain(const FString& SerializedChain, TArray<FString>& OutChain)
	{
		OutChain.Reset();

		if (SerializedChain.IsEmpty())
		{
			return false;
		}

		SerializedChain.ParseIntoArray(OutChain, TEXT("\n"), false);

		if (OutChain.IsEmpty())
		{
			return false;
		}

		for (FString& Entry : OutChain)
		{
			FString SafePath;

			if (!IsSafeRelativePath(Entry, SafePath))
			{
				OutChain.Reset();
				return false;
			}

			Entry = SafePath;
		}

		return true;
	}

	void SetReimportMetadata(
		UObject* Object,
		const FString& ArchivePath,
		const TArray<FString>& ArchiveChain)
	{
		if (!IsValid(Object) || ArchiveChain.IsEmpty())
		{
			return;
		}

		UPackage* Package = Object->GetOutermost();

		if (!IsValid(Package))
		{
			return;
		}

		FMetaData& Metadata = Package->GetMetaData();
		Metadata.SetValue(Object, ArchivePathKey, *ArchivePath);
		Metadata.SetValue(Object, ArchiveFileKey, *ArchiveChain.Last());
		Metadata.SetValue(Object, ArchiveChainKey, *SerializeChain(ArchiveChain));

		Object->MarkPackageDirty();
	}

	bool GetReimportMetadata(
		UObject* Object,
		FString& OutArchivePath,
		TArray<FString>& OutArchiveChain)
	{
		if (!IsValid(Object))
		{
			return false;
		}

		UPackage* Package = Object->GetOutermost();

		if (!IsValid(Package))
		{
			return false;
		}

		FMetaData& Metadata = Package->GetMetaData();

		const FString& ArchivePath = Metadata.GetValue(Object, ArchivePathKey);
		const FString& SerializedChain = Metadata.GetValue(Object, ArchiveChainKey);
		const FString& LegacyArchiveFile = Metadata.GetValue(Object, ArchiveFileKey);

		if (ArchivePath.IsEmpty())
		{
			return false;
		}

		OutArchivePath = ArchivePath;

		if (DeserializeChain(SerializedChain, OutArchiveChain))
		{
			return true;
		}

		if (!LegacyArchiveFile.IsEmpty())
		{
			FString SafePath;

			if (IsSafeRelativePath(LegacyArchiveFile, SafePath))
			{
				OutArchiveChain = { SafePath };
				return true;
			}
		}

		return false;
	}

	bool ReadArchiveEntry(
		const FString& ArchivePath,
		const FString& EntryPath,
		TArray<uint8>& OutData,
		FString& OutError)
	{
		FString SafePath;

		if (!IsSafeRelativePath(EntryPath, SafePath))
		{
			OutError = FString::Printf(
				TEXT("Unsafe path inside archive:\n%s"),
				*EntryPath);
			return false;
		}

		if (!FPaths::FileExists(ArchivePath))
		{
			OutError = FString::Printf(
				TEXT("Archive file does not exist:\n%s"),
				*ArchivePath);
			return false;
		}

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		IFileHandle* FileHandle = PlatformFile.OpenRead(*ArchivePath);

		if (!FileHandle)
		{
			OutError = FString::Printf(
				TEXT("Could not open archive:\n%s"),
				*ArchivePath);
			return false;
		}

		FZipArchiveReader ArchiveReader(FileHandle, nullptr);

		if (!ArchiveReader.IsValid())
		{
			OutError = FString::Printf(
				TEXT("Could not read ZIP archive:\n%s"),
				*ArchivePath);
			return false;
		}

		if (!ArchiveReader.TryReadFile(SafePath, OutData, nullptr))
		{
			OutError = FString::Printf(
				TEXT("Entry was not found or could not be extracted:\n%s\n\nArchive:\n%s"),
				*SafePath,
				*ArchivePath);
			return false;
		}

		if (static_cast<int64>(OutData.Num()) > MaxSingleFileSize)
		{
			OutData.Reset();
			OutError = FString::Printf(
				TEXT("The extracted file exceeds the size limit of %lld MB:\n%s"),
				MaxSingleFileSize / (1024ll * 1024ll),
				*SafePath);
			return false;
		}

		return true;
	}

	bool WriteFile(const FString& OutputPath, const TArray<uint8>& Data, FString& OutError)
	{
		const FString OutputDirectory = FPaths::GetPath(OutputPath);

		if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
		{
			OutError = FString::Printf(
				TEXT("Could not create directory:\n%s"),
				*OutputDirectory);
			return false;
		}

		if (!FFileHelper::SaveArrayToFile(Data, *OutputPath))
		{
			OutError = FString::Printf(
				TEXT("Could not write file:\n%s"),
				*OutputPath);
			return false;
		}

		return true;
	}

	bool ExtractArchiveChain(
		const FString& ArchivePath,
		const TArray<FString>& ArchiveChain,
		FString& OutExtractedPath,
		FString& OutError)
	{
		if (!FPaths::FileExists(ArchivePath))
		{
			OutError = FString::Printf(
				TEXT("The source ZIP archive no longer exists:\n%s"),
				*ArchivePath);
			return false;
		}

		if (ArchiveChain.IsEmpty() || ArchiveChain.Num() > MaxArchiveDepth + 1)
		{
			OutError = TEXT("The stored archive path chain is invalid or too deep.");
			return false;
		}

		FString CurrentArchivePath = FPaths::ConvertRelativePathToFull(ArchivePath);
		TArray<uint8> FileData;

		for (int32 Index = 0; Index < ArchiveChain.Num(); ++Index)
		{
			const FString& EntryPath = ArchiveChain[Index];

			if (!ReadArchiveEntry(CurrentArchivePath, EntryPath, FileData, OutError))
			{
				OutError = FString::Printf(
					TEXT("Failed to resolve source chain at step %d of %d.\n\n%s"),
					Index + 1,
					ArchiveChain.Num(),
					*OutError);
				return false;
			}

			if (Index < ArchiveChain.Num() - 1)
			{
				if (!IsZipFile(EntryPath))
				{
					OutError = FString::Printf(
						TEXT("An intermediate source entry is not a ZIP archive:\n%s"),
						*EntryPath);
					return false;
				}

				const FString IntermediateDirectory = FPaths::Combine(
					FPaths::ProjectSavedDir(),
					TEXT("ZipImporter"),
					TEXT("Reimport"),
					TEXT("Nested"),
					FString::Printf(
						TEXT("%08x"),
						GetTypeHash(ArchivePath + TEXT("|") + [&ArchiveChain, Index]()
						{
							TArray<FString> ChainPart;
							ChainPart.Append(ArchiveChain.GetData(), Index + 1);
							return SerializeChain(ChainPart);
						}())));

				CurrentArchivePath = FPaths::Combine(
					IntermediateDirectory,
					FPaths::GetCleanFilename(EntryPath));

				if (!WriteFile(CurrentArchivePath, FileData, OutError))
				{
					return false;
				}
			}
		}

		const FString TempDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("ZipImporter"),
			TEXT("Reimport"),
			FString::Printf(
				TEXT("%08x"),
				GetTypeHash(ArchivePath + TEXT("|") + SerializeChain(ArchiveChain))));

		OutExtractedPath = FPaths::Combine(
			TempDirectory,
			FPaths::GetCleanFilename(ArchiveChain.Last()));

		return WriteFile(OutExtractedPath, FileData, OutError);
	}

	bool SaveDirtyPackages(const TArray<UPackage*>& Packages)
	{
		TArray<UPackage*> UniquePackages;
		TSet<UPackage*> SeenPackages;

		for (UPackage* Package : Packages)
		{
			if (IsValid(Package) && !SeenPackages.Contains(Package))
			{
				SeenPackages.Add(Package);
				UniquePackages.Add(Package);
			}
		}

		return UniquePackages.IsEmpty() ||
			UEditorLoadingAndSavingUtils::SavePackages(UniquePackages, true);
	}

	bool ProcessArchiveRecursive(
		const FString& ArchivePath,
		const FString& DestinationPackagePath,
		const TArray<FString>& ParentChain,
		const int32 Depth,
		FImportContext& Context)
	{
		if (Depth > MaxArchiveDepth)
		{
			Context.FailedPaths.Add(FString::Printf(
				TEXT("Maximum ZIP nesting depth exceeded:\n%s"),
				*ArchivePath));
			return false;
		}

		const FString CanonicalArchivePath = FPaths::ConvertRelativePathToFull(ArchivePath);
		const FString ArchiveKey = CanonicalArchivePath.ToLower();

		if (Context.VisitedArchives.Contains(ArchiveKey))
		{
			Context.FailedPaths.Add(FString::Printf(
				TEXT("Archive recursion detected:\n%s"),
				*ArchivePath));
			return false;
		}

		Context.VisitedArchives.Add(ArchiveKey);

		IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
		IFileHandle* FileHandle = PlatformFile.OpenRead(*CanonicalArchivePath);

		if (!FileHandle)
		{
			Context.FailedPaths.Add(FString::Printf(
				TEXT("Could not open archive:\n%s"),
				*CanonicalArchivePath));
			Context.VisitedArchives.Remove(ArchiveKey);
			return false;
		}

		FZipArchiveReader ArchiveReader(FileHandle, nullptr);

		if (!ArchiveReader.IsValid())
		{
			Context.FailedPaths.Add(FString::Printf(
				TEXT("Could not read archive:\n%s"),
				*CanonicalArchivePath));
			Context.VisitedArchives.Remove(ArchiveKey);
			return false;
		}

		const TArray<FString> ArchiveFiles = ArchiveReader.GetFileNames();

		for (const FString& ArchiveFile : ArchiveFiles)
		{
			if (ArchiveFile.EndsWith(TEXT("/")) || ArchiveFile.EndsWith(TEXT("\\")))
			{
				continue;
			}

			++Context.ProcessedFiles;

			if (Context.ProcessedFiles > MaxArchiveFiles)
			{
				Context.FailedPaths.Add(FString::Printf(
					TEXT("Archive contains more than %d files."),
					MaxArchiveFiles));
				Context.VisitedArchives.Remove(ArchiveKey);
				return false;
			}

			FString RelativeFilePath;

			if (!IsSafeRelativePath(ArchiveFile, RelativeFilePath))
			{
				Context.FailedPaths.Add(FString::Printf(
					TEXT("Unsafe archive path:\n%s"),
					*ArchiveFile));
				continue;
			}

			TArray<uint8> FileData;

			if (!ArchiveReader.TryReadFile(RelativeFilePath, FileData, nullptr))
			{
				Context.FailedPaths.Add(FString::Printf(
					TEXT("Could not extract archive entry:\n%s"),
					*RelativeFilePath));
				continue;
			}

			if (static_cast<int64>(FileData.Num()) > MaxSingleFileSize)
			{
				Context.FailedPaths.Add(FString::Printf(
					TEXT("File exceeds the size limit:\n%s"),
					*RelativeFilePath));
				continue;
			}

			TArray<FString> CurrentChain = ParentChain;
			CurrentChain.Add(RelativeFilePath);

			if (IsZipFile(RelativeFilePath))
			{
				if (Depth >= MaxArchiveDepth)
				{
					Context.FailedPaths.Add(FString::Printf(
						TEXT("Maximum ZIP nesting depth exceeded:\n%s"),
						*SerializeChain(CurrentChain)));
					continue;
				}

				const FString NestedArchivePath = FPaths::Combine(
					Context.StagingDirectory,
					TEXT("Nested"),
					FString::Printf(
						TEXT("%08x"),
						GetTypeHash(SerializeChain(CurrentChain))),
					FPaths::GetCleanFilename(RelativeFilePath));

				FString WriteError;

				if (!WriteFile(NestedArchivePath, FileData, WriteError))
				{
					Context.FailedPaths.Add(WriteError);
					continue;
				}

				FString NestedDestination = DestinationPackagePath;
				const FString RelativeDirectory = FPaths::GetPath(RelativeFilePath);

				if (!RelativeDirectory.IsEmpty())
				{
					NestedDestination /= RelativeDirectory;
				}

				NestedDestination /= FPaths::GetBaseFilename(RelativeFilePath);
				NestedDestination.ReplaceInline(TEXT("\\"), TEXT("/"));

				ProcessArchiveRecursive(
					NestedArchivePath,
					NestedDestination,
					CurrentChain,
					Depth + 1,
					Context);

				continue;
			}

			const FString SerializedChain = SerializeChain(CurrentChain);

			const FString OutputPath = FPaths::Combine(
				Context.StagingDirectory,
				TEXT("Extracted"),
				FString::Printf(TEXT("%08x"), GetTypeHash(SerializedChain)),
				FPaths::GetCleanFilename(RelativeFilePath));

			FString WriteError;

			if (!WriteFile(OutputPath, FileData, WriteError))
			{
				Context.FailedPaths.Add(WriteError);
				continue;
			}

			FString TaskDestination = DestinationPackagePath;
			const FString RelativeDirectory = FPaths::GetPath(RelativeFilePath);

			if (!RelativeDirectory.IsEmpty())
			{
				TaskDestination /= RelativeDirectory;
				TaskDestination.ReplaceInline(TEXT("\\"), TEXT("/"));
			}

			FImportSource& Source = Context.Sources.AddDefaulted_GetRef();
			Source.DiskPath = OutputPath;
			Source.DestinationPath = TaskDestination;
			Source.ArchiveChain = MoveTemp(CurrentChain);
		}

		Context.VisitedArchives.Remove(ArchiveKey);
		return true;
	}
}

class FZipArchiveReimportHandler final : public FReimportHandler
{
public:
	bool CanReimport(UObject* Object, TArray<FString>& OutFilenames) override
	{
		FString ArchivePath;
		TArray<FString> ArchiveChain;

		if (!ZipImporter::GetReimportMetadata(Object, ArchivePath, ArchiveChain))
		{
			return false;
		}

		OutFilenames.Add(ArchivePath);
		return true;
	}

	void SetReimportPaths(UObject* Object, const TArray<FString>& NewReimportPaths) override
	{
		if (!IsValid(Object) || NewReimportPaths.IsEmpty())
		{
			return;
		}

		FString ArchivePath;
		TArray<FString> ArchiveChain;

		if (!ZipImporter::GetReimportMetadata(Object, ArchivePath, ArchiveChain))
		{
			return;
		}

		const FString NewArchivePath =
			FPaths::ConvertRelativePathToFull(NewReimportPaths[0]);

		ZipImporter::SetReimportMetadata(Object, NewArchivePath, ArchiveChain);
	}

	int32 GetPriority() const override
	{
		return 1000;
	}

	EReimportResult::Type Reimport(UObject* Object) override
	{
		return Reimport(Object, 0);
	}

	EReimportResult::Type Reimport(UObject* Object, int32 SourceFileIndex) override
	{
		FString ArchivePath;
		TArray<FString> ArchiveChain;

		if (!ZipImporter::GetReimportMetadata(Object, ArchivePath, ArchiveChain))
		{
			return EReimportResult::Failed;
		}

		FString ExtractedPath;
		FString Error;

		if (!ZipImporter::ExtractArchiveChain(
			ArchivePath,
			ArchiveChain,
			ExtractedPath,
			Error))
		{
			ZipImporter::ShowMessage(Error);
			return EReimportResult::Failed;
		}

		UPackage* ExistingPackage = Object->GetOutermost();

		if (!IsValid(ExistingPackage))
		{
			return EReimportResult::Failed;
		}

		UAssetImportTask* Task = NewObject<UAssetImportTask>();
		Task->Filename = ExtractedPath;
		Task->DestinationPath = FPackageName::GetLongPackagePath(ExistingPackage->GetName());
		Task->DestinationName = Object->GetName();
		Task->bAutomated = !IsForceShowDialog();
		Task->bReplaceExisting = true;
		Task->bSave = false;

		TArray<UAssetImportTask*> Tasks;
		Tasks.Add(Task);

		FAssetToolsModule& AssetToolsModule =
			FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));

		AssetToolsModule.Get().ImportAssetTasks(Tasks);

		const TArray<UObject*>& ImportedObjects = Task->GetObjects();

		if (ImportedObjects.IsEmpty())
		{
			ZipImporter::ShowMessage(
				FString::Printf(
					TEXT("Reimport failed.\n\nAsset:\n%s\n\nSource chain:\n%s"),
					*Object->GetPathName(),
					*ZipImporter::SerializeChain(ArchiveChain)));

			return EReimportResult::Failed;
		}

		TArray<UPackage*> PackagesToSave;
		bool bFoundImportedObject = false;

		for (UObject* ImportedObject : ImportedObjects)
		{
			if (!IsValid(ImportedObject))
			{
				continue;
			}

			ZipImporter::SetReimportMetadata(
				ImportedObject,
				ArchivePath,
				ArchiveChain);

			PackagesToSave.Add(ImportedObject->GetOutermost());
			bFoundImportedObject = true;
		}

		if (!bFoundImportedObject)
		{
			return EReimportResult::Failed;
		}

		if (!ZipImporter::SaveDirtyPackages(PackagesToSave))
		{
			ZipImporter::ShowMessage(
				TEXT("The source file was imported, but saving the updated asset packages failed."));
			return EReimportResult::Failed;
		}

		UE_LOG(
			LogTemp,
			Log,
			TEXT("ZIP reimport succeeded: %s"),
			*Object->GetPathName());

		return EReimportResult::Succeeded;
	}
};

void FZipImporterModule::StartupModule()
{
	ReimportHandler = new FZipArchiveReimportHandler();

	if (FModuleManager::Get().IsModuleLoaded(TEXT("ContentBrowser")))
	{
		FContentBrowserModule& ContentBrowserModule =
			FModuleManager::GetModuleChecked<FContentBrowserModule>(
				TEXT("ContentBrowser"));

		ContentBrowserModule.GetAssetViewDragAndDropExtenders().Add(
			FAssetViewDragAndDropExtender(
				FAssetViewDragAndDropExtender::FOnDropDelegate::CreateRaw(
					this,
					&FZipImporterModule::OnAssetViewDrop),
				FAssetViewDragAndDropExtender::FOnDragOverDelegate::CreateRaw(
					this,
					&FZipImporterModule::OnAssetViewDragOver)));
	}
}

void FZipImporterModule::ShutdownModule()
{
	delete ReimportHandler;
	ReimportHandler = nullptr;
}

bool FZipImporterModule::CanHandleZipDrop(
	const FAssetViewDragAndDropExtender::FPayload& Payload) const
{
	if (!Payload.DragDropOp.IsValid() ||
		!Payload.DragDropOp->IsOfType<FExternalDragOperation>())
	{
		return false;
	}

	const TSharedPtr<FExternalDragOperation> ExternalOperation =
		StaticCastSharedPtr<FExternalDragOperation>(Payload.DragDropOp);

	if (!ExternalOperation.IsValid() || !ExternalOperation->HasFiles())
	{
		return false;
	}

	if (Payload.PackagePaths.IsEmpty())
	{
		return false;
	}

	const FString DestinationPath = Payload.PackagePaths[0].ToString();

	if (!DestinationPath.StartsWith(TEXT("/Game")))
	{
		return false;
	}

	for (const FString& FilePath : ExternalOperation->GetFiles())
	{
		if (ZipImporter::IsZipFile(FilePath))
		{
			return true;
		}
	}

	return false;
}

bool FZipImporterModule::OnAssetViewDragOver(
	const FAssetViewDragAndDropExtender::FPayload& Payload)
{
	return CanHandleZipDrop(Payload);
}

bool FZipImporterModule::OnAssetViewDrop(
	const FAssetViewDragAndDropExtender::FPayload& Payload)
{
	if (!CanHandleZipDrop(Payload))
	{
		return false;
	}

	const TSharedPtr<FExternalDragOperation> ExternalOperation =
		StaticCastSharedPtr<FExternalDragOperation>(Payload.DragDropOp);

	const FString DestinationPackagePath = Payload.PackagePaths[0].ToString();

	bool bHandled = false;

	for (const FString& FilePath : ExternalOperation->GetFiles())
	{
		if (!ZipImporter::IsZipFile(FilePath))
		{
			continue;
		}

		ImportArchiveToPackagePath(FilePath, DestinationPackagePath);
		bHandled = true;
	}

	return bHandled;
}


UObject* FZipImporterModule::ImportArchiveToPackagePath(
    const FString& ArchivePath,
    const FString& DestinationPackagePath)
{
    if (!ZipImporter::IsZipFile(ArchivePath))
    {
        return nullptr;
    }

    const FString ArchiveAbsolutePath =
        FPaths::ConvertRelativePathToFull(ArchivePath);

    const FString ArchiveName =
        FPaths::GetBaseFilename(ArchiveAbsolutePath);

    const FString StagingDirectory = FPaths::Combine(
        FPaths::ProjectSavedDir(),
        TEXT("ZipImporter"),
        FString::Printf(
            TEXT("%s_%08x"),
            *ArchiveName,
            GetTypeHash(ArchiveAbsolutePath)));

    if (!FPaths::FileExists(ArchiveAbsolutePath))
    {
        ZipImporter::ShowMessage(
            FString::Printf(
                TEXT("Archive does not exist:\n%s"),
                *ArchiveAbsolutePath));

        return nullptr;
    }

    ZipImporter::FImportContext Context;
    Context.ArchivePath = ArchiveAbsolutePath;
    Context.StagingDirectory = StagingDirectory;

    if (!IFileManager::Get().MakeDirectory(*StagingDirectory, true))
    {
        ZipImporter::ShowMessage(
            FString::Printf(
                TEXT("Could not create staging directory:\n%s"),
                *StagingDirectory));

        return nullptr;
    }

    ZipImporter::ProcessArchiveRecursive(
        ArchiveAbsolutePath,
        DestinationPackagePath,
        TArray<FString>(),
        0,
        Context);

    if (Context.Sources.IsEmpty())
    {
        FString Message = TEXT("No files were available for import.");

        if (!Context.FailedPaths.IsEmpty())
        {
            Message += TEXT("\n\n");
            Message += FString::Join(Context.FailedPaths, TEXT("\n\n"));
        }

        ZipImporter::ShowMessage(Message);
        return nullptr;
    }

    TArray<UAssetImportTask*> ImportTasks;
    TMap<UAssetImportTask*, TArray<FString>> TaskArchiveChains;

    ImportTasks.Reserve(Context.Sources.Num());

    for (const ZipImporter::FImportSource& Source : Context.Sources)
    {
        UAssetImportTask* Task = NewObject<UAssetImportTask>();
        Task->Filename = Source.DiskPath;
        Task->DestinationPath = Source.DestinationPath;
        Task->bAutomated = false;
        Task->bReplaceExisting = true;
        Task->bSave = false;

        ImportTasks.Add(Task);
        TaskArchiveChains.Add(Task, Source.ArchiveChain);
    }

    FAssetToolsModule& AssetToolsModule =
        FModuleManager::LoadModuleChecked<FAssetToolsModule>(
            TEXT("AssetTools"));

    AssetToolsModule.Get().ImportAssetTasks(ImportTasks);

    TArray<UPackage*> PackagesToSave;
    int32 FailedImportTasks = 0;
    UObject* PrimaryImportedObject = nullptr;

    for (UAssetImportTask* Task : ImportTasks)
    {
        if (!Task)
        {
            ++FailedImportTasks;
            continue;
        }

        const TArray<FString>* ArchiveChain =
            TaskArchiveChains.Find(Task);

        if (!ArchiveChain)
        {
            ++FailedImportTasks;
            continue;
        }

        const TArray<UObject*>& ImportedObjects = Task->GetObjects();

        if (ImportedObjects.IsEmpty())
        {
            ++FailedImportTasks;
            continue;
        }

        for (UObject* ImportedObject : ImportedObjects)
        {
            if (!IsValid(ImportedObject))
            {
                continue;
            }

            if (!PrimaryImportedObject)
            {
                PrimaryImportedObject = ImportedObject;
            }

            ZipImporter::SetReimportMetadata(
                ImportedObject,
                ArchiveAbsolutePath,
                *ArchiveChain);

            PackagesToSave.Add(ImportedObject->GetOutermost());
            ++Context.ImportedObjects;
        }
    }

    const bool bSaved =
        ZipImporter::SaveDirtyPackages(PackagesToSave);

    UE_LOG(
        LogTemp,
        Log,
        TEXT("ZIP import finished. Archive: %s. Files: %d. Import tasks: %d. Imported objects: %d. Failed tasks: %d. Extraction errors: %d."),
        *ArchiveName,
        Context.Sources.Num(),
        ImportTasks.Num(),
        Context.ImportedObjects,
        FailedImportTasks,
        Context.FailedPaths.Num());

    if (!bSaved ||
        FailedImportTasks > 0 ||
        !Context.FailedPaths.IsEmpty())
    {
        FString ResultMessage = FString::Printf(
            TEXT("ZIP import completed with problems.\n\nArchive: %s\nFiles prepared: %d\nImport tasks: %d\nImported objects: %d\nFailed tasks: %d\nExtraction errors: %d"),
            *ArchiveName,
            Context.Sources.Num(),
            ImportTasks.Num(),
            Context.ImportedObjects,
            FailedImportTasks,
            Context.FailedPaths.Num());

        if (!bSaved)
        {
            ResultMessage +=
                TEXT("\n\nSome imported asset packages could not be saved.");
        }

        const int32 MaxErrorsToShow =
            FMath::Min(Context.FailedPaths.Num(), 20);

        for (int32 Index = 0; Index < MaxErrorsToShow; ++Index)
        {
            ResultMessage += TEXT("\n\n");
            ResultMessage += Context.FailedPaths[Index];
        }

        if (Context.FailedPaths.Num() > MaxErrorsToShow)
        {
            ResultMessage += FString::Printf(
                TEXT("\n\nAnd %d more errors. See the Output Log."),
                Context.FailedPaths.Num() - MaxErrorsToShow);
        }

        ZipImporter::ShowMessage(ResultMessage);
    }

    return PrimaryImportedObject;
}

IMPLEMENT_MODULE(FZipImporterModule, ZipImporter)
