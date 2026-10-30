#pragma once

#include "CoreMinimal.h"
#include "ContentBrowserDelegates.h"
#include "Modules/ModuleManager.h"

class FZipArchiveReimportHandler;
class UZipArchiveFactory;

class FZipImporterModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	friend class UZipArchiveFactory;

	FZipArchiveReimportHandler* ReimportHandler = nullptr;

	bool OnAssetViewDragOver(
		const FAssetViewDragAndDropExtender::FPayload& Payload);

	bool OnAssetViewDrop(
		const FAssetViewDragAndDropExtender::FPayload& Payload);

	bool CanHandleZipDrop(
		const FAssetViewDragAndDropExtender::FPayload& Payload) const;

	UObject* ImportArchiveToPackagePath(
		const FString& ArchivePath,
		const FString& DestinationPackagePath);
};