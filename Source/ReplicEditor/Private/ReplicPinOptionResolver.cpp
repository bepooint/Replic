#include "ReplicPinOptionResolver.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "ReplicLibrary.h"
#include "ReplicRuntimeUtils.h"
#include "K2Node_ReplicCallEvent.h"
#include "K2Node_ReplicGetArray.h"
#include "K2Node_ReplicGetEnum.h"
#include "K2Node_ReplicSetArray.h"
#include "K2Node_ReplicSetEnum.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectIterator.h"

namespace
{
	// In Blueprint-generated classes, enums often appear as FByteProperty with Enum metadata instead of FEnumProperty.
	// The editor filters need to treat both shapes the same or PropertyName dropdowns drift away from real Blueprint vars.
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

	struct FReplicCollectedOption
	{
		FName Value = NAME_None;
		FString OriginName;
		FString OriginPath;
		FString TypeName;
		const FProperty* SourceProperty = nullptr;
	};

	bool IsUsableReplicClass(UClass* InClass)
	{
		if (!InClass || InClass == UObject::StaticClass() || InClass->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists))
		{
			return false;
		}

		const FString ClassName = InClass->GetName();
		return !ClassName.StartsWith(TEXT("TRASHCLASS_"));
	}

	void GatherMetadataSourceClasses(UClass* InClass, TArray<UClass*>& OutClasses)
	{
		if (!InClass || !IsUsableReplicClass(InClass))
		{
			return;
		}

		auto AddUniqueClass = [&OutClasses](UClass* SourceClass)
		{
			if (SourceClass && !OutClasses.Contains(SourceClass))
			{
				OutClasses.Add(SourceClass);
			}
		};

		AddUniqueClass(InClass);

		if (UBlueprint* Blueprint = Cast<UBlueprint>(InClass->ClassGeneratedBy))
		{
			// Read from both skeleton and generated classes so dropdowns survive the usual pre-/post-compile Blueprint phases.
			AddUniqueClass(Blueprint->SkeletonGeneratedClass.Get());
			AddUniqueClass(Blueprint->GeneratedClass.Get());
		}
	}

	bool DoesPropertyMatchKind(const FProperty* Property, EReplicPinOptionKind Kind)
	{
		switch (Kind)
		{
		case EReplicPinOptionKind::AnyProperty:
			return Property != nullptr;
		case EReplicPinOptionKind::BoolProperty:
			return CastField<FBoolProperty>(Property) != nullptr;
		case EReplicPinOptionKind::IntProperty:
			return CastField<FIntProperty>(Property) != nullptr;
		case EReplicPinOptionKind::FloatProperty:
			return CastField<FFloatProperty>(Property) != nullptr || CastField<FDoubleProperty>(Property) != nullptr;
		case EReplicPinOptionKind::ByteProperty:
			return CastField<FByteProperty>(Property) != nullptr && !IsEnumLikeProperty(Property);
		case EReplicPinOptionKind::EnumProperty:
			return IsEnumLikeProperty(Property);
		case EReplicPinOptionKind::NameProperty:
			return CastField<FNameProperty>(Property) != nullptr;
		case EReplicPinOptionKind::StringProperty:
			return CastField<FStrProperty>(Property) != nullptr;
		case EReplicPinOptionKind::TextProperty:
			return CastField<FTextProperty>(Property) != nullptr;
		case EReplicPinOptionKind::VectorProperty:
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				return StructProperty->Struct == TBaseStructure<FVector>::Get();
			}
			return false;
		case EReplicPinOptionKind::RotatorProperty:
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				return StructProperty->Struct == TBaseStructure<FRotator>::Get();
			}
			return false;
		case EReplicPinOptionKind::TransformProperty:
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				return StructProperty->Struct == TBaseStructure<FTransform>::Get();
			}
			return false;
		case EReplicPinOptionKind::ObjectProperty:
			return CastField<FObjectPropertyBase>(Property) != nullptr && CastField<FClassProperty>(Property) == nullptr;
		case EReplicPinOptionKind::ClassProperty:
			return CastField<FClassProperty>(Property) != nullptr;
		case EReplicPinOptionKind::StructProperty:
			if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
			{
				return StructProperty->Struct != TBaseStructure<FVector>::Get()
					&& StructProperty->Struct != TBaseStructure<FRotator>::Get()
					&& StructProperty->Struct != TBaseStructure<FTransform>::Get();
			}
			return false;
		case EReplicPinOptionKind::ArrayProperty:
			return CastField<FArrayProperty>(Property) != nullptr;
		case EReplicPinOptionKind::SetProperty:
			return CastField<FSetProperty>(Property) != nullptr;
		case EReplicPinOptionKind::MapProperty:
			return CastField<FMapProperty>(Property) != nullptr;
		default:
			return false;
		}
	}

	bool IsReferencePropertyKind(EReplicPinOptionKind Kind)
	{
		return Kind == EReplicPinOptionKind::ObjectProperty || Kind == EReplicPinOptionKind::ClassProperty;
	}

	UClass* GetReferenceClassForProperty(const FProperty* Property, EReplicPinOptionKind Kind)
	{
		if (!Property)
		{
			return nullptr;
		}

		if (Kind == EReplicPinOptionKind::ObjectProperty)
		{
			if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
			{
				return ObjectProperty->PropertyClass;
			}
		}
		else if (Kind == EReplicPinOptionKind::ClassProperty)
		{
			if (const FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
			{
				return ClassProperty->MetaClass;
			}
		}

		return nullptr;
	}

	bool IsMeaningfulReferenceClass(UClass* InClass)
	{
		return InClass && InClass != UObject::StaticClass() && InClass != UClass::StaticClass();
	}

	bool DoesPropertyAcceptReferenceClass(const FProperty* Property, EReplicPinOptionKind Kind, UClass* ValueClass)
	{
		if (!Property || !IsReferencePropertyKind(Kind) || !IsMeaningfulReferenceClass(ValueClass))
		{
			return true;
		}

		if (UClass* PropertyClass = GetReferenceClassForProperty(Property, Kind))
		{
			return ValueClass->IsChildOf(PropertyClass);
		}

		return false;
	}

	FString GetReadablePropertyTypeName(const FProperty* Property)
	{
		if (!Property)
		{
			return TEXT("Unknown");
		}

		if (const FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
		{
			return FString::Printf(TEXT("Array of %s"), *GetReadablePropertyTypeName(ArrayProperty->Inner));
		}

		if (const FSetProperty* SetProperty = CastField<FSetProperty>(Property))
		{
			return FString::Printf(TEXT("Set of %s"), *GetReadablePropertyTypeName(SetProperty->ElementProp));
		}

		if (const FMapProperty* MapProperty = CastField<FMapProperty>(Property))
		{
			return FString::Printf(TEXT("Map of %s -> %s"), *GetReadablePropertyTypeName(MapProperty->KeyProp), *GetReadablePropertyTypeName(MapProperty->ValueProp));
		}

		if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
		{
			return EnumProperty->GetEnum() ? EnumProperty->GetEnum()->GetName() : TEXT("Enum");
		}

		if (const FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
		{
			if (ByteProperty->Enum)
			{
				return ByteProperty->Enum->GetName();
			}
		}

		if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
		{
			return StructProperty->Struct ? StructProperty->Struct->GetName() : TEXT("Struct");
		}

		if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
		{
			return ObjectProperty->PropertyClass ? ObjectProperty->PropertyClass->GetName() : TEXT("Object");
		}

		return Property->GetCPPType();
	}

	FString GetOriginNameForClass(UClass* SourceClass)
	{
		if (UBlueprint* Blueprint = SourceClass ? Cast<UBlueprint>(SourceClass->ClassGeneratedBy) : nullptr)
		{
			return Blueprint->GetName();
		}

		return SourceClass ? SourceClass->GetName() : FString(TEXT("Unknown"));
	}

	FString GetOriginPathForClass(UClass* SourceClass)
	{
		if (UBlueprint* Blueprint = SourceClass ? Cast<UBlueprint>(SourceClass->ClassGeneratedBy) : nullptr)
		{
			return Blueprint->GetPathName();
		}

		return SourceClass ? SourceClass->GetPathName() : FString();
	}

	bool IsCurrentBlueprintVariableProperty(const FProperty* Property)
	{
		if (!Property)
		{
			return false;
		}

		UClass* OwnerClass = Property->GetOwnerClass();
		UBlueprint* OwnerBlueprint = OwnerClass ? Cast<UBlueprint>(OwnerClass->ClassGeneratedBy) : nullptr;
		if (!OwnerBlueprint)
		{
			return true;
		}

		UBlueprint* FoundBlueprint = nullptr;
		return FBlueprintEditorUtils::FindNewVariableIndexAndBlueprint(OwnerBlueprint, Property->GetFName(), FoundBlueprint) != INDEX_NONE;
	}

	bool IsCurrentBlueprintEventFunction(const UFunction* Function)
	{
		if (!Function)
		{
			return false;
		}

		UClass* OwnerClass = Function->GetOwnerClass();
		UBlueprint* OwnerBlueprint = OwnerClass ? Cast<UBlueprint>(OwnerClass->ClassGeneratedBy) : nullptr;
		if (!OwnerBlueprint)
		{
			return true;
		}

		TArray<UK2Node_CustomEvent*> CustomEventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(OwnerBlueprint, CustomEventNodes);
		for (const UK2Node_CustomEvent* EventNode : CustomEventNodes)
		{
			if (!EventNode)
			{
				continue;
			}

			if (EventNode->GetFunctionName() == Function->GetFName() || EventNode->CustomFunctionName == Function->GetFName())
			{
				return true;
			}
		}

		return false;
	}

	UClass* ResolveClassFromPinType(const UEdGraphPin* Pin)
	{
		if (!Pin)
		{
			return nullptr;
		}

		if (Pin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self || Pin->PinName == UEdGraphSchema_K2::PN_Self)
		{
			if (const UK2Node* K2Node = Cast<UK2Node>(Pin->GetOwningNode()))
			{
				if (const UBlueprint* Blueprint = K2Node->GetBlueprint())
				{
					if (Blueprint->SkeletonGeneratedClass)
					{
						return Blueprint->SkeletonGeneratedClass.Get();
					}

					if (Blueprint->GeneratedClass)
					{
						return Blueprint->GeneratedClass.Get();
					}
				}
			}

			if (const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(Pin->GetOwningNode()))
			{
				if (Blueprint->SkeletonGeneratedClass)
				{
					return Blueprint->SkeletonGeneratedClass.Get();
				}

				if (Blueprint->GeneratedClass)
				{
					return Blueprint->GeneratedClass.Get();
				}
			}
		}

		return Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
	}

	UClass* ResolveTargetClassFromPin(const UEdGraphPin* Pin, TSet<const UEdGraphPin*>& VisitedPins);
	UClass* ResolveClassFromCallFunctionNode(const UK2Node_CallFunction* CallFunctionNode, TSet<const UEdGraphPin*>& VisitedPins);

	UClass* ResolveReferenceClassFromValuePin(const UEdGraphPin* Pin, EReplicPinOptionKind Kind, TSet<const UEdGraphPin*>& VisitedPins)
	{
		if (!Pin || !IsReferencePropertyKind(Kind) || VisitedPins.Contains(Pin))
		{
			return nullptr;
		}

		VisitedPins.Add(Pin);

		if (Kind == EReplicPinOptionKind::ObjectProperty)
		{
			if (UObject* DefaultObject = Pin->DefaultObject)
			{
				return DefaultObject->GetClass();
			}
		}
		else if (Kind == EReplicPinOptionKind::ClassProperty)
		{
			if (UClass* DefaultClass = Cast<UClass>(Pin->DefaultObject))
			{
				return DefaultClass;
			}

			if (!Pin->DefaultValue.IsEmpty())
			{
				FSoftClassPath SoftClassPath(Pin->DefaultValue);
				if (UClass* ResolvedClass = SoftClassPath.ResolveClass())
				{
					return ResolvedClass;
				}

				if (UClass* LoadedClass = SoftClassPath.TryLoadClass<UObject>())
				{
					return LoadedClass;
				}

				if (UClass* FoundClass = FindObject<UClass>(nullptr, *Pin->DefaultValue))
				{
					return FoundClass;
				}
			}
		}

		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(LinkedPin ? LinkedPin->GetOwningNode() : nullptr))
			{
				if (UClass* DynamicClass = ResolveClassFromCallFunctionNode(CallFunctionNode, VisitedPins))
				{
					return DynamicClass;
				}
			}

			if (UClass* LinkedClass = ResolveReferenceClassFromValuePin(LinkedPin, Kind, VisitedPins))
			{
				return LinkedClass;
			}
		}

		if (UClass* DirectClass = ResolveClassFromPinType(Pin))
		{
			if (IsMeaningfulReferenceClass(DirectClass))
			{
				return DirectClass;
			}

			return nullptr;
		}

		return nullptr;
	}

	UClass* ResolveClassFromCallFunctionNode(const UK2Node_CallFunction* CallFunctionNode, TSet<const UEdGraphPin*>& VisitedPins)
	{
		if (!CallFunctionNode)
		{
			return nullptr;
		}

		const UFunction* Function = CallFunctionNode->GetTargetFunction();
		if (!Function || !Function->HasMetaData(FBlueprintMetadata::MD_DynamicOutputType))
		{
			return nullptr;
		}

		const FString TypePinName = Function->GetMetaData(FBlueprintMetadata::MD_DynamicOutputType);
		if (TypePinName.IsEmpty())
		{
			return nullptr;
		}

		if (const UEdGraphPin* TypePin = CallFunctionNode->FindPin(FName(*TypePinName)))
		{
			if (UClass* TypeClass = Cast<UClass>(TypePin->DefaultObject))
			{
				return TypeClass;
			}

			if (!TypePin->DefaultValue.IsEmpty())
			{
				FSoftClassPath SoftClassPath(TypePin->DefaultValue);
				if (UClass* TypeClass = SoftClassPath.ResolveClass())
				{
					return TypeClass;
				}

				if (UClass* TypeClass = SoftClassPath.TryLoadClass<UObject>())
				{
					return TypeClass;
				}

				if (UClass* TypeClass = FindObject<UClass>(nullptr, *TypePin->DefaultValue))
				{
					return TypeClass;
				}

				if (UClass* TypeClass = LoadObject<UClass>(nullptr, *TypePin->DefaultValue))
				{
					return TypeClass;
				}
			}

			return ResolveTargetClassFromPin(TypePin, VisitedPins);
		}

		return nullptr;
	}

	UClass* ResolveTargetClassFromPin(const UEdGraphPin* Pin, TSet<const UEdGraphPin*>& VisitedPins)
	{
		if (!Pin || VisitedPins.Contains(Pin))
		{
			return nullptr;
		}

		VisitedPins.Add(Pin);

		if (const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Pin->GetOwningNode()))
		{
			if (UClass* DynamicClass = ResolveClassFromCallFunctionNode(CallFunctionNode, VisitedPins))
			{
				if (IsUsableReplicClass(DynamicClass))
				{
					return DynamicClass;
				}
			}
		}

		// Prefer the pin's own resolved type over any sibling links that also happen to touch the same wildcard pin.
		// This keeps Array Element and similar flow pins stable even when they fan out into multiple nodes.
		if (UClass* DirectClass = ResolveClassFromPinType(Pin))
		{
			if (IsUsableReplicClass(DirectClass))
			{
				return DirectClass;
			}
		}

		for (const UEdGraphPin* LinkedPin : Pin->LinkedTo)
		{
			if (UClass* LinkedClass = ResolveTargetClassFromPin(LinkedPin, VisitedPins))
			{
				if (IsUsableReplicClass(LinkedClass))
				{
					return LinkedClass;
				}
			}
		}

		return nullptr;
	}

	UClass* ResolveTargetClassForReplicNode(const UEdGraphPin* NamePin)
	{
		const UEdGraphNode* OwningNode = NamePin ? NamePin->GetOwningNode() : nullptr;
		if (!OwningNode)
		{
			return nullptr;
		}

		const UEdGraphPin* TargetObjectPin = OwningNode->FindPin(TEXT("TargetObject"));
		if (!TargetObjectPin)
		{
			return nullptr;
		}

		if (TargetObjectPin->LinkedTo.Num() == 0 && TargetObjectPin->DefaultObject == nullptr && TargetObjectPin->DefaultValue.IsEmpty())
		{
			// Unconnected TargetObject means "Self/current Blueprint" for Replic nodes.
			if (const UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForNode(OwningNode))
			{
				if (Blueprint->SkeletonGeneratedClass)
				{
					return Blueprint->SkeletonGeneratedClass.Get();
				}

				if (Blueprint->GeneratedClass)
				{
					return Blueprint->GeneratedClass.Get();
				}
			}
		}

		TSet<const UEdGraphPin*> VisitedPins;
		return ResolveTargetClassFromPin(TargetObjectPin, VisitedPins);
	}

	UClass* ResolveReferenceValueClassForReplicNode(const UEdGraphPin* NamePin, EReplicPinOptionKind Kind)
	{
		if (!NamePin || !IsReferencePropertyKind(Kind))
		{
			return nullptr;
		}

		const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(NamePin->GetOwningNode());
		const UFunction* TargetFunction = CallFunctionNode ? CallFunctionNode->GetTargetFunction() : nullptr;
		if (!TargetFunction || TargetFunction->GetOwnerClass() != UReplicLibrary::StaticClass())
		{
			return nullptr;
		}

		const FName FunctionName = TargetFunction->GetFName();
		const bool bIsObjectSetter = Kind == EReplicPinOptionKind::ObjectProperty
			&& FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject);
		const bool bIsClassSetter = Kind == EReplicPinOptionKind::ClassProperty
			&& FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass);
		if (!bIsObjectSetter && !bIsClassSetter)
		{
			return nullptr;
		}

		const UEdGraphPin* ValuePin = CallFunctionNode->FindPin(TEXT("Value"));
		if (!ValuePin)
		{
			return nullptr;
		}

		TSet<const UEdGraphPin*> VisitedPins;
		return ResolveReferenceClassFromValuePin(ValuePin, Kind, VisitedPins);
	}

	void CollectMarkedPropertiesFromClass(UClass* TargetClass, EReplicPinOptionKind Kind, TArray<FReplicCollectedOption>& OutOptions)
	{
		TArray<UClass*> SourceClasses;
		GatherMetadataSourceClasses(TargetClass, SourceClasses);

		for (UClass* SourceClass : SourceClasses)
		{
			// Read metadata from skeleton and generated classes so the dropdowns remain stable before and after Blueprint compile.
			for (TFieldIterator<FProperty> PropertyIt(SourceClass, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (!ReplicRuntimeUtils::IsMarkedVariable(Property) || !DoesPropertyMatchKind(Property, Kind) || !IsCurrentBlueprintVariableProperty(Property))
				{
					continue;
				}

				UClass* OwnerClass = Property->GetOwnerClass();
				if (!IsUsableReplicClass(OwnerClass))
				{
					continue;
				}

				FReplicCollectedOption& NewOption = OutOptions.AddDefaulted_GetRef();
				NewOption.Value = Property->GetFName();
				NewOption.OriginName = GetOriginNameForClass(OwnerClass);
				NewOption.OriginPath = GetOriginPathForClass(OwnerClass);
				NewOption.TypeName = GetReadablePropertyTypeName(Property);
				NewOption.SourceProperty = Property;
			}
		}
	}

	void CollectMarkedEventsFromClass(UClass* TargetClass, TArray<FReplicCollectedOption>& OutOptions)
	{
		TArray<UClass*> SourceClasses;
		GatherMetadataSourceClasses(TargetClass, SourceClasses);

		for (UClass* SourceClass : SourceClasses)
		{
			for (TFieldIterator<UFunction> FunctionIt(SourceClass, EFieldIterationFlags::IncludeSuper); FunctionIt; ++FunctionIt)
			{
				UFunction* Function = *FunctionIt;
				if (!ReplicRuntimeUtils::IsMarkedEvent(Function) || !IsCurrentBlueprintEventFunction(Function))
				{
					continue;
				}

				UClass* OwnerClass = Function->GetOwnerClass();
				if (!IsUsableReplicClass(OwnerClass))
				{
					continue;
				}

				FReplicCollectedOption& NewOption = OutOptions.AddDefaulted_GetRef();
				NewOption.Value = Function->GetFName();
				NewOption.OriginName = GetOriginNameForClass(OwnerClass);
				NewOption.OriginPath = GetOriginPathForClass(OwnerClass);
			}
		}
	}

	void CollectGlobalMarkedProperties(EReplicPinOptionKind Kind, TArray<FReplicCollectedOption>& OutOptions)
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* SourceClass = *ClassIt;
			if (!IsUsableReplicClass(SourceClass))
			{
				continue;
			}

			CollectMarkedPropertiesFromClass(SourceClass, Kind, OutOptions);
		}
	}

	void CollectGlobalMarkedEvents(TArray<FReplicCollectedOption>& OutOptions)
	{
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* SourceClass = *ClassIt;
			if (!IsUsableReplicClass(SourceClass))
			{
				continue;
			}

			CollectMarkedEventsFromClass(SourceClass, OutOptions);
		}
	}

	FText BuildTooltipText(const FReplicCollectedOption& Option, bool bHasConcreteTargetClass)
	{
		FString Tooltip = FString::Printf(TEXT("Name: %s\n"), *Option.Value.ToString());
		Tooltip += FString::Printf(TEXT("Declared In: %s"), *Option.OriginName);

		if (!Option.TypeName.IsEmpty())
		{
			Tooltip += FString::Printf(TEXT("\nType: %s"), *Option.TypeName);
		}

		if (!Option.OriginPath.IsEmpty())
		{
			Tooltip += FString::Printf(TEXT("\nAsset: %s"), *Option.OriginPath);
		}

		if (!bHasConcreteTargetClass)
		{
			Tooltip += TEXT("\n\nTarget class is not fully resolved, so this list may contain entries from multiple Blueprints or classes.");
		}

		return FText::FromString(Tooltip);
	}

	FName GetFunctionName(const UEdGraphPin* Pin)
	{
		if (const UK2Node_CallFunction* CallFunctionNode = Pin ? Cast<UK2Node_CallFunction>(Pin->GetOwningNode()) : nullptr)
		{
			if (const UFunction* TargetFunction = CallFunctionNode->GetTargetFunction())
			{
				return TargetFunction->GetFName();
			}
		}

		return NAME_None;
	}

	bool FindMarkedPropertyOnClass(UClass* TargetClass, FName PropertyName, const FProperty*& OutProperty)
	{
		OutProperty = nullptr;

		TArray<UClass*> SourceClasses;
		GatherMetadataSourceClasses(TargetClass, SourceClasses);

		for (UClass* SourceClass : SourceClasses)
		{
			for (TFieldIterator<FProperty> PropertyIt(SourceClass, EFieldIterationFlags::IncludeSuper); PropertyIt; ++PropertyIt)
			{
				FProperty* Property = *PropertyIt;
				if (Property && Property->GetFName() == PropertyName && ReplicRuntimeUtils::IsMarkedVariable(Property))
				{
					OutProperty = Property;
					return true;
				}
			}
		}

		return false;
	}

	bool FindMarkedEventOnClass(UClass* TargetClass, FName EventName, UFunction*& OutFunction)
	{
		OutFunction = nullptr;

		TArray<UClass*> SourceClasses;
		GatherMetadataSourceClasses(TargetClass, SourceClasses);

		for (UClass* SourceClass : SourceClasses)
		{
			for (TFieldIterator<UFunction> FunctionIt(SourceClass, EFieldIterationFlags::IncludeSuper); FunctionIt; ++FunctionIt)
			{
				UFunction* Function = *FunctionIt;
				if (Function && Function->GetFName() == EventName && ReplicRuntimeUtils::IsMarkedEvent(Function))
				{
					OutFunction = Function;
					return true;
				}
			}
		}

		return false;
	}

	bool FindMarkedEventNodeOnClass(UClass* TargetClass, FName EventName, UK2Node_CustomEvent*& OutEventNode)
	{
		OutEventNode = nullptr;

		if (!TargetClass)
		{
			return false;
		}

		UBlueprint* Blueprint = Cast<UBlueprint>(TargetClass->ClassGeneratedBy);
		if (!Blueprint)
		{
			return false;
		}

		TArray<UK2Node_CustomEvent*> CustomEventNodes;
		FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, CustomEventNodes);

		for (UK2Node_CustomEvent* EventNode : CustomEventNodes)
		{
			if (!EventNode)
			{
				continue;
			}

			const bool bNameMatches = EventNode->CustomFunctionName == EventName || EventNode->GetFunctionName() == EventName;
			if (!bNameMatches)
			{
				continue;
			}

			OutEventNode = EventNode;
			return true;
		}

		return false;
	}
}

