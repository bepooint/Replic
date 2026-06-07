#include "ReplicSceneComponentDetailsCustomization.h"

#include "Components/SceneComponent.h"
#include "DetailCategoryBuilder.h"
#include "IDetailGroup.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ReplicTransportComponent.h"
#include "ScopedTransaction.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SNumericEntryBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "ReplicSceneComponentDetailsCustomization"

namespace
{
	FName NormalizeComponentName(FName ComponentName)
	{
		if (ComponentName.IsNone())
		{
			return NAME_None;
		}

		FString NameString = ComponentName.ToString();
		NameString.RemoveFromEnd(UActorComponent::ComponentTemplateNameSuffix);
		return FName(*NameString);
	}

	FText TransformSpaceToText(EReplicTransformSpace TransformSpace)
	{
		return TransformSpace == EReplicTransformSpace::World
			? LOCTEXT("WorldTransform", "World Transform")
			: LOCTEXT("RelativeTransform", "Relative Transform");
	}
}

TSharedRef<IDetailCustomization> FReplicSceneComponentDetailsCustomization::MakeInstance()
{
	return MakeShared<FReplicSceneComponentDetailsCustomization>();
}

FReplicSceneComponentDetailsCustomization::FReplicSceneComponentDetailsCustomization()
{
	TransformSpaceOptions.Add(MakeShared<EReplicTransformSpace>(EReplicTransformSpace::Relative));
	TransformSpaceOptions.Add(MakeShared<EReplicTransformSpace>(EReplicTransformSpace::World));
}

void FReplicSceneComponentDetailsCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	SelectedComponent = ResolveSceneComponent(DetailLayout);
	SelectedTransport = ResolveTransport(SelectedComponent.Get());

	IDetailCategoryBuilder& ReplicCategory = DetailLayout.EditCategory(
		TEXT("Replic"),
		LOCTEXT("ReplicCategory", "Replic"),
		ECategoryPriority::Important);

	ReplicCategory.AddCustomRow(LOCTEXT("PrerequisitesFilter", "Replic prerequisites"))
		.Visibility(TAttribute<EVisibility>::CreateLambda([this]()
		{
			return HasValidReplicOwner() ? EVisibility::Collapsed : EVisibility::Visible;
		}))
		.WholeRowContent()
		[
			SNew(STextBlock)
			.Text(this, &FReplicSceneComponentDetailsCustomization::GetPrerequisiteText)
			.AutoWrapText(true)
		];

	ReplicCategory.AddCustomRow(LOCTEXT("TransformSpaceFilter", "Transform Space"))
		.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return HasValidReplicOwner(); }))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("TransformSpaceLabel", "Transform Space"))
			.ToolTipText(LOCTEXT("TransformSpaceTooltip", "Choose whether Replic tracks this component in relative or world space."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		.MinDesiredWidth(180.0f)
		[
			SNew(SComboBox<TSharedPtr<EReplicTransformSpace>>)
			.OptionsSource(&TransformSpaceOptions)
			.OnGenerateWidget(this, &FReplicSceneComponentDetailsCustomization::MakeTransformSpaceWidget)
			.OnSelectionChanged_Lambda([this](TSharedPtr<EReplicTransformSpace> NewSelection, ESelectInfo::Type)
			{
				if (NewSelection.IsValid())
				{
					SetTransformSpace(*NewSelection);
				}
			})
			[
				SNew(STextBlock)
				.Text(this, &FReplicSceneComponentDetailsCustomization::GetTransformSpaceText)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
		];

	auto AddCheckBoxRow = [this, &ReplicCategory](const FText& Label, const FText& Tooltip, bool FReplicComponentTransformSettings::*Member)
	{
		ReplicCategory.AddCustomRow(Label)
			.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return HasValidReplicOwner(); }))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(Tooltip)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SCheckBox)
				.IsChecked(this, &FReplicSceneComponentDetailsCustomization::GetChannelCheckState, Member)
				.OnCheckStateChanged(this, &FReplicSceneComponentDetailsCustomization::OnChannelCheckStateChanged, Member)
			];
	};

	AddCheckBoxRow(
		LOCTEXT("ReplicateLocationLabel", "Replicate Location"),
		LOCTEXT("ReplicateLocationTooltip", "Automatically replicate this component's location in the selected transform space."),
		&FReplicComponentTransformSettings::bReplicateLocation);
	AddCheckBoxRow(
		LOCTEXT("ReplicateRotationLabel", "Replicate Rotation"),
		LOCTEXT("ReplicateRotationTooltip", "Automatically replicate this component's rotation in the selected transform space."),
		&FReplicComponentTransformSettings::bReplicateRotation);
	AddCheckBoxRow(
		LOCTEXT("ReplicateScaleLabel", "Replicate Scale"),
		LOCTEXT("ReplicateScaleTooltip", "Automatically replicate this component's scale in the selected transform space."),
		&FReplicComponentTransformSettings::bReplicateScale);
	AddCheckBoxRow(
		LOCTEXT("PersistentStateLabel", "Persistent State"),
		LOCTEXT("PersistentStateTooltip", "Store the latest transform so late joiners receive the current component state."),
		&FReplicComponentTransformSettings::bPersistentState);

	IDetailGroup& AdvancedSettingsGroup = ReplicCategory.AddGroup(
		TEXT("ReplicAdvancedSettings"),
		LOCTEXT("AdvancedSettingsGroup", "Advanced Settings"),
		false,
		false);
	AdvancedSettingsGroup.SetToolTip(LOCTEXT("AdvancedSettingsTooltip", "Bandwidth and change-threshold settings for automatic component transform replication."));

	auto AddFloatRow = [this, &AdvancedSettingsGroup](const FText& Label, const FText& Tooltip, float FReplicComponentTransformSettings::*Member, float MinValue)
	{
		AdvancedSettingsGroup.AddWidgetRow()
			.IsEnabled(TAttribute<bool>::CreateLambda([this]() { return HasValidReplicOwner(); }))
			.NameContent()
			[
				SNew(STextBlock)
				.Text(Label)
				.ToolTipText(Tooltip)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MinDesiredWidth(120.0f)
			[
				SNew(SNumericEntryBox<float>)
				.Value(this, &FReplicSceneComponentDetailsCustomization::GetFloatSettingValue, Member)
				.OnValueChanged(this, &FReplicSceneComponentDetailsCustomization::SetFloatSettingValue, Member)
				.OnValueCommitted_Lambda([this, Member](float NewValue, ETextCommit::Type)
				{
					SetFloatSettingValue(NewValue, Member);
				})
				.MinValue(MinValue)
				.MinSliderValue(MinValue)
				.AllowSpin(true)
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	};

	AddFloatRow(
		LOCTEXT("MinUpdateIntervalLabel", "Min Update Interval"),
		LOCTEXT("MinUpdateIntervalTooltip", "Minimum seconds between automatic transform updates for this component. Increase this for many fast physics objects."),
		&FReplicComponentTransformSettings::MinUpdateIntervalSeconds,
		0.0f);
	AddFloatRow(
		LOCTEXT("LocationThresholdLabel", "Location Threshold"),
		LOCTEXT("LocationThresholdTooltip", "Minimum location delta before Replic sends another location update."),
		&FReplicComponentTransformSettings::LocationThreshold,
		0.0f);
	AddFloatRow(
		LOCTEXT("RotationThresholdLabel", "Rotation Threshold"),
		LOCTEXT("RotationThresholdTooltip", "Minimum rotation delta in degrees before Replic sends another rotation update."),
		&FReplicComponentTransformSettings::RotationThresholdDegrees,
		0.0f);
	AddFloatRow(
		LOCTEXT("ScaleThresholdLabel", "Scale Threshold"),
		LOCTEXT("ScaleThresholdTooltip", "Minimum scale delta before Replic sends another scale update."),
		&FReplicComponentTransformSettings::ScaleThreshold,
		0.0f);
}

USceneComponent* FReplicSceneComponentDetailsCustomization::ResolveSceneComponent(IDetailLayoutBuilder& DetailLayout) const
{
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailLayout.GetObjectsBeingCustomized(Objects);
	for (const TWeakObjectPtr<UObject>& Object : Objects)
	{
		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Object.Get()))
		{
			return SceneComponent;
		}
	}

	return nullptr;
}

