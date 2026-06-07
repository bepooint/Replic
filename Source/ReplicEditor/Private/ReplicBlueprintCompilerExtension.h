#pragma once

#include "BlueprintCompilerExtension.h"

#include "ReplicBlueprintCompilerExtension.generated.h"

UCLASS()
class UReplicBlueprintCompilerExtension : public UBlueprintCompilerExtension
{
	GENERATED_BODY()

protected:
	virtual void ProcessBlueprintCompiled(const FKismetCompilerContext& CompilationContext, const FBlueprintCompiledData& Data) override;
};
