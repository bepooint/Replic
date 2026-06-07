#include "ReplicLibrary.h"

#include "ReplicPropertyObserver.h"
#include "ReplicRuntimeUtils.h"
#include "ReplicTransportComponent.h"
#include "Templates/SubclassOf.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// Blueprint enums can be emitted either as FEnumProperty or as FByteProperty with an attached UEnum.
	// Treat both shapes as enum-like so the editor/runtime behavior matches native and Blueprint enums.
	bool IsEnumLikeProperty(const FProperty* Property)
	{
		if (CastField<FEnumProperty>(Property))
		{
			return true;
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			return ByteProperty->Enum != nullptr;
		}

		return false;
	}

	template<typename TValueType, typename TSerializer>
	bool SetMarkedSerializedValue(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TValueType& Value, TSerializer&& Serializer)
	{
		UObject* EffectiveTargetObject = TargetObject ? TargetObject : ContextObject;
		if (!ContextObject || !EffectiveTargetObject)
		{
			return false;
		}

		FReplicResolvedTarget ResolvedRequester;
		if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, ResolvedRequester) || !ResolvedRequester.HostActor)
		{
			return false;
		}

		// Keep the target registered so late-created observers such as widgets can receive already existing persistent state.
		UReplicLibrary::RegisterObservedObject(EffectiveTargetObject);

		if (UReplicTransportComponent* RequestTransport = UReplicTransportComponent::FindOrCreate(ResolvedRequester.HostActor))
		{
			return RequestTransport->RequestMarkedPropertyWrite(ContextObject, EffectiveTargetObject, PropertyName, Serializer(Value));
		}

		return false;
	}

	template<typename TValueType, typename TSerializer>
	FReplicNamedValue MakeSerializedNamedValue(FName Name, const TValueType& Value, TSerializer&& Serializer)
	{
		FReplicNamedValue NamedValue;
		NamedValue.Name = Name;
		NamedValue.SerializedValue = Serializer(Value);
		return NamedValue;
	}

	static FString SerializeText(const FText& Value)
	{
		FString SerializedText;
		FTextStringHelper::WriteToBuffer(SerializedText, Value);
		return SerializedText;
	}

	template<typename TPredicate>
	TArray<FString> CollectMarkedPropertyOptions(TPredicate&& Predicate)
	{
		// These helpers back the raw Blueprint function library nodes.
		// Editor-side nodes use ReplicPinOptionResolver for target-aware filtering, but the library API still needs
		// a global fallback so the underlying UFUNCTION metadata remains valid in generic contexts.
		TSet<FString> UniqueOptions;

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated))
			{
				continue;
			}

			for (TFieldIterator<FProperty> PropertyIt(Class, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (!ReplicRuntimeUtils::IsMarkedVariable(Property) || !Predicate(Property))
				{
					continue;
				}

				UniqueOptions.Add(Property->GetName());
			}
		}

		TArray<FString> SortedOptions = UniqueOptions.Array();
		SortedOptions.Sort();
		return SortedOptions;
	}

	TArray<FString> CollectMarkedEventOptions()
	{
		TSet<FString> UniqueOptions;

		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* Class = *ClassIt;
			if (!Class || Class->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated))
			{
				continue;
			}

			for (TFieldIterator<UFunction> FunctionIt(Class, EFieldIterationFlags::IncludeSuper); FunctionIt; ++FunctionIt)
			{
				UFunction* Function = *FunctionIt;
				if (ReplicRuntimeUtils::IsMarkedEvent(Function))
				{
					UniqueOptions.Add(Function->GetName());
				}
			}
		}

		TArray<FString> SortedOptions = UniqueOptions.Array();
		SortedOptions.Sort();
		return SortedOptions;
	}
}

void UReplicLibrary::RegisterObservedObject(UObject* ObservedObject)
{
	ReplicRuntimeUtils::RegisterObservedObject(ObservedObject);

	FReplicResolvedTarget ResolvedTarget;
	if (ReplicRuntimeUtils::ResolveTarget(ObservedObject, ResolvedTarget))
	{
		if (UReplicTransportComponent* Transport = UReplicTransportComponent::FindOnActor(ResolvedTarget.HostActor))
		{
			Transport->ApplyStoredStateToObservedObject(ObservedObject);
		}
	}
}