UBlueprint* FReplicSceneComponentDetailsCustomization::ResolveBlueprint(const UObject* Object) const
{
	for (const UObject* Outer = Object; Outer; Outer = Outer->GetOuter())
	{
		if (UBlueprint* Blueprint = const_cast<UBlueprint*>(Cast<UBlueprint>(Outer)))
		{
			return Blueprint;
		}

		if (const UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Outer))
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(BlueprintGeneratedClass->ClassGeneratedBy))
			{
				return Blueprint;
			}
		}

		if (const UClass* Class = Cast<UClass>(Outer))
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Class->ClassGeneratedBy))
			{
				return Blueprint;
			}
		}
	}

	if (const UActorComponent* ActorComponent = Cast<UActorComponent>(Object))
	{
		if (const AActor* Owner = ActorComponent->GetOwner())
		{
			if (UBlueprint* Blueprint = Cast<UBlueprint>(Owner->GetClass()->ClassGeneratedBy))
			{
				return Blueprint;
			}
		}
	}

	return nullptr;
}

AActor* FReplicSceneComponentDetailsCustomization::ResolveOwnerActor(const USceneComponent* SceneComponent) const
{
	if (!SceneComponent)
	{
		return nullptr;
	}

	if (AActor* Owner = SceneComponent->GetOwner())
	{
		return Owner;
	}

	for (UObject* Outer = SceneComponent->GetOuter(); Outer; Outer = Outer->GetOuter())
	{
		if (AActor* OuterActor = Cast<AActor>(Outer))
		{
			return OuterActor;
		}

		if (UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Outer))
		{
			return Cast<AActor>(BlueprintGeneratedClass->GetDefaultObject(false));
		}

		if (UBlueprint* Blueprint = Cast<UBlueprint>(Outer))
		{
			if (Blueprint->GeneratedClass)
			{
				return Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false));
			}
		}
	}

	if (const UBlueprint* Blueprint = ResolveBlueprint(SceneComponent))
	{
		if (Blueprint->GeneratedClass)
		{
			return Cast<AActor>(Blueprint->GeneratedClass->GetDefaultObject(false));
		}
	}

	return nullptr;
}

UReplicTransportComponent* FReplicSceneComponentDetailsCustomization::ResolveTransport(const USceneComponent* SceneComponent) const
{
	AActor* OwnerActor = ResolveOwnerActor(SceneComponent);
	if (OwnerActor)
	{
		if (UReplicTransportComponent* Transport = OwnerActor->FindComponentByClass<UReplicTransportComponent>())
		{
			return Transport;
		}
	}

	if (UBlueprint* Blueprint = ResolveBlueprint(SceneComponent))
	{
		if (const USimpleConstructionScript* SCS = Blueprint->SimpleConstructionScript)
		{
			for (USCS_Node* SCSNode : SCS->GetAllNodes())
			{
				if (!SCSNode || !SCSNode->ComponentClass || !SCSNode->ComponentClass->IsChildOf<UReplicTransportComponent>())
				{
					continue;
				}

				if (UReplicTransportComponent* Transport = Cast<UReplicTransportComponent>(SCSNode->ComponentTemplate))
				{
					return Transport;
				}

				if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
				{
					if (UReplicTransportComponent* Transport = Cast<UReplicTransportComponent>(SCSNode->GetActualComponentTemplate(GeneratedClass)))
					{
						return Transport;
					}
				}
			}
		}

		if (UBlueprintGeneratedClass* GeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass))
		{
			if (AActor* BlueprintCDO = Cast<AActor>(GeneratedClass->GetDefaultObject(false)))
			{
				if (UReplicTransportComponent* Transport = BlueprintCDO->FindComponentByClass<UReplicTransportComponent>())
				{
					return Transport;
				}
			}
		}
	}

	return nullptr;
}

