#pragma once

#include "CoreMinimal.h"
#include "ReplicPinOptionResolver.h"
#include "SGraphPin.h"

template<typename OptionType>
class SComboBox;

class SReplicEnumValuePin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SReplicEnumValuePin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj);

protected:
	virtual TSharedRef<SWidget> GetDefaultValueWidget() override;

private:
	TSharedPtr<FReplicPinOptionItem> MakeNoneItem() const;
	void RefreshOptions();
	void HandleComboBoxOpening();
	void HandleSelectionChanged(TSharedPtr<FReplicPinOptionItem> SelectedItem, ESelectInfo::Type SelectInfo);
	TSharedRef<SWidget> GenerateOptionWidget(TSharedPtr<FReplicPinOptionItem> Item) const;
	TSharedPtr<FReplicPinOptionItem> GetSelectedItem() const;
	FText GetSelectedItemText() const;
	FText GetSelectedItemTooltip() const;

	TSharedPtr<SComboBox<TSharedPtr<FReplicPinOptionItem>>> ComboBox;
	TArray<TSharedPtr<FReplicPinOptionItem>> Options;
};