void UReplicLibrary::UnregisterObservedObject(UObject* ObservedObject)
{
	ReplicRuntimeUtils::UnregisterObservedObject(ObservedObject);
}

UReplicPropertyObserver* UReplicLibrary::BindMarkedPropertyChanged(UObject* TargetObject, FName PropertyName)
{
	if (!TargetObject)
	{
		return nullptr;
	}

	FReplicResolvedTarget ResolvedTarget;
	if (!ReplicRuntimeUtils::ResolveTarget(TargetObject, ResolvedTarget) || !ResolvedTarget.HostActor)
	{
		return nullptr;
	}

	UReplicTransportComponent* Transport = UReplicTransportComponent::FindOnActor(ResolvedTarget.HostActor);
	if (!Transport)
	{
		return nullptr;
	}

	if (!PropertyName.IsNone())
	{
		FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
		if (!TargetProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
		{
			return nullptr;
		}
	}

	// The observer lives under the target object so its lifetime naturally follows the data owner unless the caller
	// stores it even longer on purpose.
	UReplicPropertyObserver* Observer = NewObject<UReplicPropertyObserver>(TargetObject);
	Observer->Initialize(Transport, TargetObject, PropertyName);
	return Observer;
}

void UReplicLibrary::UnbindMarkedPropertyChanged(UReplicPropertyObserver* Observer)
{
	if (Observer)
	{
		Observer->Unbind();
	}
}

bool UReplicLibrary::GetMarkedBool(UObject* TargetObject, FName PropertyName, bool& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FBoolProperty* BoolProperty = CastField<FBoolProperty>(TargetProperty);
	if (!BoolProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = BoolProperty->GetPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedInt(UObject* TargetObject, FName PropertyName, int32& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FIntProperty* IntProperty = CastField<FIntProperty>(TargetProperty);
	if (!IntProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = IntProperty->GetPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedFloat(UObject* TargetObject, FName PropertyName, double& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	if (!ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	if (const FDoubleProperty* DoubleProperty = CastField<FDoubleProperty>(TargetProperty))
	{
		Value = DoubleProperty->GetPropertyValue_InContainer(TargetObject);
		return true;
	}

	if (const FFloatProperty* FloatProperty = CastField<FFloatProperty>(TargetProperty))
	{
		Value = FloatProperty->GetPropertyValue_InContainer(TargetObject);
		return true;
	}

	return false;
}

bool UReplicLibrary::GetMarkedByte(UObject* TargetObject, FName PropertyName, uint8& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	if (!ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(TargetProperty))
	{
		Value = ByteProperty->GetPropertyValue_InContainer(TargetObject);
		return true;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(TargetProperty))
	{
		const void* EnumValuePtr = EnumProperty->ContainerPtrToValuePtr<void>(TargetObject);
		Value = static_cast<uint8>(EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(EnumValuePtr));
		return true;
	}

	return false;
}

bool UReplicLibrary::GetMarkedEnum(UObject* TargetObject, FName PropertyName, uint8& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	if (!TargetProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(TargetProperty))
	{
		const void* EnumValuePtr = EnumProperty->ContainerPtrToValuePtr<void>(TargetObject);
		Value = static_cast<uint8>(EnumProperty->GetUnderlyingProperty()->GetUnsignedIntPropertyValue(EnumValuePtr));
		return true;
	}

	if (const FByteProperty* ByteProperty = CastField<FByteProperty>(TargetProperty))
	{
		if (ByteProperty->Enum)
		{
			Value = ByteProperty->GetPropertyValue_InContainer(TargetObject);
			return true;
		}
	}

	return false;
}

bool UReplicLibrary::GetMarkedStruct(UObject* TargetObject, FName PropertyName, int32& Value)
{
	// Placeholder required by CustomThunk. execGetMarkedStruct performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execGetMarkedStruct)
{
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = GetMarkedValueIntoProperty(TargetObject, PropertyName, ValueProperty, ValuePtr);
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::GetMarkedArray(UObject* TargetObject, FName PropertyName, TArray<int32>& Value)
{
	// Placeholder required by CustomThunk. execGetMarkedArray performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execGetMarkedArray)
{
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = GetMarkedValueIntoProperty(TargetObject, PropertyName, ValueProperty, ValuePtr);
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::GetMarkedArrayInternal(UObject* TargetObject, FName PropertyName, TArray<int32>& Value)
{
	// Placeholder required by CustomThunk. execGetMarkedArrayInternal performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execGetMarkedArrayInternal)
{
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = GetMarkedValueIntoProperty(TargetObject, PropertyName, ValueProperty, ValuePtr);
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::GetMarkedSet(UObject* TargetObject, FName PropertyName, TSet<int32>& Value)
{
	// Placeholder required by CustomThunk. execGetMarkedSet performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execGetMarkedSet)
{
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FSetProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = GetMarkedValueIntoProperty(TargetObject, PropertyName, ValueProperty, ValuePtr);
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::GetMarkedMap(UObject* TargetObject, FName PropertyName, TMap<int32, int32>& Value)
{
	// Placeholder required by CustomThunk. execGetMarkedMap performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execGetMarkedMap)
{
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FMapProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = GetMarkedValueIntoProperty(TargetObject, PropertyName, ValueProperty, ValuePtr);
	*(bool*)RESULT_PARAM = bSuccess;
}

#define REPLIC_DEFINE_SETTER(FunctionName, CppType, PropertyClass) \
bool UReplicLibrary::FunctionName(UObject* ContextObject, UObject* TargetObject, FName PropertyName, CppType Value) \
{ \
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const auto& TypedValue) { return LexToString(TypedValue); }); \
}

REPLIC_DEFINE_SETTER(SetMarkedBool, bool, FBoolProperty)
REPLIC_DEFINE_SETTER(SetMarkedInt, int32, FIntProperty)
REPLIC_DEFINE_SETTER(SetMarkedFloat, double, FDoubleProperty)
REPLIC_DEFINE_SETTER(SetMarkedByte, uint8, FByteProperty)
REPLIC_DEFINE_SETTER(SetMarkedEnum, uint8, FEnumProperty)

bool UReplicLibrary::SetMarkedStruct(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Value)
{
	// Placeholder required by CustomThunk. execSetMarkedStruct performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetMarkedStruct)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	bool bSuccess = false;
	if (ValueProperty && ValuePtr)
	{
		bSuccess = SetMarkedValueFromProperty(ContextObject, TargetObject, PropertyName, ValueProperty, ValuePtr);
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::SetMarkedArray(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TArray<int32>& Value)
{
	// Placeholder required by CustomThunk. execSetMarkedArray performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetMarkedArray)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	bool bSuccess = false;
	if (ValueProperty && ValuePtr)
	{
		bSuccess = SetMarkedValueFromProperty(ContextObject, TargetObject, PropertyName, ValueProperty, ValuePtr);
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::SetMarkedArrayInternal(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TArray<int32>& Value)
{
	// Placeholder required by CustomThunk. execSetMarkedArrayInternal performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetMarkedArrayInternal)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	bool bSuccess = false;
	if (ValueProperty && ValuePtr)
	{
		bSuccess = SetMarkedValueFromProperty(ContextObject, TargetObject, PropertyName, ValueProperty, ValuePtr);
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::SetMarkedSet(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TSet<int32>& Value)
{
	// Placeholder required by CustomThunk. execSetMarkedSet performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetMarkedSet)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FSetProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	bool bSuccess = false;
	if (ValueProperty && ValuePtr)
	{
		bSuccess = SetMarkedValueFromProperty(ContextObject, TargetObject, PropertyName, ValueProperty, ValuePtr);
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::SetMarkedMap(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TMap<int32, int32>& Value)
{
	// Placeholder required by CustomThunk. execSetMarkedMap performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetMarkedMap)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FMapProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	bool bSuccess = false;
	if (ValueProperty && ValuePtr)
	{
		bSuccess = SetMarkedValueFromProperty(ContextObject, TargetObject, PropertyName, ValueProperty, ValuePtr);
	}

	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::AddToMarkedArray(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Item)
{
	// Placeholder required by CustomThunk. execAddToMarkedArray performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execAddToMarkedArray)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ItemProperty = Stack.MostRecentProperty;
	const void* ItemPtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = ItemProperty && ItemPtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::AddArrayItem,
			ItemProperty,
			ItemPtr,
			nullptr,
			nullptr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::RemoveFromMarkedArray(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Item)
{
	// Placeholder required by CustomThunk. execRemoveFromMarkedArray performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execRemoveFromMarkedArray)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ItemProperty = Stack.MostRecentProperty;
	const void* ItemPtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = ItemProperty && ItemPtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::RemoveArrayItem,
			ItemProperty,
			ItemPtr,
			nullptr,
			nullptr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::AddToMarkedSet(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Item)
{
	// Placeholder required by CustomThunk. execAddToMarkedSet performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execAddToMarkedSet)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ItemProperty = Stack.MostRecentProperty;
	const void* ItemPtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = ItemProperty && ItemPtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::AddSetItem,
			ItemProperty,
			ItemPtr,
			nullptr,
			nullptr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::RemoveFromMarkedSet(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Item)
{
	// Placeholder required by CustomThunk. execRemoveFromMarkedSet performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execRemoveFromMarkedSet)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ItemProperty = Stack.MostRecentProperty;
	const void* ItemPtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = ItemProperty && ItemPtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::RemoveSetItem,
			ItemProperty,
			ItemPtr,
			nullptr,
			nullptr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::SetInMarkedMap(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Key, const int32& Value)
{
	// Placeholder required by CustomThunk. execSetInMarkedMap performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execSetInMarkedMap)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* KeyProperty = Stack.MostRecentProperty;
	const void* KeyPtr = Stack.MostRecentPropertyAddress;
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = KeyProperty && KeyPtr && ValueProperty && ValuePtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::SetMapEntry,
			KeyProperty,
			KeyPtr,
			ValueProperty,
			ValuePtr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::RemoveFromMarkedMap(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const int32& Key)
{
	// Placeholder required by CustomThunk. execRemoveFromMarkedMap performs the real typed work.
	return false;
}

DEFINE_FUNCTION(UReplicLibrary::execRemoveFromMarkedMap)
{
	P_GET_OBJECT(UObject, ContextObject);
	P_GET_OBJECT(UObject, TargetObject);
	P_GET_PROPERTY(FNameProperty, PropertyName);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* KeyProperty = Stack.MostRecentProperty;
	const void* KeyPtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	const bool bSuccess = KeyProperty && KeyPtr
		? RequestMarkedContainerDeltaFromProperties(
			ContextObject,
			TargetObject,
			PropertyName,
			EReplicContainerDeltaOperation::RemoveMapEntry,
			KeyProperty,
			KeyPtr,
			nullptr,
			nullptr)
		: false;
	*(bool*)RESULT_PARAM = bSuccess;
}

bool UReplicLibrary::CallMarkedEvent(UObject* ContextObject, UObject* TargetObject, FName EventName, const TArray<FReplicNamedValue>& Arguments)
{
	UObject* EffectiveTargetObject = TargetObject ? TargetObject : ContextObject;
	if (!ContextObject || !EffectiveTargetObject)
	{
		return false;
	}

	FReplicResolvedTarget ResolvedRequester;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, ResolvedRequester) || !ResolvedRequester.HostActor)
	{
		return false;
	}

	if (UReplicTransportComponent* RequestTransport = UReplicTransportComponent::FindOrCreate(ResolvedRequester.HostActor))
	{
		return RequestTransport->RequestMarkedEvent(ContextObject, EffectiveTargetObject, EventName, Arguments);
	}

	return false;
}

#define REPLIC_DEFINE_NAMED_VALUE(FunctionName, CppType, PropertyClass) \
FReplicNamedValue UReplicLibrary::FunctionName(FName Name, CppType Value) \
{ \
	return MakeSerializedNamedValue(Name, Value, [](const auto& TypedValue) { return LexToString(TypedValue); }); \
}

REPLIC_DEFINE_NAMED_VALUE(MakeNamedBoolValue, bool, FBoolProperty)
REPLIC_DEFINE_NAMED_VALUE(MakeNamedIntValue, int32, FIntProperty)
REPLIC_DEFINE_NAMED_VALUE(MakeNamedFloatValue, double, FDoubleProperty)

FReplicNamedValue UReplicLibrary::MakeNamedStructValue(FName Name, const int32& Value)
{
	// Placeholder required by CustomThunk. execMakeNamedStructValue performs the real typed work.
	return FReplicNamedValue();
}

DEFINE_FUNCTION(UReplicLibrary::execMakeNamedStructValue)
{
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.StepCompiledIn<FStructProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	FReplicNamedValue NamedValue;
	if (ValueProperty && ValuePtr)
	{
		NamedValue = MakeNamedValueFromProperty(Name, ValueProperty, ValuePtr);
	}

	*(FReplicNamedValue*)RESULT_PARAM = NamedValue;
}

FReplicNamedValue UReplicLibrary::MakeNamedArrayValue(FName Name, const TArray<int32>& Value)
{
	// Placeholder required by CustomThunk. execMakeNamedArrayValue performs the real typed work.
	return FReplicNamedValue();
}

DEFINE_FUNCTION(UReplicLibrary::execMakeNamedArrayValue)
{
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.StepCompiledIn<FArrayProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	FReplicNamedValue NamedValue;
	if (ValueProperty && ValuePtr)
	{
		NamedValue = MakeNamedValueFromProperty(Name, ValueProperty, ValuePtr);
	}

	*(FReplicNamedValue*)RESULT_PARAM = NamedValue;
}

FReplicNamedValue UReplicLibrary::MakeNamedSetValue(FName Name, const TSet<int32>& Value)
{
	// Placeholder required by CustomThunk. execMakeNamedSetValue performs the real typed work.
	return FReplicNamedValue();
}

DEFINE_FUNCTION(UReplicLibrary::execMakeNamedSetValue)
{
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.StepCompiledIn<FSetProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	FReplicNamedValue NamedValue;
	if (ValueProperty && ValuePtr)
	{
		NamedValue = MakeNamedValueFromProperty(Name, ValueProperty, ValuePtr);
	}

	*(FReplicNamedValue*)RESULT_PARAM = NamedValue;
}

FReplicNamedValue UReplicLibrary::MakeNamedMapValue(FName Name, const TMap<int32, int32>& Value)
{
	// Placeholder required by CustomThunk. execMakeNamedMapValue performs the real typed work.
	return FReplicNamedValue();
}

DEFINE_FUNCTION(UReplicLibrary::execMakeNamedMapValue)
{
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.StepCompiledIn<FMapProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	FReplicNamedValue NamedValue;
	if (ValueProperty && ValuePtr)
	{
		NamedValue = MakeNamedValueFromProperty(Name, ValueProperty, ValuePtr);
	}

	*(FReplicNamedValue*)RESULT_PARAM = NamedValue;
}

FReplicNamedValue UReplicLibrary::MakeNamedGenericValue(FName Name, const int32& Value)
{
	// Placeholder required by CustomThunk. execMakeNamedGenericValue performs the real typed work.
	return FReplicNamedValue();
}

DEFINE_FUNCTION(UReplicLibrary::execMakeNamedGenericValue)
{
	P_GET_PROPERTY(FNameProperty, Name);
	Stack.StepCompiledIn<FProperty>(nullptr);
	const FProperty* ValueProperty = Stack.MostRecentProperty;
	const void* ValuePtr = Stack.MostRecentPropertyAddress;
	P_FINISH;

	FReplicNamedValue NamedValue;
	if (ValueProperty && ValuePtr)
	{
		NamedValue = MakeNamedValueFromProperty(Name, ValueProperty, ValuePtr);
	}

	*(FReplicNamedValue*)RESULT_PARAM = NamedValue;
}

bool UReplicLibrary::SetMarkedValueFromProperty(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FProperty* ValueProperty, const void* ValuePtr)
{
	UObject* EffectiveTargetObject = TargetObject ? TargetObject : ContextObject;
	if (!ContextObject || !EffectiveTargetObject || !ValueProperty || !ValuePtr)
	{
		return false;
	}

	FReplicResolvedTarget ResolvedRequester;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, ResolvedRequester) || !ResolvedRequester.HostActor)
	{
		return false;
	}

	FString SerializedValue;
	if (!ReplicRuntimeUtils::ExportPropertyValueToText(ValueProperty, ValuePtr, SerializedValue))
	{
		return false;
	}

	// Keep the target registered so late-created observers such as widgets can receive already existing persistent state.
	// This keeps setter-based writes and observer/persistent-state behavior aligned for both direct actor targets and
	// secondary observed objects.
	RegisterObservedObject(EffectiveTargetObject);

	if (UReplicTransportComponent* RequestTransport = UReplicTransportComponent::FindOrCreate(ResolvedRequester.HostActor))
	{
		return RequestTransport->RequestMarkedPropertyWrite(ContextObject, EffectiveTargetObject, PropertyName, SerializedValue);
	}

	return false;
}

bool UReplicLibrary::RequestMarkedContainerDeltaFromProperties(
	UObject* ContextObject,
	UObject* TargetObject,
	FName PropertyName,
	EReplicContainerDeltaOperation Operation,
	const FProperty* PrimaryProperty,
	const void* PrimaryValuePtr,
	const FProperty* SecondaryProperty,
	const void* SecondaryValuePtr)
{
	UObject* EffectiveTargetObject = TargetObject ? TargetObject : ContextObject;
	if (!ContextObject || !EffectiveTargetObject || !PrimaryProperty || !PrimaryValuePtr)
	{
		return false;
	}

	FReplicResolvedTarget ResolvedRequester;
	if (!ReplicRuntimeUtils::ResolveTarget(ContextObject, ResolvedRequester) || !ResolvedRequester.HostActor)
	{
		return false;
	}

	FString SerializedPrimaryValue;
	if (!ReplicRuntimeUtils::ExportPropertyValueToText(PrimaryProperty, PrimaryValuePtr, SerializedPrimaryValue))
	{
		return false;
	}

	FString SerializedSecondaryValue;
	if (SecondaryProperty && SecondaryValuePtr && !ReplicRuntimeUtils::ExportPropertyValueToText(SecondaryProperty, SecondaryValuePtr, SerializedSecondaryValue))
	{
		return false;
	}

	RegisterObservedObject(EffectiveTargetObject);

	if (UReplicTransportComponent* RequestTransport = UReplicTransportComponent::FindOrCreate(ResolvedRequester.HostActor))
	{
		return RequestTransport->RequestMarkedContainerDelta(
			ContextObject,
			EffectiveTargetObject,
			PropertyName,
			Operation,
			SerializedPrimaryValue,
			SerializedSecondaryValue);
	}

	return false;
}

bool UReplicLibrary::GetMarkedValueIntoProperty(UObject* TargetObject, FName PropertyName, const FProperty* OutputProperty, void* OutputValuePtr)
{
	if (!TargetObject || !OutputProperty || !OutputValuePtr || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	if (!TargetProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	const void* SourceValuePtr = TargetProperty->ContainerPtrToValuePtr<void>(TargetObject);
	return ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(TargetProperty, SourceValuePtr, OutputProperty, OutputValuePtr);
}

FReplicNamedValue UReplicLibrary::MakeNamedValueFromProperty(FName Name, const FProperty* ValueProperty, const void* ValuePtr)
{
	FReplicNamedValue NamedValue;
	NamedValue.Name = Name;
	if (ValueProperty && ValuePtr)
	{
		ReplicRuntimeUtils::ExportPropertyValueToText(ValueProperty, ValuePtr, NamedValue.SerializedValue);
	}
	return NamedValue;
}

bool UReplicLibrary::SetMarkedName(UObject* ContextObject, UObject* TargetObject, FName PropertyName, FName Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FName& TypedValue) { return TypedValue.ToString(); });
}

bool UReplicLibrary::SetMarkedString(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FString& Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FString& TypedValue) { return TypedValue; });
}

bool UReplicLibrary::SetMarkedText(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FText& Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FText& TypedValue) { return SerializeText(TypedValue); });
}

bool UReplicLibrary::SetMarkedVector(UObject* ContextObject, UObject* TargetObject, FName PropertyName, FVector Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FVector& TypedValue) { return TypedValue.ToString(); });
}

bool UReplicLibrary::SetMarkedRotator(UObject* ContextObject, UObject* TargetObject, FName PropertyName, FRotator Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FRotator& TypedValue) { return TypedValue.ToString(); });
}

bool UReplicLibrary::SetMarkedTransform(UObject* ContextObject, UObject* TargetObject, FName PropertyName, FTransform Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const FTransform& TypedValue) { return TypedValue.ToString(); });
}

bool UReplicLibrary::SetMarkedObject(UObject* ContextObject, UObject* TargetObject, FName PropertyName, UObject* Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const UObject* TypedValue) { return GetPathNameSafe(TypedValue); });
}

bool UReplicLibrary::SetMarkedClass(UObject* ContextObject, UObject* TargetObject, FName PropertyName, TSubclassOf<UObject> Value)
{
	return SetMarkedSerializedValue(ContextObject, TargetObject, PropertyName, Value, [](const TSubclassOf<UObject>& TypedValue) { return GetPathNameSafe(TypedValue.Get()); });
}

bool UReplicLibrary::GetMarkedName(UObject* TargetObject, FName PropertyName, FName& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FNameProperty* NameProperty = CastField<FNameProperty>(TargetProperty);
	if (!NameProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = NameProperty->GetPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedString(UObject* TargetObject, FName PropertyName, FString& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FStrProperty* StringProperty = CastField<FStrProperty>(TargetProperty);
	if (!StringProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = StringProperty->GetPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedText(UObject* TargetObject, FName PropertyName, FText& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FTextProperty* TextProperty = CastField<FTextProperty>(TargetProperty);
	if (!TextProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = TextProperty->GetPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedVector(UObject* TargetObject, FName PropertyName, FVector& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FStructProperty* StructProperty = CastField<FStructProperty>(TargetProperty);
	if (!StructProperty || StructProperty->Struct != TBaseStructure<FVector>::Get() || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = *StructProperty->ContainerPtrToValuePtr<FVector>(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedRotator(UObject* TargetObject, FName PropertyName, FRotator& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FStructProperty* StructProperty = CastField<FStructProperty>(TargetProperty);
	if (!StructProperty || StructProperty->Struct != TBaseStructure<FRotator>::Get() || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = *StructProperty->ContainerPtrToValuePtr<FRotator>(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedTransform(UObject* TargetObject, FName PropertyName, FTransform& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FStructProperty* StructProperty = CastField<FStructProperty>(TargetProperty);
	if (!StructProperty || StructProperty->Struct != TBaseStructure<FTransform>::Get() || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = *StructProperty->ContainerPtrToValuePtr<FTransform>(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedObject(UObject* TargetObject, FName PropertyName, UObject*& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(TargetProperty);
	if (!ObjectProperty || CastField<FClassProperty>(TargetProperty) != nullptr || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = ObjectProperty->GetObjectPropertyValue_InContainer(TargetObject);
	return true;
}

bool UReplicLibrary::GetMarkedClass(UObject* TargetObject, FName PropertyName, TSubclassOf<UObject>& Value)
{
	if (!TargetObject || PropertyName.IsNone())
	{
		return false;
	}

	FProperty* TargetProperty = ReplicRuntimeUtils::FindPropertyByName(TargetObject, PropertyName);
	const FClassProperty* ClassProperty = CastField<FClassProperty>(TargetProperty);
	if (!ClassProperty || !ReplicRuntimeUtils::IsMarkedVariable(TargetObject, PropertyName))
	{
		return false;
	}

	Value = Cast<UClass>(ClassProperty->GetObjectPropertyValue_InContainer(TargetObject));
	return true;
}

FReplicNamedValue UReplicLibrary::MakeNamedNameValue(FName Name, FName Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FName& TypedValue) { return TypedValue.ToString(); });
}

FReplicNamedValue UReplicLibrary::MakeNamedStringValue(FName Name, const FString& Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FString& TypedValue) { return TypedValue; });
}

FReplicNamedValue UReplicLibrary::MakeNamedTextValue(FName Name, const FText& Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FText& TypedValue) { return SerializeText(TypedValue); });
}

FReplicNamedValue UReplicLibrary::MakeNamedVectorValue(FName Name, FVector Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FVector& TypedValue) { return TypedValue.ToString(); });
}

FReplicNamedValue UReplicLibrary::MakeNamedRotatorValue(FName Name, FRotator Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FRotator& TypedValue) { return TypedValue.ToString(); });
}

FReplicNamedValue UReplicLibrary::MakeNamedTransformValue(FName Name, FTransform Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const FTransform& TypedValue) { return TypedValue.ToString(); });
}

FReplicNamedValue UReplicLibrary::MakeNamedObjectValue(FName Name, UObject* Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const UObject* TypedValue) { return GetPathNameSafe(TypedValue); });
}

FReplicNamedValue UReplicLibrary::MakeNamedClassValue(FName Name, TSubclassOf<UObject> Value)
{
	return MakeSerializedNamedValue(Name, Value, [](const TSubclassOf<UObject>& TypedValue) { return GetPathNameSafe(TypedValue.Get()); });
}

TArray<FString> UReplicLibrary::GetMarkedBoolPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FBoolProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedAnyPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return Property != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedIntPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FIntProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedFloatPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		return CastField<FFloatProperty>(Property) != nullptr || CastField<FDoubleProperty>(Property) != nullptr;
	});
}

TArray<FString> UReplicLibrary::GetMarkedBytePropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		return CastField<FByteProperty>(Property) != nullptr && !IsEnumLikeProperty(Property);
	});
}

TArray<FString> UReplicLibrary::GetMarkedEnumPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return IsEnumLikeProperty(Property); });
}

TArray<FString> UReplicLibrary::GetMarkedNamePropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FNameProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedStringPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FStrProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedTextPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FTextProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedVectorPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct == TBaseStructure<FVector>::Get();
		}

		return false;
	});
}

TArray<FString> UReplicLibrary::GetMarkedRotatorPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct == TBaseStructure<FRotator>::Get();
		}

		return false;
	});
}

TArray<FString> UReplicLibrary::GetMarkedTransformPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct == TBaseStructure<FTransform>::Get();
		}

		return false;
	});
}

TArray<FString> UReplicLibrary::GetMarkedObjectPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		return CastField<FObjectPropertyBase>(Property) != nullptr && CastField<FClassProperty>(Property) == nullptr;
	});
}

TArray<FString> UReplicLibrary::GetMarkedClassPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FClassProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedStructPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property)
	{
		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct != TBaseStructure<FVector>::Get()
				&& StructProperty->Struct != TBaseStructure<FRotator>::Get()
				&& StructProperty->Struct != TBaseStructure<FTransform>::Get();
		}

		return false;
	});
}

TArray<FString> UReplicLibrary::GetMarkedArrayPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FArrayProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedSetPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FSetProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedMapPropertyOptions()
{
	return CollectMarkedPropertyOptions([](const FProperty* Property) { return CastField<FMapProperty>(Property) != nullptr; });
}

TArray<FString> UReplicLibrary::GetMarkedEventOptions()
{
	return CollectMarkedEventOptions();
}