FReplicComponentTransformSettings* FReplicSceneComponentDetailsCustomization::FindSettings() const
{
	USceneComponent* SceneComponent = SelectedComponent.Get();
	UReplicTransportComponent* Transport = SelectedTransport.Get();
	if (!SceneComponent || !Transport)
	{
		return nullptr;
	}

	return Transport->ComponentTransformSettings.FindByPredicate(
		[SceneComponent](const FReplicComponentTransformSettings& Settings)
		{
			return NormalizeComponentName(Settings.ComponentName) == NormalizeComponentName(SceneComponent->GetFName());
		});
}

FReplicComponentTransformSettings& FReplicSceneComponentDetailsCustomization::FindOrAddSettings() const
{
	USceneComponent* SceneComponent = SelectedComponent.Get();
	UReplicTransportComponent* Transport = SelectedTransport.Get();
	check(SceneComponent);
	check(Transport);

	if (FReplicComponentTransformSettings* ExistingSettings = FindSettings())
	{
		return *ExistingSettings;
	}

	FReplicComponentTransformSettings& NewSettings = Transport->ComponentTransformSettings.AddDefaulted_GetRef();
	NewSettings.ComponentName = NormalizeComponentName(SceneComponent->GetFName());
	return NewSettings;
}

void FReplicSceneComponentDetailsCustomization::MarkTransportChanged() const
{
	if (UReplicTransportComponent* Transport = SelectedTransport.Get())
	{
		Transport->RefreshComponentTransformTracking(true);
		if (UPackage* Package = Transport->GetOutermost())
		{
			Package->MarkPackageDirty();
		}
	}

	if (UBlueprint* Blueprint = ResolveBlueprint(SelectedComponent.Get()))
	{
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}
}

bool FReplicSceneComponentDetailsCustomization::HasValidReplicOwner() const
{
	const USceneComponent* SceneComponent = SelectedComponent.Get();
	const UReplicTransportComponent* Transport = SelectedTransport.Get();
	const AActor* OwnerActor = ResolveOwnerActor(SceneComponent);
	return SceneComponent && Transport && OwnerActor && OwnerActor->GetIsReplicated();
}

bool FReplicSceneComponentDetailsCustomization::IsChannelEnabled(bool FReplicComponentTransformSettings::*Member) const
{
	if (const FReplicComponentTransformSettings* Settings = FindSettings())
	{
		return Settings->*Member;
	}

	return false;
}

void FReplicSceneComponentDetailsCustomization::SetChannelEnabled(bool FReplicComponentTransformSettings::*Member, bool bEnabled) const
{
	if (!HasValidReplicOwner())
	{
		return;
	}

	UReplicTransportComponent* Transport = SelectedTransport.Get();
	if (!Transport)
	{
		return;
	}

	if (IsChannelEnabled(Member) == bEnabled)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeReplicComponentTransformChannel", "Change Replic Component Transform Channel"));
	if (UBlueprint* Blueprint = ResolveBlueprint(SelectedComponent.Get()))
	{
		Blueprint->Modify();
	}
	if (USceneComponent* SceneComponent = SelectedComponent.Get())
	{
		SceneComponent->Modify();
	}
	Transport->Modify();
	FReplicComponentTransformSettings& Settings = FindOrAddSettings();
	Settings.*Member = bEnabled;
	MarkTransportChanged();
}

