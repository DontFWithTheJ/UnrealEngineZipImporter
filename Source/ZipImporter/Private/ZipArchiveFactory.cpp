#include "ZipArchiveFactory.h"

#include "ZipImporter.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

UZipArchiveFactory::UZipArchiveFactory(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = UObject::StaticClass();
	bCreateNew = false;
	bEditorImport = true;
	bText = false;
	Formats.Add(TEXT("zip;ZIP Archive"));
}

bool UZipArchiveFactory::FactoryCanImport(const FString& Filename)
{
	return FPaths::GetExtension(Filename).Equals(
		TEXT("zip"),
		ESearchCase::IgnoreCase);
}

UObject* UZipArchiveFactory::FactoryCreateFile(
	UClass*,
	UObject* InParent,
	FName,
	EObjectFlags,
	const FString& Filename,
	const TCHAR*,
	FFeedbackContext*,
	bool& bOutOperationCanceled)
{
	bOutOperationCanceled = false;

	if (!IsValid(InParent))
	{
		return nullptr;
	}

	FZipImporterModule* Module =
		FModuleManager::GetModulePtr<FZipImporterModule>(TEXT("ZipImporter"));

	if (!Module)
	{
		return nullptr;
	}

	const FString ParentPackageName = InParent->GetOutermost()->GetName();

	const FString DestinationPackagePath =
		FPackageName::GetLongPackagePath(ParentPackageName);

	if (DestinationPackagePath != TEXT("/Game") &&
		!DestinationPackagePath.StartsWith(TEXT("/Game/")))
	{
		return nullptr;
	}

	return Module->ImportArchiveToPackagePath(
		Filename,
		DestinationPackagePath);
}