bool ReplicPinOptionResolver::IsSupportedReplicNamePin(const UEdGraphPin* Pin, EReplicPinOptionKind& OutKind)
{
	OutKind = EReplicPinOptionKind::None;

	if (!Pin || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Name)
	{
		return false;
	}

	if (const UK2Node_ReplicCallEvent* ReplicEventNode = Cast<UK2Node_ReplicCallEvent>(Pin->GetOwningNode()))
	{
		if (Pin->PinName == ReplicEventNode->GetEventNamePinName())
		{
			OutKind = EReplicPinOptionKind::Event;
		}

		return OutKind != EReplicPinOptionKind::None;
	}

	if (const UK2Node_ReplicSetEnum* ReplicSetEnumNode = Cast<UK2Node_ReplicSetEnum>(Pin->GetOwningNode()))
	{
		if (Pin->PinName == ReplicSetEnumNode->GetPropertyNamePinName())
		{
			OutKind = EReplicPinOptionKind::EnumProperty;
		}

		return OutKind != EReplicPinOptionKind::None;
	}

	if (const UK2Node_ReplicSetArray* ReplicSetArrayNode = Cast<UK2Node_ReplicSetArray>(Pin->GetOwningNode()))
	{
		if (Pin->PinName == ReplicSetArrayNode->GetPropertyNamePinName())
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}

		return OutKind != EReplicPinOptionKind::None;
	}

	if (const UK2Node_ReplicGetEnum* ReplicGetEnumNode = Cast<UK2Node_ReplicGetEnum>(Pin->GetOwningNode()))
	{
		if (Pin->PinName == ReplicGetEnumNode->GetPropertyNamePinName())
		{
			OutKind = EReplicPinOptionKind::EnumProperty;
		}

		return OutKind != EReplicPinOptionKind::None;
	}

	if (const UK2Node_ReplicGetArray* ReplicGetArrayNode = Cast<UK2Node_ReplicGetArray>(Pin->GetOwningNode()))
	{
		if (Pin->PinName == ReplicGetArrayNode->GetPropertyNamePinName())
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}

		return OutKind != EReplicPinOptionKind::None;
	}

	const UK2Node_CallFunction* CallFunctionNode = Cast<UK2Node_CallFunction>(Pin->GetOwningNode());
	const UFunction* TargetFunction = CallFunctionNode ? CallFunctionNode->GetTargetFunction() : nullptr;
	if (!TargetFunction || TargetFunction->GetOwnerClass() != UReplicLibrary::StaticClass())
	{
		return false;
	}

	if (Pin->PinName == TEXT("PropertyName"))
	{
		const FName FunctionName = TargetFunction->GetFName();
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, BindMarkedPropertyChanged))
		{
			OutKind = EReplicPinOptionKind::AnyProperty;
		}
		if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedBool))
		{
			OutKind = EReplicPinOptionKind::BoolProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedBool))
		{
			OutKind = EReplicPinOptionKind::BoolProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedInt))
		{
			OutKind = EReplicPinOptionKind::IntProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedInt))
		{
			OutKind = EReplicPinOptionKind::IntProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedFloat))
		{
			OutKind = EReplicPinOptionKind::FloatProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedFloat))
		{
			OutKind = EReplicPinOptionKind::FloatProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedByte))
		{
			OutKind = EReplicPinOptionKind::ByteProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedEnum))
		{
			OutKind = EReplicPinOptionKind::EnumProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedByte))
		{
			OutKind = EReplicPinOptionKind::ByteProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedEnum))
		{
			OutKind = EReplicPinOptionKind::EnumProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedName))
		{
			OutKind = EReplicPinOptionKind::NameProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedName))
		{
			OutKind = EReplicPinOptionKind::NameProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedString))
		{
			OutKind = EReplicPinOptionKind::StringProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedString))
		{
			OutKind = EReplicPinOptionKind::StringProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedText))
		{
			OutKind = EReplicPinOptionKind::TextProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedText))
		{
			OutKind = EReplicPinOptionKind::TextProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedVector))
		{
			OutKind = EReplicPinOptionKind::VectorProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedVector))
		{
			OutKind = EReplicPinOptionKind::VectorProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedRotator))
		{
			OutKind = EReplicPinOptionKind::RotatorProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedRotator))
		{
			OutKind = EReplicPinOptionKind::RotatorProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedTransform))
		{
			OutKind = EReplicPinOptionKind::TransformProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedTransform))
		{
			OutKind = EReplicPinOptionKind::TransformProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject))
		{
			OutKind = EReplicPinOptionKind::ObjectProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedObject))
		{
			OutKind = EReplicPinOptionKind::ObjectProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedClass))
		{
			OutKind = EReplicPinOptionKind::ClassProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedClass))
		{
			OutKind = EReplicPinOptionKind::ClassProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedStruct))
		{
			OutKind = EReplicPinOptionKind::StructProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedStruct))
		{
			OutKind = EReplicPinOptionKind::StructProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray))
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedArray))
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray))
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedArray))
		{
			OutKind = EReplicPinOptionKind::ArrayProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet))
		{
			OutKind = EReplicPinOptionKind::SetProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedSet))
		{
			OutKind = EReplicPinOptionKind::SetProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet))
		{
			OutKind = EReplicPinOptionKind::SetProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedSet))
		{
			OutKind = EReplicPinOptionKind::SetProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap))
		{
			OutKind = EReplicPinOptionKind::MapProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, GetMarkedMap))
		{
			OutKind = EReplicPinOptionKind::MapProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap))
		{
			OutKind = EReplicPinOptionKind::MapProperty;
		}
		else if (FunctionName == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap))
		{
			OutKind = EReplicPinOptionKind::MapProperty;
		}
	}
	else if (Pin->PinName == TEXT("EventName") && TargetFunction->GetFName() == GET_FUNCTION_NAME_CHECKED(UReplicLibrary, CallMarkedEvent))
	{
		OutKind = EReplicPinOptionKind::Event;
	}

	return OutKind != EReplicPinOptionKind::None;
}

