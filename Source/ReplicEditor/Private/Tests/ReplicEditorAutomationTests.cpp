#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet/KismetMathLibrary.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ReplicCallNodeValidation.h"
#include "ReplicLibrary.h"
#include "ReplicMetadata.h"
#include "ReplicPinOptionResolver.h"
#include "Tests/ReplicPIENetworkTestActors.h"
#include "K2Node_ReplicCallEvent.h"
#include "K2Node_ReplicSetEnum.h"

namespace
{
	void ConfigureReplicEditorAutomationMetadata()
	{
		if (FProperty* SharedValueProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue)))
		{
			SharedValueProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			SharedValueProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			SharedValueProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* SharedArrayProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)))
		{
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			SharedArrayProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* NameSetStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState)))
		{
			NameSetStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			NameSetStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			NameSetStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* IntSetStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState)))
		{
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			IntSetStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* NameIntMapStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState)))
		{
			NameIntMapStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			NameIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			NameIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* IntIntMapStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState)))
		{
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			IntIntMapStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* EnumStateProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, EnumState)))
		{
			EnumStateProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			EnumStateProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			EnumStateProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (FProperty* LinkedActorProperty = FindFProperty<FProperty>(AReplicPIENetworkActor::StaticClass(), GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, LinkedActor)))
		{
			LinkedActorProperty->SetMetaData(ReplicMetadata::VariableEnabled, TEXT("true"));
			LinkedActorProperty->SetMetaData(ReplicMetadata::VariablePersistent, TEXT("true"));
			LinkedActorProperty->SetMetaData(ReplicMetadata::VariablePermissionMode, TEXT("None"));
		}

		if (UFunction* MarkedPulseFunction = AReplicPIENetworkActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse)))
		{
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventEnabled, TEXT("true"));
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventPermissionMode, TEXT("None"));
			MarkedPulseFunction->SetMetaData(ReplicMetadata::EventMode, TEXT("ReplicateAll"));
		}

		if (UFunction* MarkedVectorPulseFunction = AReplicPIENetworkActor::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedVectorPulse)))
		{
			MarkedVectorPulseFunction->SetMetaData(ReplicMetadata::EventEnabled, TEXT("true"));
			MarkedVectorPulseFunction->SetMetaData(ReplicMetadata::EventPermissionMode, TEXT("None"));
			MarkedVectorPulseFunction->SetMetaData(ReplicMetadata::EventMode, TEXT("ReplicateAll"));
		}
	}

	UBlueprint* CreateTransientReplicBlueprint(const TCHAR* BaseName)
	{
		const FName UniqueName = MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), FName(BaseName));
		return FKismetEditorUtilities::CreateBlueprint(
			AReplicPIENetworkActor::StaticClass(),
			GetTransientPackage(),
			UniqueName,
			BPTYPE_Normal,
			UBlueprint::StaticClass(),
			UBlueprintGeneratedClass::StaticClass(),
			NAME_None);
	}

	UEdGraph* GetEventGraph(UBlueprint* Blueprint)
	{
		return Blueprint ? FBlueprintEditorUtils::FindEventGraph(Blueprint) : nullptr;
	}

	template<typename NodeType>
	NodeType* AddNodeToGraph(UEdGraph* Graph)
	{
		if (!Graph)
		{
			return nullptr;
		}

		NodeType* Node = NewObject<NodeType>(Graph);
		Node->CreateNewGuid();
		Graph->AddNode(Node, false, false);
		return Node;
	}

	UK2Node_CallFunction* AddCallFunctionNode(UEdGraph* Graph, UFunction* Function)
	{
		UK2Node_CallFunction* Node = AddNodeToGraph<UK2Node_CallFunction>(Graph);
		if (!Node || !Function)
		{
			return nullptr;
		}

		Node->SetFromFunction(Function);
		Node->AllocateDefaultPins();
		return Node;
	}

	bool OptionsContain(const TArray<TSharedPtr<FReplicPinOptionItem>>& Options, const FName ExpectedValue)
	{
		return Options.ContainsByPredicate([ExpectedValue](const TSharedPtr<FReplicPinOptionItem>& Item)
		{
			return Item.IsValid() && Item->Value == ExpectedValue;
		});
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorResolveSelfTargetOptionsTest,
	"Replic.Editor.Options.ResolveSelfTargetOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorResolveSelfTargetOptionsTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorSelfTargetOptions"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CallFunction* SetIntNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedInt)));
	TestNotNull(TEXT("Raw SetMarkedInt node should be created"), SetIntNode);
	if (!SetIntNode)
	{
		return false;
	}

	UEdGraphPin* PropertyNamePin = SetIntNode->FindPin(TEXT("PropertyName"));
	TestNotNull(TEXT("SetMarkedInt PropertyName pin should exist"), PropertyNamePin);

	UClass* ResolvedTargetClass = nullptr;
	TestTrue(TEXT("Unconnected TargetObject should resolve to Self target class"), ReplicPinOptionResolver::ResolveTargetClass(PropertyNamePin, ResolvedTargetClass));
	TestTrue(TEXT("Resolved self target should derive from the test actor class"), ResolvedTargetClass && ResolvedTargetClass->IsChildOf(AReplicPIENetworkActor::StaticClass()));

	TArray<TSharedPtr<FReplicPinOptionItem>> PropertyOptions;
	ReplicPinOptionResolver::BuildOptions(PropertyNamePin, PropertyOptions);
	TestTrue(TEXT("SharedValue should appear for raw SetMarkedInt on Self"), OptionsContain(PropertyOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue)));
	TestFalse(TEXT("EnumState must not appear in the int property dropdown"), OptionsContain(PropertyOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, EnumState)));

	UK2Node_CallFunction* CallEventNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, CallMarkedEvent)));
	TestNotNull(TEXT("Raw CallMarkedEvent node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	UEdGraphPin* EventNamePin = CallEventNode->FindPin(TEXT("EventName"));
	TestNotNull(TEXT("CallMarkedEvent EventName pin should exist"), EventNamePin);

	TArray<TSharedPtr<FReplicPinOptionItem>> EventOptions;
	ReplicPinOptionResolver::BuildOptions(EventNamePin, EventOptions);
	TestTrue(TEXT("MarkedPulse should appear for raw CallMarkedEvent on Self"), OptionsContain(EventOptions, GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorEnumNodeTypingTest,
	"Replic.Editor.EnumNodeTyping",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorEnumNodeTypingTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorEnumTyping"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_ReplicSetEnum* EnumNode = AddNodeToGraph<UK2Node_ReplicSetEnum>(Graph);
	TestNotNull(TEXT("Replic enum node should be created"), EnumNode);
	if (!EnumNode)
	{
		return false;
	}

	EnumNode->AllocateDefaultPins();

	UEdGraphPin* PropertyNamePin = EnumNode->GetPropertyNamePin();
	PropertyNamePin->DefaultValue = GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, EnumState).ToString();
	PropertyNamePin->AutogeneratedDefaultValue = PropertyNamePin->DefaultValue;
	EnumNode->RefreshValuePinType();

	UEdGraphPin* ValuePin = EnumNode->GetValuePin();
	TestEqual(TEXT("Enum node value pin should use byte category"), ValuePin->PinType.PinCategory, UEdGraphSchema_K2::PC_Byte);
	TestTrue(TEXT("Enum node value pin should point at the selected enum"), ValuePin->PinType.PinSubCategoryObject == StaticEnum<EReplicPIETestEnum>());

	PropertyNamePin = EnumNode->GetPropertyNamePin();
	PropertyNamePin->DefaultValue.Reset();
	PropertyNamePin->AutogeneratedDefaultValue.Reset();
	EnumNode->RefreshValuePinType();

	ValuePin = EnumNode->GetValuePin();
	TestTrue(TEXT("Clearing PropertyName should reset the enum subcategory"), ValuePin->PinType.PinSubCategoryObject == nullptr);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorResolveContainerPropertyOptionsTest,
	"Replic.Editor.Options.ResolveContainerPropertyOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorResolveContainerPropertyOptionsTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorContainerOptions"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CallFunction* SetArrayNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)));
	UK2Node_CallFunction* SetSetNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)));
	UK2Node_CallFunction* SetMapNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)));
	TestNotNull(TEXT("Raw SetMarkedArray node should be created"), SetArrayNode);
	TestNotNull(TEXT("Raw SetMarkedSet node should be created"), SetSetNode);
	TestNotNull(TEXT("Raw SetMarkedMap node should be created"), SetMapNode);
	if (!SetArrayNode || !SetSetNode || !SetMapNode)
	{
		return false;
	}

	TArray<TSharedPtr<FReplicPinOptionItem>> ArrayOptions;
	TArray<TSharedPtr<FReplicPinOptionItem>> SetOptions;
	TArray<TSharedPtr<FReplicPinOptionItem>> MapOptions;
	ReplicPinOptionResolver::BuildOptions(SetArrayNode->FindPin(TEXT("PropertyName")), ArrayOptions);
	ReplicPinOptionResolver::BuildOptions(SetSetNode->FindPin(TEXT("PropertyName")), SetOptions);
	ReplicPinOptionResolver::BuildOptions(SetMapNode->FindPin(TEXT("PropertyName")), MapOptions);

	TestTrue(TEXT("SharedArray should appear for raw SetMarkedArray"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));
	TestFalse(TEXT("NameSetState must not appear in the array property dropdown"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState)));
	TestFalse(TEXT("NameIntMapState must not appear in the array property dropdown"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState)));

	TestTrue(TEXT("NameSetState should appear for raw SetMarkedSet"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState)));
	TestFalse(TEXT("SharedArray must not appear in the set property dropdown"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));
	TestFalse(TEXT("NameIntMapState must not appear in the set property dropdown"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState)));

	TestTrue(TEXT("NameIntMapState should appear for raw SetMarkedMap"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState)));
	TestFalse(TEXT("SharedArray must not appear in the map property dropdown"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));
	TestFalse(TEXT("NameSetState must not appear in the map property dropdown"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorResolveContainerDeltaPropertyOptionsTest,
	"Replic.Editor.Options.ResolveContainerDeltaPropertyOptions",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorResolveContainerDeltaPropertyOptionsTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorContainerDeltaOptions"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CallFunction* AddArrayNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)));
	UK2Node_CallFunction* AddSetNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)));
	UK2Node_CallFunction* SetMapNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)));
	UK2Node_CallFunction* RemoveMapNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap)));
	TestNotNull(TEXT("Raw AddToMarkedArray node should be created"), AddArrayNode);
	TestNotNull(TEXT("Raw AddToMarkedSet node should be created"), AddSetNode);
	TestNotNull(TEXT("Raw SetInMarkedMap node should be created"), SetMapNode);
	TestNotNull(TEXT("Raw RemoveFromMarkedMap node should be created"), RemoveMapNode);
	if (!AddArrayNode || !AddSetNode || !SetMapNode || !RemoveMapNode)
	{
		return false;
	}

	TArray<TSharedPtr<FReplicPinOptionItem>> ArrayOptions;
	TArray<TSharedPtr<FReplicPinOptionItem>> SetOptions;
	TArray<TSharedPtr<FReplicPinOptionItem>> MapOptions;
	TArray<TSharedPtr<FReplicPinOptionItem>> RemoveMapOptions;
	ReplicPinOptionResolver::BuildOptions(AddArrayNode->FindPin(TEXT("PropertyName")), ArrayOptions);
	ReplicPinOptionResolver::BuildOptions(AddSetNode->FindPin(TEXT("PropertyName")), SetOptions);
	ReplicPinOptionResolver::BuildOptions(SetMapNode->FindPin(TEXT("PropertyName")), MapOptions);
	ReplicPinOptionResolver::BuildOptions(RemoveMapNode->FindPin(TEXT("PropertyName")), RemoveMapOptions);

	TestTrue(TEXT("SharedArray should appear for raw AddToMarkedArray"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));
	TestFalse(TEXT("IntSetState must not appear in the array delta dropdown"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState)));
	TestFalse(TEXT("IntIntMapState must not appear in the array delta dropdown"), OptionsContain(ArrayOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState)));

	TestTrue(TEXT("IntSetState should appear for raw AddToMarkedSet"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState)));
	TestTrue(TEXT("NameSetState should also appear for raw AddToMarkedSet"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState)));
	TestFalse(TEXT("SharedArray must not appear in the set delta dropdown"), OptionsContain(SetOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));

	TestTrue(TEXT("IntIntMapState should appear for raw SetInMarkedMap"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState)));
	TestTrue(TEXT("NameIntMapState should also appear for raw SetInMarkedMap"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState)));
	TestFalse(TEXT("SharedArray must not appear in the map delta dropdown"), OptionsContain(MapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray)));

	TestTrue(TEXT("IntIntMapState should appear for raw RemoveFromMarkedMap"), OptionsContain(RemoveMapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState)));
	TestFalse(TEXT("IntSetState must not appear in the remove map dropdown"), OptionsContain(RemoveMapOptions, GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorCallEventArgumentRefreshTest,
	"Replic.Editor.CallEventArgumentRefresh",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorCallEventArgumentRefreshTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorCallEventRefresh"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_ReplicCallEvent* CallEventNode = AddNodeToGraph<UK2Node_ReplicCallEvent>(Graph);
	TestNotNull(TEXT("Replic call event node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	CallEventNode->AllocateDefaultPins();

	UEdGraphPin* EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue = GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedPulse).ToString();
	EventNamePin->AutogeneratedDefaultValue = EventNamePin->DefaultValue;
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	TArray<UEdGraphPin*> ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Selecting MarkedPulse should create exactly one argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() == 1)
	{
		TestEqual(TEXT("MarkedPulse argument pin should be Delta"), ArgumentPins[0]->PinName, FName(TEXT("Delta")));
		TestEqual(TEXT("MarkedPulse argument pin should be int"), ArgumentPins[0]->PinType.PinCategory, UEdGraphSchema_K2::PC_Int);
	}

	EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue.Reset();
	EventNamePin->AutogeneratedDefaultValue.Reset();
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Clearing EventName should remove dynamic argument pins"), ArgumentPins.Num(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorCallEventStructArgumentReconstructTest,
	"Replic.Editor.CallEventStructArgumentReconstruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorCallEventStructArgumentReconstructTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorCallEventStructReconstruct"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_ReplicCallEvent* CallEventNode = AddNodeToGraph<UK2Node_ReplicCallEvent>(Graph);
	TestNotNull(TEXT("Replic call event node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	CallEventNode->AllocateDefaultPins();

	UEdGraphPin* EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue = GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedVectorPulse).ToString();
	EventNamePin->AutogeneratedDefaultValue = EventNamePin->DefaultValue;
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	TArray<UEdGraphPin*> ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Selecting MarkedVectorPulse should create exactly one top-level argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() != 1)
	{
		return false;
	}

	UEdGraphPin* StructArgumentPin = ArgumentPins[0];
	TestEqual(TEXT("Struct argument pin should be named Delta"), StructArgumentPin->PinName, FName(TEXT("Delta")));
	TestEqual(TEXT("Struct argument pin should be a struct"), StructArgumentPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Struct);

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	TestNotNull(TEXT("K2 schema should exist"), Schema);
	if (!Schema)
	{
		return false;
	}

	Schema->SplitPin(StructArgumentPin, false);
	TestTrue(TEXT("Struct argument pin should have child pins after split"), StructArgumentPin->SubPins.Num() > 0);

	CallEventNode->ReconstructNode();

	ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Reconstructing should still keep exactly one top-level argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() != 1)
	{
		return false;
	}

	StructArgumentPin = ArgumentPins[0];
	TestEqual(TEXT("Reconstructed struct argument pin should still be named Delta"), StructArgumentPin->PinName, FName(TEXT("Delta")));
	TestEqual(TEXT("Reconstructed struct argument pin should still be a struct"), StructArgumentPin->PinType.PinCategory, UEdGraphSchema_K2::PC_Struct);
	TestTrue(TEXT("Reconstructed struct argument pin should restore its split child pins"), StructArgumentPin->SubPins.Num() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorCallEventStructConnectionReconstructTest,
	"Replic.Editor.CallEventStructConnectionReconstruct",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorCallEventStructConnectionReconstructTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorCallEventStructConnection"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_ReplicCallEvent* CallEventNode = AddNodeToGraph<UK2Node_ReplicCallEvent>(Graph);
	TestNotNull(TEXT("Replic call event node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	CallEventNode->AllocateDefaultPins();

	UEdGraphPin* EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue = GET_FUNCTION_NAME_CHECKED(AReplicPIENetworkActor, MarkedVectorPulse).ToString();
	EventNamePin->AutogeneratedDefaultValue = EventNamePin->DefaultValue;
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	TArray<UEdGraphPin*> ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Selecting MarkedVectorPulse should create exactly one top-level argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() != 1)
	{
		return false;
	}

	UK2Node_CallFunction* MakeVectorNode = AddCallFunctionNode(Graph, UKismetMathLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetMathLibrary, MakeVector)));
	TestNotNull(TEXT("MakeVector node should be created"), MakeVectorNode);
	if (!MakeVectorNode)
	{
		return false;
	}

	UEdGraphPin* MakeVectorOutputPin = MakeVectorNode->GetReturnValuePin();
	TestNotNull(TEXT("MakeVector return pin should exist"), MakeVectorOutputPin);
	if (!MakeVectorOutputPin)
	{
		return false;
	}

	const UEdGraphSchema_K2* Schema = GetDefault<UEdGraphSchema_K2>();
	TestNotNull(TEXT("K2 schema should exist"), Schema);
	if (!Schema)
	{
		return false;
	}

	UEdGraphPin* StructArgumentPin = ArgumentPins[0];
	TestTrue(TEXT("Struct argument pin should accept a MakeVector connection"), Schema->TryCreateConnection(MakeVectorOutputPin, StructArgumentPin));
	TestEqual(TEXT("Struct argument pin should have one link before reconstruct"), StructArgumentPin->LinkedTo.Num(), 1);

	CallEventNode->ReconstructNode();

	ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Reconstructing should still keep exactly one top-level argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() != 1)
	{
		return false;
	}

	StructArgumentPin = ArgumentPins[0];
	TestEqual(TEXT("Reconstructed struct argument pin should still be named Delta"), StructArgumentPin->PinName, FName(TEXT("Delta")));
	TestEqual(TEXT("Reconstructed struct argument pin should still have one link"), StructArgumentPin->LinkedTo.Num(), 1);
	TestTrue(TEXT("Reconstructed struct argument pin should remain linked to MakeVector"), StructArgumentPin->LinkedTo.Num() == 1 && StructArgumentPin->LinkedTo[0] == MakeVectorOutputPin);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorCallEventSameBlueprintCompileFallbackTest,
	"Replic.Editor.CallEventSameBlueprintCompileFallback",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorCallEventSameBlueprintCompileFallbackTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorCallEventSelfCompile"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CustomEvent* CustomEventNode = AddNodeToGraph<UK2Node_CustomEvent>(Graph);
	TestNotNull(TEXT("Custom event node should be created"), CustomEventNode);
	if (!CustomEventNode)
	{
		return false;
	}

	CustomEventNode->CustomFunctionName = FName(TEXT("LocalMarkedReplicEvent"));
	CustomEventNode->AllocateDefaultPins();
	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventEnabled, FString(TEXT("true")));
	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventPermissionMode, FString(TEXT("None")));
	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventMode, FString(TEXT("ReplicateAll")));

	UK2Node_ReplicCallEvent* CallEventNode = AddNodeToGraph<UK2Node_ReplicCallEvent>(Graph);
	TestNotNull(TEXT("Replic call event node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	CallEventNode->AllocateDefaultPins();

	UEdGraphPin* EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue = CustomEventNode->CustomFunctionName.ToString();
	EventNamePin->AutogeneratedDefaultValue = EventNamePin->DefaultValue;
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TestTrue(TEXT("Blueprint with same-Blueprint Replic event call should compile"), Blueprint->Status != BS_Error);

	return Blueprint->Status != BS_Error;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorCallEventBoolArgumentCompileTest,
	"Replic.Editor.CallEventBoolArgumentCompile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorCallEventBoolArgumentCompileTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorCallEventBoolCompile"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CustomEvent* CustomEventNode = AddNodeToGraph<UK2Node_CustomEvent>(Graph);
	TestNotNull(TEXT("Custom event node should be created"), CustomEventNode);
	if (!CustomEventNode)
	{
		return false;
	}

	CustomEventNode->CustomFunctionName = FName(TEXT("LocalMarkedBoolReplicEvent"));
	CustomEventNode->AllocateDefaultPins();

	FEdGraphPinType BoolPinType;
	BoolPinType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	CustomEventNode->CreateUserDefinedPin(FName(TEXT("bIsOpening")), BoolPinType, EGPD_Output, false);
	CustomEventNode->ReconstructNode();

	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventEnabled, FString(TEXT("true")));
	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventPermissionMode, FString(TEXT("None")));
	CustomEventNode->GetUserDefinedMetaData().SetMetaData(ReplicMetadata::EventMode, FString(TEXT("ReplicateAll")));

	UK2Node_ReplicCallEvent* CallEventNode = AddNodeToGraph<UK2Node_ReplicCallEvent>(Graph);
	TestNotNull(TEXT("Replic call event node should be created"), CallEventNode);
	if (!CallEventNode)
	{
		return false;
	}

	CallEventNode->AllocateDefaultPins();

	UEdGraphPin* EventNamePin = CallEventNode->GetEventNamePin();
	EventNamePin->DefaultValue = CustomEventNode->CustomFunctionName.ToString();
	EventNamePin->AutogeneratedDefaultValue = EventNamePin->DefaultValue;
	CallEventNode->PinDefaultValueChanged(EventNamePin);

	const TArray<UEdGraphPin*> ArgumentPins = CallEventNode->GetArgumentPins();
	TestEqual(TEXT("Bool Replic event should expose one argument pin"), ArgumentPins.Num(), 1);
	if (ArgumentPins.Num() != 1)
	{
		return false;
	}

	ArgumentPins[0]->DefaultValue = TEXT("true");
	FKismetEditorUtilities::CompileBlueprint(Blueprint);

	TestTrue(TEXT("Blueprint with bool Replic event argument should compile"), Blueprint->Status != BS_Error);

	return Blueprint->Status != BS_Error;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorRawGenericValueValidationTest,
	"Replic.Editor.RawGenericValueValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorRawGenericValueValidationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorGenericValidation"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	auto ExpectValidationError = [this, Graph](UFunction* Function, const FName PropertyName, TFunctionRef<void(UEdGraphPin*)> MutateValuePin, const TCHAR* What) -> bool
	{
		UK2Node_CallFunction* Node = AddCallFunctionNode(Graph, Function);
		TestNotNull(FString::Printf(TEXT("%s node should be created"), What), Node);
		if (!Node)
		{
			return false;
		}

		UEdGraphPin* PropertyPin = Node->FindPin(TEXT("PropertyName"));
		UEdGraphPin* ValuePin = Node->FindPin(TEXT("Value"));
		TestNotNull(FString::Printf(TEXT("%s PropertyName pin should exist"), What), PropertyPin);
		TestNotNull(FString::Printf(TEXT("%s Value pin should exist"), What), ValuePin);
		if (!PropertyPin || !ValuePin)
		{
			return false;
		}

		PropertyPin->DefaultValue = PropertyName.ToString();
		PropertyPin->AutogeneratedDefaultValue = PropertyPin->DefaultValue;
		MutateValuePin(ValuePin);

		FString ValidationMessage;
		EMessageSeverity::Type ValidationSeverity = EMessageSeverity::Info;
		const bool bHasValidationIssue = ReplicCallNodeValidation::ValidateReplicCallNode(Node, ValidationMessage, ValidationSeverity);
		TestTrue(FString::Printf(TEXT("%s should raise a validation issue"), What), bHasValidationIssue);
		TestEqual(FString::Printf(TEXT("%s should raise an error"), What), ValidationSeverity, EMessageSeverity::Error);
		return bHasValidationIssue && ValidationSeverity == EMessageSeverity::Error;
	};

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedInt)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedValue),
		[](UEdGraphPin* ValuePin)
		{
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			PinType.PinSubCategoryObject = AReplicPIENetworkPlayerController::StaticClass();
			ValuePin->PinType = PinType;
		},
		TEXT("Raw SetMarkedInt primitive mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedArray)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
		[](UEdGraphPin* ValuePin)
		{
			FEdGraphPinType PinType;
			PinType.ContainerType = EPinContainerType::Set;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			ValuePin->PinType = PinType;
		},
		TEXT("Raw SetMarkedArray container mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedSet)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameSetState),
		[](UEdGraphPin* ValuePin)
		{
			FEdGraphPinType PinType;
			PinType.ContainerType = EPinContainerType::Array;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			ValuePin->PinType = PinType;
		},
		TEXT("Raw SetMarkedSet container mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedMap)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, NameIntMapState),
		[](UEdGraphPin* ValuePin)
		{
			FEdGraphPinType PinType;
			PinType.ContainerType = EPinContainerType::Map;
			PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			PinType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Float;
			ValuePin->PinType = PinType;
		},
		TEXT("Raw SetMarkedMap map value mismatch"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorRawReferenceValidationTest,
	"Replic.Editor.RawReferenceValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorRawReferenceValidationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorReferenceValidation"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	UK2Node_CallFunction* SetObjectNode = AddCallFunctionNode(Graph, UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetMarkedObject)));
	TestNotNull(TEXT("Raw SetMarkedObject node should be created"), SetObjectNode);
	if (!SetObjectNode)
	{
		return false;
	}

	UEdGraphPin* PropertyNamePin = SetObjectNode->FindPin(TEXT("PropertyName"));
	UEdGraphPin* ValuePin = SetObjectNode->FindPin(TEXT("Value"));
	TestNotNull(TEXT("SetMarkedObject PropertyName pin should exist"), PropertyNamePin);
	TestNotNull(TEXT("SetMarkedObject Value pin should exist"), ValuePin);
	if (!PropertyNamePin || !ValuePin)
	{
		return false;
	}

	PropertyNamePin->DefaultValue = GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, LinkedActor).ToString();
	PropertyNamePin->AutogeneratedDefaultValue = PropertyNamePin->DefaultValue;

	ValuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	ValuePin->PinType.PinSubCategoryObject = AReplicPIENetworkActor::StaticClass();

	FString ValidationMessage;
	EMessageSeverity::Type ValidationSeverity = EMessageSeverity::Info;
	TestFalse(TEXT("Compatible reference value should not raise a validation issue"), ReplicCallNodeValidation::ValidateReplicCallNode(SetObjectNode, ValidationMessage, ValidationSeverity));

	ValuePin->PinType.PinSubCategoryObject = AReplicPIENetworkPlayerController::StaticClass();
	const bool bHasValidationIssue = ReplicCallNodeValidation::ValidateReplicCallNode(SetObjectNode, ValidationMessage, ValidationSeverity);
	TestTrue(TEXT("Incompatible reference value should raise a validation issue"), bHasValidationIssue);
	TestEqual(TEXT("Incompatible reference value should raise an error"), ValidationSeverity, EMessageSeverity::Error);
	TestFalse(TEXT("Validation message should not be empty for an incompatible reference value"), ValidationMessage.IsEmpty());

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FReplicEditorRawContainerDeltaValidationTest,
	"Replic.Editor.RawContainerDeltaValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FReplicEditorRawContainerDeltaValidationTest::RunTest(const FString& Parameters)
{
	ConfigureReplicEditorAutomationMetadata();

	UBlueprint* Blueprint = CreateTransientReplicBlueprint(TEXT("ReplicEditorContainerDeltaValidation"));
	UEdGraph* Graph = GetEventGraph(Blueprint);
	TestNotNull(TEXT("Transient test blueprint should be created"), Blueprint);
	TestNotNull(TEXT("Transient test event graph should exist"), Graph);
	if (!Blueprint || !Graph)
	{
		return false;
	}

	auto ExpectValidationError = [this, Graph](UFunction* Function, const FName PropertyName, TFunctionRef<void(UK2Node_CallFunction*, FString&)> MutatePins, const TCHAR* What) -> bool
	{
		UK2Node_CallFunction* Node = AddCallFunctionNode(Graph, Function);
		TestNotNull(FString::Printf(TEXT("%s node should be created"), What), Node);
		if (!Node)
		{
			return false;
		}

		UEdGraphPin* PropertyPin = Node->FindPin(TEXT("PropertyName"));
		TestNotNull(FString::Printf(TEXT("%s PropertyName pin should exist"), What), PropertyPin);
		if (!PropertyPin)
		{
			return false;
		}

		PropertyPin->DefaultValue = PropertyName.ToString();
		PropertyPin->AutogeneratedDefaultValue = PropertyPin->DefaultValue;

		FString MutateError;
		MutatePins(Node, MutateError);
		TestTrue(FString::Printf(TEXT("%s should be able to mutate the relevant pins"), What), MutateError.IsEmpty());
		if (!MutateError.IsEmpty())
		{
			AddError(MutateError);
			return false;
		}

		FString ValidationMessage;
		EMessageSeverity::Type ValidationSeverity = EMessageSeverity::Info;
		const bool bHasValidationIssue = ReplicCallNodeValidation::ValidateReplicCallNode(Node, ValidationMessage, ValidationSeverity);
		TestTrue(FString::Printf(TEXT("%s should raise a validation issue"), What), bHasValidationIssue);
		TestEqual(FString::Printf(TEXT("%s should raise an error"), What), ValidationSeverity, EMessageSeverity::Error);
		return bHasValidationIssue && ValidationSeverity == EMessageSeverity::Error;
	};

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedArray)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, SharedArray),
		[](UK2Node_CallFunction* Node, FString& MutateError)
		{
			if (UEdGraphPin* ItemPin = Node->FindPin(TEXT("Item")))
			{
				ItemPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			}
			else
			{
				MutateError = TEXT("Array delta Item pin was missing.");
			}
		},
		TEXT("Raw AddToMarkedArray item mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, AddToMarkedSet)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntSetState),
		[](UK2Node_CallFunction* Node, FString& MutateError)
		{
			if (UEdGraphPin* ItemPin = Node->FindPin(TEXT("Item")))
			{
				ItemPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			}
			else
			{
				MutateError = TEXT("Set delta Item pin was missing.");
			}
		},
		TEXT("Raw AddToMarkedSet item mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, SetInMarkedMap)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
		[](UK2Node_CallFunction* Node, FString& MutateError)
		{
			UEdGraphPin* KeyPin = Node->FindPin(TEXT("Key"));
			UEdGraphPin* ValuePin = Node->FindPin(TEXT("Value"));
			if (!KeyPin || !ValuePin)
			{
				MutateError = TEXT("Map delta Key/Value pin was missing.");
				return;
			}

			KeyPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			ValuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Real;
			ValuePin->PinType.PinSubCategory = UEdGraphSchema_K2::PC_Float;
		},
		TEXT("Raw SetInMarkedMap key/value mismatch"));

	ExpectValidationError(
		UReplicLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UReplicLibrary, RemoveFromMarkedMap)),
		GET_MEMBER_NAME_CHECKED(AReplicPIENetworkActor, IntIntMapState),
		[](UK2Node_CallFunction* Node, FString& MutateError)
		{
			if (UEdGraphPin* KeyPin = Node->FindPin(TEXT("Key")))
			{
				KeyPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
			}
			else
			{
				MutateError = TEXT("Remove map delta Key pin was missing.");
			}
		},
		TEXT("Raw RemoveFromMarkedMap key mismatch"));

	return true;
}

#endif
