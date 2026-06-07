#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class FBlueprintEditor;
class UK2Node_CustomEvent;

class FReplicEventDetailsCustomization : public IDetailCustomization
{
public:
	explicit FReplicEventDetailsCustomization(TWeakPtr<FBlueprintEditor> InBlueprintEditor = nullptr, bool bInFunctionCustomization = false);
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	UK2Node_CustomEvent* ResolveSelectedEventNode(IDetailLayoutBuilder& DetailLayout) const;
	bool GetBoolMetadata(const UK2Node_CustomEvent* EventNode, const FName& Key, bool bDefaultValue) const;
	FString GetStringMetadata(const UK2Node_CustomEvent* EventNode, const FName& Key, const FString& DefaultValue) const;
	FReply CreateReplicCallerNode(UK2Node_CustomEvent* EventNode) const;
	FReply CreateCustomValidationFunction(UK2Node_CustomEvent* EventNode) const;
	void SetBoolMetadata(UK2Node_CustomEvent* EventNode, const FName& Key, bool bEnabled) const;
	void SetStringMetadata(UK2Node_CustomEvent* EventNode, const FName& Key, const FString& Value) const;
	void RefreshBlueprint(UK2Node_CustomEvent* EventNode) const;

	TWeakPtr<FBlueprintEditor> BlueprintEditor;
	TArray<TSharedPtr<FString>> ModeOptions;
	TArray<TSharedPtr<FString>> PermissionOptions;
	bool bFunctionCustomization = false;
};
