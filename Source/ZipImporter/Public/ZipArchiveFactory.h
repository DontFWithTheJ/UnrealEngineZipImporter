#pragma once

#include "CoreMinimal.h"
#include "Factories/Factory.h"
#include "ZipArchiveFactory.generated.h"

UCLASS()
class ZIPIMPORTER_API UZipArchiveFactory final : public UFactory
{
	GENERATED_BODY()

public:
	UZipArchiveFactory(const FObjectInitializer& ObjectInitializer);

	virtual bool FactoryCanImport(const FString& Filename) override;

	virtual UObject* FactoryCreateFile(
		UClass* InClass,
		UObject* InParent,
		FName InName,
		EObjectFlags Flags,
		const FString& Filename,
		const TCHAR* Parms,
		FFeedbackContext* Warn,
		bool& bOutOperationCanceled) override;
};