#include "Misc/AutomationTest.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS

#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicRuntimeUtils.h"
#include "ReplicSettings.h"
#include "ReplicTypes.h"
#include "Tests/ReplicAutomationTestObject.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "ReplicTransportComponent.h"
#include "UObject/Package.h"

namespace
{
	UReplicAutomationTestObject* NewReplicAutomationObject()
	{
		return NewObject<UReplicAutomationTestObject>(GetTransientPackage());
	}

	void ConfigureReplicTestMetadata()
	{
		UClass* TestClass = UReplicAutomationTestObject::StaticClass();
		if (!TestClass)
		{
			return;
		}

		if (FProperty* MarkedIntProperty = FindFProperty<FProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedInt)))
		{
			MarkedIntProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			MarkedIntProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("false"));
			MarkedIntProperty->SetMetaData(ReplicMetadata::VariableBatching, TEXT("true"));
			MarkedIntProperty->SetMetaData(ReplicMetadata::VariableBatchInterval, TEXT("0.25"));
			MarkedIntProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("OwnerOnly"));
		}

		const FName MarkedPropertyNames[] =
		{
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedEnum),
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedObject),
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedClass),
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedSet),
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedMap),
			GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedStruct)
		};

		for (const FName PropertyName : MarkedPropertyNames)
		{
			if (FProperty* Property = FindFProperty<FProperty>(TestClass, PropertyName))
			{
				Property->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			}
		}

		if (FProperty* MarkedArrayProperty = FindFProperty<FProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedArray)))
		{
			MarkedArrayProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			MarkedArrayProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("Custom"));
		}

		if (UFunction* MarkedEventFunction = TestClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicAutomationTestObject, MarkedEvent)))
		{
			MarkedEventFunction->SetMetaData(ReplicMetadata::EventEnabled, TEXT("true"));
			MarkedEventFunction->SetMetaData(ReplicMetadata::EventPermissionMode, TEXT("Custom"));
			MarkedEventFunction->SetMetaData(ReplicMetadata::EventMode, TEXT("OwnerOnly"));
		}
	}

	template<typename PropertyType>
	PropertyType* FindPropertyChecked(const UClass* OwnerClass, const FName PropertyName)
	{
		return FindFProperty<PropertyType>(OwnerClass, PropertyName);
	}

	FString ExportPropertyChecked(FAutomationTestBase& Test, UObject* Object, const FProperty* Property)
	{
		FString TextValue;
		Test.TestNotNull(TEXT("Export property exists"), Property);
		Test.TestTrue(TEXT("Export property to text"), ReplicRuntimeUtils::ExportObjectPropertyToText(Object, Property, TextValue));
		return TextValue;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicDiagnosticsSettingsAutomationTest,
	"Replic.Runtime.Diagnostics.SettingsDefaults",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicDiagnosticsSettingsAutomationTest::RunTest(const FString& Parameters)
{
	const UReplicSettings* Settings = GetDefault<UReplicSettings>();
	TestNotNull(TEXT("Replic settings exist"), Settings);
	if (!Settings)
	{
		return false;
	}

	TestFalse(TEXT("Runtime debug logs are quiet by default"), Settings->bEnableRuntimeDebugLogs);
	TestFalse(TEXT("Verbose routine logs are quiet by default"), Settings->bEnableVerboseRuntimeLogs);
	TestFalse(TEXT("Screen debug messages are quiet by default"), Settings->bEnableScreenDebugMessages);
	TestTrue(TEXT("Write diagnostics channel is ready when logging is enabled"), Settings->bEnableWriteDebugLogs);
	TestTrue(TEXT("Event diagnostics channel is ready when logging is enabled"), Settings->bEnableEventDebugLogs);
	TestTrue(TEXT("State diagnostics channel is ready when logging is enabled"), Settings->bEnableStateDebugLogs);
	TestTrue(TEXT("Permission diagnostics channel is ready when logging is enabled"), Settings->bEnableDetailedPermissionLogs);
	TestFalse(TEXT("Observer diagnostics remain opt-in"), Settings->bEnableObserverDebugLogs);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicMetadataAutomationTest,
	"Replic.Runtime.Metadata.Settings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicMetadataAutomationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicTestMetadata();

	const UClass* TestClass = UReplicAutomationTestObject::StaticClass();
	TestNotNull(TEXT("Test class exists"), TestClass);

	const FProperty* MarkedIntProperty = FindPropertyChecked<FProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedInt));
	const FReplicVariableSettings IntSettings = ReplicRuntimeUtils::GetVariableSettings(MarkedIntProperty);
	TestTrue(TEXT("MarkedInt is Replic-enabled"), IntSettings.bReplicateAll);
	TestFalse(TEXT("MarkedInt persistent state is disabled"), IntSettings.bPersistentState);
	TestTrue(TEXT("MarkedInt batching is enabled"), IntSettings.bUseBatching);
	TestEqual(TEXT("MarkedInt batch interval"), IntSettings.BatchIntervalSeconds, 0.25f);
	TestEqual(TEXT("MarkedInt permission mode"), IntSettings.PermissionMode, EReplicPermissionMode::OwnerOnly);

	const FProperty* MarkedArrayProperty = FindPropertyChecked<FProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedArray));
	const FReplicVariableSettings ArraySettings = ReplicRuntimeUtils::GetVariableSettings(MarkedArrayProperty);
	TestTrue(TEXT("MarkedArray is Replic-enabled"), ArraySettings.bReplicateAll);
	TestEqual(TEXT("MarkedArray permission mode"), ArraySettings.PermissionMode, EReplicPermissionMode::Custom);

	UFunction* MarkedEventFunction = TestClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicAutomationTestObject, MarkedEvent));
	TestNotNull(TEXT("MarkedEvent exists"), MarkedEventFunction);
	const FReplicEventSettings EventSettings = ReplicRuntimeUtils::GetEventSettings(MarkedEventFunction);
	TestTrue(TEXT("MarkedEvent is Replic-enabled"), EventSettings.bReplicateAll);
	TestEqual(TEXT("MarkedEvent permission mode"), EventSettings.PermissionMode, EReplicPermissionMode::Custom);
	TestEqual(TEXT("MarkedEvent dispatch mode"), EventSettings.Mode, EReplicEventMode::OwnerOnly);

	FReplicTargetDescriptor ActorDescriptor;
	ActorDescriptor.Kind = EReplicTargetKind::Actor;

	FReplicTargetDescriptor ComponentDescriptor;
	ComponentDescriptor.Kind = EReplicTargetKind::ActorComponent;
	ComponentDescriptor.ObjectName = TEXT("ReplicComponent");

	TestNotEqual(TEXT("Different target descriptors produce different keys"), ActorDescriptor.ToKey(), ComponentDescriptor.ToKey());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicSerializationAutomationTest,
	"Replic.Runtime.Serialization.RoundTrip",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicSerializationAutomationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicTestMetadata();

	UReplicAutomationTestObject* SourceObject = NewReplicAutomationObject();
	UReplicAutomationTestObject* DestinationObject = NewReplicAutomationObject();
	TestNotNull(TEXT("Source object exists"), SourceObject);
	TestNotNull(TEXT("Destination object exists"), DestinationObject);

	UReplicAutomationHelperObject* HelperObject = NewObject<UReplicAutomationHelperObject>(GetTransientPackage());
	TestNotNull(TEXT("Helper object exists"), HelperObject);

	SourceObject->MarkedArray = { 1, 2, 3 };
	SourceObject->MarkedSet = { TEXT("Door_A"), TEXT("Door_B") };
	SourceObject->MarkedMap.Add(TEXT("Potion"), 3);
	SourceObject->MarkedMap.Add(TEXT("Gem"), 5);
	SourceObject->MarkedStruct.ItemName = TEXT("Potion");
	SourceObject->MarkedStruct.Amount = 3;
	SourceObject->MarkedStruct.Rarity = EReplicAutomationEnum::Rare;
	SourceObject->MarkedStruct.PreviewClass = AActor::StaticClass();
	SourceObject->MarkedObject = HelperObject;
	SourceObject->MarkedClass = AActor::StaticClass();

	const UClass* TestClass = UReplicAutomationTestObject::StaticClass();
	const FArrayProperty* MarkedArrayProperty = FindPropertyChecked<FArrayProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedArray));
	const FArrayProperty* ScratchArrayProperty = FindPropertyChecked<FArrayProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchArray));
	TestTrue(
		TEXT("Copy array by serialized text"),
		ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(
			MarkedArrayProperty,
			MarkedArrayProperty->ContainerPtrToValuePtr<void>(SourceObject),
			ScratchArrayProperty,
			ScratchArrayProperty->ContainerPtrToValuePtr<void>(DestinationObject)));
	TestEqual(TEXT("ScratchArray size"), DestinationObject->ScratchArray.Num(), 3);
	TestEqual(TEXT("ScratchArray[0]"), DestinationObject->ScratchArray[0], 1);
	TestEqual(TEXT("ScratchArray[2]"), DestinationObject->ScratchArray[2], 3);

	const FSetProperty* MarkedSetProperty = FindPropertyChecked<FSetProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedSet));
	const FSetProperty* ScratchSetProperty = FindPropertyChecked<FSetProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchSet));
	TestTrue(
		TEXT("Copy set by serialized text"),
		ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(
			MarkedSetProperty,
			MarkedSetProperty->ContainerPtrToValuePtr<void>(SourceObject),
			ScratchSetProperty,
			ScratchSetProperty->ContainerPtrToValuePtr<void>(DestinationObject)));
	TestEqual(TEXT("ScratchSet size"), DestinationObject->ScratchSet.Num(), 2);
	TestTrue(TEXT("ScratchSet contains Door_A"), DestinationObject->ScratchSet.Contains(TEXT("Door_A")));
	TestTrue(TEXT("ScratchSet contains Door_B"), DestinationObject->ScratchSet.Contains(TEXT("Door_B")));

	const FMapProperty* MarkedMapProperty = FindPropertyChecked<FMapProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedMap));
	const FMapProperty* ScratchMapProperty = FindPropertyChecked<FMapProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchMap));
	TestTrue(
		TEXT("Copy map by serialized text"),
		ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(
			MarkedMapProperty,
			MarkedMapProperty->ContainerPtrToValuePtr<void>(SourceObject),
			ScratchMapProperty,
			ScratchMapProperty->ContainerPtrToValuePtr<void>(DestinationObject)));
	TestEqual(TEXT("ScratchMap size"), DestinationObject->ScratchMap.Num(), 2);
	TestEqual(TEXT("ScratchMap Potion"), DestinationObject->ScratchMap.FindRef(TEXT("Potion")), 3);
	TestEqual(TEXT("ScratchMap Gem"), DestinationObject->ScratchMap.FindRef(TEXT("Gem")), 5);

	const FStructProperty* MarkedStructProperty = FindPropertyChecked<FStructProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedStruct));
	const FStructProperty* ScratchStructProperty = FindPropertyChecked<FStructProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchStruct));
	TestTrue(
		TEXT("Copy struct by serialized text"),
		ReplicRuntimeUtils::CopyValueBetweenPropertiesByText(
			MarkedStructProperty,
			MarkedStructProperty->ContainerPtrToValuePtr<void>(SourceObject),
			ScratchStructProperty,
			ScratchStructProperty->ContainerPtrToValuePtr<void>(DestinationObject)));
	TestEqual(TEXT("ScratchStruct item name"), DestinationObject->ScratchStruct.ItemName, FName(TEXT("Potion")));
	TestEqual(TEXT("ScratchStruct amount"), DestinationObject->ScratchStruct.Amount, 3);
	TestEqual(TEXT("ScratchStruct rarity"), DestinationObject->ScratchStruct.Rarity, EReplicAutomationEnum::Rare);
	TestEqual(TEXT("ScratchStruct preview class"), DestinationObject->ScratchStruct.PreviewClass.Get(), AActor::StaticClass());

	const FObjectPropertyBase* MarkedObjectProperty = FindPropertyChecked<FObjectPropertyBase>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedObject));
	const FObjectPropertyBase* ScratchObjectProperty = FindPropertyChecked<FObjectPropertyBase>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchObject));
	const FString MarkedObjectText = ExportPropertyChecked(*this, SourceObject, MarkedObjectProperty);
	TestTrue(TEXT("Import object by serialized text"), ReplicRuntimeUtils::ImportObjectPropertyFromText(DestinationObject, const_cast<FObjectPropertyBase*>(ScratchObjectProperty), MarkedObjectText));
	TestTrue(TEXT("ScratchObject round-trip"), DestinationObject->ScratchObject.Get() == HelperObject);

	const FClassProperty* MarkedClassProperty = FindPropertyChecked<FClassProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedClass));
	const FClassProperty* ScratchClassProperty = FindPropertyChecked<FClassProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, ScratchClass));
	const FString MarkedClassText = ExportPropertyChecked(*this, SourceObject, MarkedClassProperty);
	TestTrue(TEXT("Import class by serialized text"), ReplicRuntimeUtils::ImportObjectPropertyFromText(DestinationObject, const_cast<FClassProperty*>(ScratchClassProperty), MarkedClassText));
	TestEqual(TEXT("ScratchClass round-trip"), DestinationObject->ScratchClass.Get(), AActor::StaticClass());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicGetterAutomationTest,
	"Replic.Runtime.Library.Getters",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicGetterAutomationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicTestMetadata();

	UReplicAutomationTestObject* TestObject = NewReplicAutomationObject();
	TestNotNull(TEXT("Test object exists"), TestObject);

	UReplicAutomationHelperObject* HelperObject = NewObject<UReplicAutomationHelperObject>(GetTransientPackage());
	TestObject->MarkedInt = 13;
	TestObject->MarkedEnum = EReplicAutomationEnum::Epic;
	TestObject->MarkedObject = HelperObject;
	TestObject->MarkedClass = AActor::StaticClass();

	int32 IntValue = 0;
	TestTrue(TEXT("GetMarkedInt succeeds"), UReplicLibrary::GetMarkedInt(TestObject, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedInt), IntValue));
	TestEqual(TEXT("GetMarkedInt value"), IntValue, 13);

	uint8 EnumValue = 0;
	TestTrue(TEXT("GetMarkedEnum succeeds"), UReplicLibrary::GetMarkedEnum(TestObject, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedEnum), EnumValue));
	TestEqual(TEXT("GetMarkedEnum value"), EnumValue, static_cast<uint8>(EReplicAutomationEnum::Epic));

	UObject* ObjectValue = nullptr;
	TestTrue(TEXT("GetMarkedObject succeeds"), UReplicLibrary::GetMarkedObject(TestObject, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedObject), ObjectValue));
	TestTrue(TEXT("GetMarkedObject value"), ObjectValue == HelperObject);

	TSubclassOf<UObject> ClassValue;
	TestTrue(TEXT("GetMarkedClass succeeds"), UReplicLibrary::GetMarkedClass(TestObject, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedClass), ClassValue));
	TestEqual(TEXT("GetMarkedClass value"), ClassValue.Get(), AActor::StaticClass());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicInvokeEventAutomationTest,
	"Replic.Runtime.Events.SerializedInvocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicInvokeEventAutomationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicTestMetadata();

	UReplicAutomationTestObject* TestObject = NewReplicAutomationObject();
	TestNotNull(TEXT("Test object exists"), TestObject);

	const UClass* TestClass = UReplicAutomationTestObject::StaticClass();
	UFunction* MarkedEventFunction = TestClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicAutomationTestObject, MarkedEvent));
	TestNotNull(TEXT("MarkedEvent function exists"), MarkedEventFunction);

	TestObject->MarkedArray = { 4, 5, 6 };
	const FArrayProperty* MarkedArrayProperty = FindPropertyChecked<FArrayProperty>(TestClass, GET_MEMBER_NAME_CHECKED(UReplicAutomationTestObject, MarkedArray));
	const FString SerializedArray = ExportPropertyChecked(*this, TestObject, MarkedArrayProperty);

	TArray<FReplicNamedValue> Arguments;
	Arguments.Add({ TEXT("Count"), LexToString(42) });
	Arguments.Add({ TEXT("Item"), FString(TEXT("Potion")) });
	Arguments.Add({ TEXT("Values"), SerializedArray });

	TestTrue(TEXT("InvokeFunctionBySerializedArguments succeeds"), ReplicRuntimeUtils::InvokeFunctionBySerializedArguments(TestObject, MarkedEventFunction, Arguments));
	TestEqual(TEXT("Event count applied"), TestObject->LastEventCount, 42);
	TestEqual(TEXT("Event item applied"), TestObject->LastEventItem, FName(TEXT("Potion")));
	TestEqual(TEXT("Event array size applied"), TestObject->LastEventValues.Num(), 3);
	TestEqual(TEXT("Event array first value applied"), TestObject->LastEventValues[0], 4);
	TestEqual(TEXT("Event array last value applied"), TestObject->LastEventValues[2], 6);

	UReplicAutomationHelperObject* ReferencedObject = NewObject<UReplicAutomationHelperObject>(TestObject);
	UFunction* ReferenceEventFunction = TestClass->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicAutomationTestObject, MarkedReferenceEvent));
	TestNotNull(TEXT("MarkedReferenceEvent function exists"), ReferenceEventFunction);

	TArray<FReplicNamedValue> ReferenceArguments;
	ReferenceArguments.Add(UReplicLibrary::MakeNamedObjectValue(TEXT("ObjectValue"), ReferencedObject));
	ReferenceArguments.Add(UReplicLibrary::MakeNamedClassValue(TEXT("ClassValue"), AActor::StaticClass()));
	TestTrue(TEXT("Native object and class arguments invoke successfully"), ReplicRuntimeUtils::InvokeFunctionBySerializedArguments(TestObject, ReferenceEventFunction, ReferenceArguments));
	TestTrue(TEXT("Native object argument is preserved"), TestObject->LastEventObject == ReferencedObject);
	TestEqual(TEXT("Native class argument is preserved"), TestObject->LastEventClass.Get(), AActor::StaticClass());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicComponentTransformStateAutomationTest,
	"Replic.Runtime.Transforms.ComponentStateApply",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicComponentTransformStateAutomationTest::RunTest(const FString& Parameters)
{
	AActor* OwnerActor = NewObject<AActor>(GetTransientPackage());
	TestNotNull(TEXT("Owner actor exists"), OwnerActor);

	USceneComponent* SceneComponent = NewObject<USceneComponent>(OwnerActor, TEXT("DoorMesh"));
	TestNotNull(TEXT("Scene component exists"), SceneComponent);
	OwnerActor->SetRootComponent(SceneComponent);
	OwnerActor->AddInstanceComponent(SceneComponent);

	UReplicTransportComponent* Transport = NewObject<UReplicTransportComponent>(OwnerActor, TEXT("ReplicTransportComponent"));
	TestNotNull(TEXT("Replic transport exists"), Transport);
	OwnerActor->AddInstanceComponent(Transport);

	FReplicComponentTransformSettings Settings;
	Settings.ComponentName = SceneComponent->GetFName();
	TestFalse(TEXT("Empty transform settings do not replicate anything"), Settings.HasAnyReplicatedChannel());
	Settings.bReplicateRotation = true;
	TestTrue(TEXT("Rotation-enabled settings replicate a transform channel"), Settings.HasAnyReplicatedChannel());

	FReplicComponentTransformState RelativeRotationState;
	RelativeRotationState.ComponentName = SceneComponent->GetFName();
	RelativeRotationState.TransformSpace = EReplicTransformSpace::Relative;
	RelativeRotationState.bReplicateRotation = true;
	RelativeRotationState.Rotation = FRotator(0.0, 90.0, 0.0);
	Transport->ApplyComponentTransformState(RelativeRotationState);
	TestEqual(TEXT("Relative rotation was applied"), SceneComponent->GetRelativeRotation(), RelativeRotationState.Rotation);

	FReplicComponentTransformState RelativeLocationScaleState;
	RelativeLocationScaleState.ComponentName = SceneComponent->GetFName();
	RelativeLocationScaleState.TransformSpace = EReplicTransformSpace::Relative;
	RelativeLocationScaleState.bReplicateLocation = true;
	RelativeLocationScaleState.bReplicateScale = true;
	RelativeLocationScaleState.Location = FVector(10.0, 20.0, 30.0);
	RelativeLocationScaleState.Scale = FVector(2.0, 3.0, 4.0);
	Transport->ApplyComponentTransformState(RelativeLocationScaleState);
	TestEqual(TEXT("Relative location was applied"), SceneComponent->GetRelativeLocation(), RelativeLocationScaleState.Location);
	TestEqual(TEXT("Relative scale was applied"), SceneComponent->GetRelativeScale3D(), RelativeLocationScaleState.Scale);
	TestEqual(TEXT("Rotation was preserved when not included in state"), SceneComponent->GetRelativeRotation(), RelativeRotationState.Rotation);

	return true;
}

#endif
