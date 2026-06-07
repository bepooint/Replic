#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"
#include "ReplicTypes.h"

class UReplicTransportComponent;
class USceneComponent;
class UBlueprint;

class FReplicSceneComponentDetailsCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	FReplicSceneComponentDetailsCustomization();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailLayout) override;

private:
	USceneComponent* ResolveSceneComponent(IDetailLayoutBuilder& DetailLayout) const;
	UBlueprint* ResolveBlueprint(const UObject* Object) const;
	AActor* ResolveOwnerActor(const USceneComponent* SceneComponent) const;
	UReplicTransportComponent* ResolveTransport(const USceneComponent* SceneComponent) const;
	FReplicComponentTransformSettings* FindSettings() const;
	FReplicComponentTransformSettings& FindOrAddSettings() const;
	void MarkTransportChanged() const;

	bool HasValidReplicOwner() const;
	bool IsChannelEnabled(bool FReplicComponentTransformSettings::*Member) const;
	void SetChannelEnabled(bool FReplicComponentTransformSettings::*Member, bool bEnabled) const;
	ECheckBoxState GetChannelCheckState(bool FReplicComponentTransformSettings::*Member) const;
	void OnChannelCheckStateChanged(ECheckBoxState NewState, bool FReplicComponentTransformSettings::*Member) const;
	TOptional<float> GetFloatSettingValue(float FReplicComponentTransformSettings::*Member) const;
	void SetFloatSettingValue(float NewValue, float FReplicComponentTransformSettings::*Member) const;

	EReplicTransformSpace GetTransformSpace() const;
	void SetTransformSpace(EReplicTransformSpace NewSpace) const;
	TSharedRef<SWidget> MakeTransformSpaceWidget(TSharedPtr<EReplicTransformSpace> Space) const;
	FText GetTransformSpaceText() const;

	FText GetPrerequisiteText() const;

	TWeakObjectPtr<USceneComponent> SelectedComponent;
	TWeakObjectPtr<UReplicTransportComponent> SelectedTransport;
	TArray<TSharedPtr<EReplicTransformSpace>> TransformSpaceOptions;
};
