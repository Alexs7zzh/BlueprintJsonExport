#pragma once

#include "Commandlets/Commandlet.h"
#include "BlueprintJsonExportCommandlet.generated.h"

UCLASS()
class BLUEPRINTJSONEXPORT_API UBlueprintJsonExportCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UBlueprintJsonExportCommandlet();

	virtual int32 Main(const FString& Params) override;
};