bool ReplicPinOptionResolver::ResolveTargetClass(const UEdGraphPin* Pin, UClass*& OutTargetClass)
{
	OutTargetClass = nullptr;

	if (!Pin)
	{
		return false;
	}

	UClass* TargetClass = ResolveTargetClassForReplicNode(Pin);
	if (!IsUsableReplicClass(TargetClass))
	{
		return false;
	}

	OutTargetClass = TargetClass;
	return true;
}

bool ReplicPinOptionResolver::ResolveReferenceValueClass(const UEdGraphPin* Pin, UClass*& OutValueClass)
{
	OutValueClass = nullptr;

	EReplicPinOptionKind Kind = EReplicPinOptionKind::None;
	if (!IsSupportedReplicNamePin(Pin, Kind) || !IsReferencePropertyKind(Kind))
	{
		return false;
	}

	UClass* ValueClass = ResolveReferenceValueClassForReplicNode(Pin, Kind);
	if (!ValueClass)
	{
		return false;
	}

	OutValueClass = ValueClass;
	return true;
}

bool ReplicPinOptionResolver::ResolveMarkedProperty(const UEdGraphPin* Pin, FName PropertyName, const FProperty*& OutProperty)
{
	OutProperty = nullptr;

	if (PropertyName.IsNone())
	{
		return false;
	}

	UClass* TargetClass = nullptr;
	return ResolveTargetClass(Pin, TargetClass) && FindMarkedPropertyOnClass(TargetClass, PropertyName, OutProperty);
}