ECheckBoxState FReplicSceneComponentDetailsCustomization::GetChannelCheckState(bool FReplicComponentTransformSettings::*Member) const
{
	return IsChannelEnabled(Member) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void FReplicSceneComponentDetailsCustomization::OnChannelCheckStateChanged(ECheckBoxState NewState, bool FReplicComponentTransformSettings::*Member) const
{
	SetChannelEnabled(Member, NewState == ECheckBoxState::Checked);
}

TOptional<float> FReplicSceneComponentDetailsCustomization::GetFloatSettingValue(float FReplicComponentTransformSettings::*Member) const
{
	if (const FReplicComponentTransformSettings* Settings = FindSettings())
	{
		return Settings->*Member;
	}

	FReplicComponentTransformSettings Defaults;
	return Defaults.*Member;
}

void FReplicSceneComponentDetailsCustomization::SetFloatSettingValue(float NewValue, float FReplicComponentTransformSettings::*Member) const
{
	if (!HasValidReplicOwner())
	{
		return;
	}

	UReplicTransportComponent* Transport = SelectedTransport.Get();
	if (!Transport)
	{
		return;
	}

	const float ClampedNewValue = FMath::Max(NewValue, 0.0f);
	if (const FReplicComponentTransformSettings* ExistingSettings = FindSettings())
	{
		if (FMath::IsNearlyEqual(ExistingSettings->*Member, ClampedNewValue))
		{
			return;
		}
	}
	else
	{
		FReplicComponentTransformSettings Defaults;
		if (FMath::IsNearlyEqual(Defaults.*Member, ClampedNewValue))
		{
			return;
		}
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeReplicComponentTransformAdvancedSetting", "Change Replic Component Transform Advanced Setting"));
	if (UBlueprint* Blueprint = ResolveBlueprint(SelectedComponent.Get()))
	{
		Blueprint->Modify();
	}
	if (USceneComponent* SceneComponent = SelectedComponent.Get())
	{
		SceneComponent->Modify();
	}
	Transport->Modify();
	FReplicComponentTransformSettings& Settings = FindOrAddSettings();
	Settings.*Member = ClampedNewValue;
	MarkTransportChanged();
}

EReplicTransformSpace FReplicSceneComponentDetailsCustomization::GetTransformSpace() const
{
	if (const FReplicComponentTransformSettings* Settings = FindSettings())
	{
		return Settings->TransformSpace;
	}

	return EReplicTransformSpace::Relative;
}

void FReplicSceneComponentDetailsCustomization::SetTransformSpace(EReplicTransformSpace NewSpace) const
{
	if (!HasValidReplicOwner())
	{
		return;
	}

	UReplicTransportComponent* Transport = SelectedTransport.Get();
	if (!Transport)
	{
		return;
	}

	if (GetTransformSpace() == NewSpace)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("ChangeReplicComponentTransformSpace", "Change Replic Component Transform Space"));
	if (UBlueprint* Blueprint = ResolveBlueprint(SelectedComponent.Get()))
	{
		Blueprint->Modify();
	}
	if (USceneComponent* SceneComponent = SelectedComponent.Get())
	{
		SceneComponent->Modify();
	}
	Transport->Modify();
	FReplicComponentTransformSettings& Settings = FindOrAddSettings();
	Settings.TransformSpace = NewSpace;
	MarkTransportChanged();
}

TSharedRef<SWidget> FReplicSceneComponentDetailsCustomization::MakeTransformSpaceWidget(TSharedPtr<EReplicTransformSpace> Space) const
{
	return SNew(STextBlock)
		.Text(Space.IsValid() ? TransformSpaceToText(*Space) : FText::GetEmpty())
		.Font(IDetailLayoutBuilder::GetDetailFont());
}

FText FReplicSceneComponentDetailsCustomization::GetTransformSpaceText() const
{
	return TransformSpaceToText(GetTransformSpace());
}

FText FReplicSceneComponentDetailsCustomization::GetPrerequisiteText() const
{
	if (!SelectedComponent.IsValid())
	{
		return LOCTEXT("NoComponent", "Replic transform settings require a selected SceneComponent.");
	}

	if (!SelectedTransport.IsValid())
	{
		return LOCTEXT("NoTransport", "Add a ReplicTransportComponent to this Actor before enabling component transform replication.");
	}

	const AActor* OwnerActor = ResolveOwnerActor(SelectedComponent.Get());
	if (!OwnerActor || !OwnerActor->GetIsReplicated())
	{
		return LOCTEXT("ActorNotReplicated", "Enable Replicates in this Actor's Class Defaults before enabling component transform replication.");
	}

	return FText::GetEmpty();
}

#undef LOCTEXT_NAMESPACE