bool ReplicPinOptionResolver::ResolveMarkedEvent(const UEdGraphPin* Pin, FName EventName, UFunction*& OutFunction)
{
	OutFunction = nullptr;

	if (EventName.IsNone())
	{
		return false;
	}

	UClass* TargetClass = nullptr;
	return ResolveTargetClass(Pin, TargetClass) && FindMarkedEventOnClass(TargetClass, EventName, OutFunction);
}

bool ReplicPinOptionResolver::ResolveMarkedEventNode(const UEdGraphPin* Pin, FName EventName, UK2Node_CustomEvent*& OutEventNode)
{
	OutEventNode = nullptr;

	if (EventName.IsNone())
	{
		return false;
	}

	UClass* TargetClass = nullptr;
	return ResolveTargetClass(Pin, TargetClass) && FindMarkedEventNodeOnClass(TargetClass, EventName, OutEventNode);
}

void ReplicPinOptionResolver::BuildOptions(const UEdGraphPin* Pin, TArray<TSharedPtr<FReplicPinOptionItem>>& OutOptions)
{
	OutOptions.Reset();

	EReplicPinOptionKind Kind = EReplicPinOptionKind::None;
	if (!IsSupportedReplicNamePin(Pin, Kind))
	{
		return;
	}

	TArray<FReplicCollectedOption> RawOptions;
	UClass* TargetClass = ResolveTargetClassForReplicNode(Pin);
	const bool bHasConcreteTargetClass = IsUsableReplicClass(TargetClass);

	if (bHasConcreteTargetClass)
	{
		if (Kind == EReplicPinOptionKind::Event)
		{
			CollectMarkedEventsFromClass(TargetClass, RawOptions);
		}
		else
		{
			CollectMarkedPropertiesFromClass(TargetClass, Kind, RawOptions);
		}
	}

	if (IsReferencePropertyKind(Kind))
	{
		// Reference-typed setters can narrow PropertyName choices further once the connected Value pin already implies a more
		// concrete object/class type.
		UClass* ValueClass = ResolveReferenceValueClassForReplicNode(Pin, Kind);
		if (IsMeaningfulReferenceClass(ValueClass))
		{
			RawOptions.RemoveAll([Kind, ValueClass](const FReplicCollectedOption& Option)
			{
				return !DoesPropertyAcceptReferenceClass(Option.SourceProperty, Kind, ValueClass);
			});
		}
	}

	TMap<FName, int32> NameCounts;
	TSet<FString> SeenKeys;
	TArray<FReplicCollectedOption> UniqueRawOptions;
	for (const FReplicCollectedOption& Option : RawOptions)
	{
		const FString UniqueKey = FString::Printf(TEXT("%s|%s"), *Option.Value.ToString(), *Option.OriginPath);
		if (!SeenKeys.Contains(UniqueKey))
		{
			SeenKeys.Add(UniqueKey);
			UniqueRawOptions.Add(Option);
			NameCounts.FindOrAdd(Option.Value)++;
		}
	}

	for (const FReplicCollectedOption& Option : UniqueRawOptions)
	{
		// Only include the origin name in the visible label when the target class is ambiguous or the same property name exists
		// in multiple metadata sources. Otherwise the dropdown stays compact.
		const bool bNeedsOriginInDisplay = !bHasConcreteTargetClass || NameCounts.FindRef(Option.Value) > 1;
		TSharedPtr<FReplicPinOptionItem> NewItem = MakeShared<FReplicPinOptionItem>();
		NewItem->Value = Option.Value;
		NewItem->DisplayText = bNeedsOriginInDisplay
			? FText::FromString(FString::Printf(TEXT("%s (%s)"), *Option.Value.ToString(), *Option.OriginName))
			: FText::FromName(Option.Value);
		NewItem->TooltipText = BuildTooltipText(Option, bHasConcreteTargetClass);
		NewItem->SortKey = FString::Printf(TEXT("%s|%s"), *Option.Value.ToString(), *Option.OriginName);
		OutOptions.Add(NewItem);
	}

	OutOptions.Sort([](const TSharedPtr<FReplicPinOptionItem>& A, const TSharedPtr<FReplicPinOptionItem>& B)
	{
		return A.IsValid() && B.IsValid() ? A->SortKey < B->SortKey : A.IsValid();
	});
}
