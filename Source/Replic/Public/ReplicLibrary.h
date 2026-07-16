#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ReplicTypes.h"
#include "Templates/SubclassOf.h"

#include "ReplicLibrary.generated.h"

class UReplicPropertyObserver;

UCLASS()
class REPLIC_API UReplicLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category = "Replic|Debug", meta = (DefaultToSelf = "TargetObject", DisplayName = "Has Replic Transport Component", ToolTip = "Checks whether Replic can resolve TargetObject and its host actor currently owns a ReplicTransportComponent.\n\nThis diagnostic check never creates or adds a component."))
	static bool HasReplicTransportComponent(UObject* TargetObject);

	UFUNCTION(BlueprintCallable, Category = "Replic|Debug", meta = (DefaultToSelf = "TargetObject", DisplayName = "Get Marked Property Debug Info", ToolTip = "Inspects a marked property without changing gameplay state.\n\nReturns target resolution, transport presence, Replic settings, the current local value, and the locally stored persistent value. Return Value is true only when the target, property, and Replic configuration are valid."))
	static bool GetMarkedPropertyDebugInfo(UObject* TargetObject, FName PropertyName, FReplicPropertyDebugInfo& DebugInfo);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ObservedObject", ToolTip = "Registers an object so Replic can re-apply stored persistent state to it when needed.\n\nUse this for widgets or other observed objects that are created after the replicated state already exists."))
	static void RegisterObservedObject(UObject* ObservedObject);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ObservedObject", ToolTip = "Unregisters a previously observed object from Replic.\n\nUse this when the observed object is going away and should stop receiving state re-application."))
	static void UnregisterObservedObject(UObject* ObservedObject);

	UFUNCTION(BlueprintCallable, Category = "Replic|Observe", meta = (DefaultToSelf = "TargetObject", ToolTip = "Creates a Replic property observer for the given target object.\n\nThe returned observer fires its OnChanged delegate whenever Replic applies a different value to the selected marked property on this machine.\n\nLeave PropertyName empty or choose None to listen for any marked property on the target object."))
	static UReplicPropertyObserver* BindMarkedPropertyChanged(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedAnyPropertyOptions")) FName PropertyName);

	UFUNCTION(BlueprintCallable, Category = "Replic|Observe", meta = (ToolTip = "Stops a previously created Replic property observer.\n\nThis is the explicit unbind node for Bind Marked Property Changed."))
	static void UnbindMarkedPropertyChanged(UReplicPropertyObserver* Observer);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked bool property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedBool(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedBoolPropertyOptions")) FName PropertyName, bool& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked int property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedInt(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedIntPropertyOptions")) FName PropertyName, int32& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked float property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedFloat(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedFloatPropertyOptions")) FName PropertyName, double& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked byte or enum-backed property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedByte(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedBytePropertyOptions")) FName PropertyName, uint8& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked enum property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked enum property on TargetObject.\n@param Value Receives the current local enum value as its underlying byte if the read succeeds."))
	static bool GetMarkedEnum(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedEnumPropertyOptions")) FName PropertyName, uint8& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked name property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedName(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedNamePropertyOptions")) FName PropertyName, FName& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked string property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedString(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedStringPropertyOptions")) FName PropertyName, FString& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked text property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedText(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedTextPropertyOptions")) FName PropertyName, FText& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked vector property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedVector(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedVectorPropertyOptions")) FName PropertyName, FVector& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked rotator property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedRotator(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedRotatorPropertyOptions")) FName PropertyName, FRotator& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked transform property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedTransform(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedTransformPropertyOptions")) FName PropertyName, FTransform& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked object reference property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedObject(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedObjectPropertyOptions")) FName PropertyName, UObject*& Value);

	UFUNCTION(BlueprintPure, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ToolTip = "Reads a marked class reference property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedClass(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedClassPropertyOptions")) FName PropertyName, TSubclassOf<UObject>& Value);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "TargetObject", CustomStructureParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Reads a marked struct property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedStruct(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedStructPropertyOptions")) FName PropertyName, int32& Value);

	DECLARE_FUNCTION(execGetMarkedStruct);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "TargetObject", ArrayParm = "Value", ArrayTypeDependentParams = "Value", AutoCreateRefTerm = "Value", ToolTip = "Reads a marked array property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedArray(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedArrayPropertyOptions")) FName PropertyName, TArray<int32>& Value);

	DECLARE_FUNCTION(execGetMarkedArray);

	UFUNCTION(BlueprintPure, CustomThunk, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "TargetObject", ArrayParm = "Value", ArrayTypeDependentParams = "Value", AutoCreateRefTerm = "Value"))
	static bool GetMarkedArrayInternal(UObject* TargetObject, FName PropertyName, TArray<int32>& Value);

	DECLARE_FUNCTION(execGetMarkedArrayInternal);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "TargetObject", SetParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Reads a marked set property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedSet(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedSetPropertyOptions")) FName PropertyName, TSet<int32>& Value);

	DECLARE_FUNCTION(execGetMarkedSet);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "TargetObject", MapParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Reads a marked map property from the local target object.\n\nThis does not send anything over the network. It only reads the current local value.\n\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value Receives the current local value if the read succeeds."))
	static bool GetMarkedMap(UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedMapPropertyOptions")) FName PropertyName, TMap<int32, int32>& Value);

	DECLARE_FUNCTION(execGetMarkedMap);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked bool property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedBool(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedBoolPropertyOptions")) FName PropertyName, bool Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked int property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedInt(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedIntPropertyOptions")) FName PropertyName, int32 Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked float property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedFloat(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedFloatPropertyOptions")) FName PropertyName, double Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked byte property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedByte(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedBytePropertyOptions")) FName PropertyName, uint8 Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked enum property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target enum property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked enum property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked enum property on TargetObject.\n@param Value New enum value as its underlying byte that should be written and replicated."))
	static bool SetMarkedEnum(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedEnumPropertyOptions")) FName PropertyName, uint8 Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked name property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedName(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedNamePropertyOptions")) FName PropertyName, FName Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked string property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedString(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedStringPropertyOptions")) FName PropertyName, const FString& Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked text property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedText(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedTextPropertyOptions")) FName PropertyName, const FText& Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked vector property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedVector(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedVectorPropertyOptions")) FName PropertyName, FVector Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked rotator property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedRotator(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedRotatorPropertyOptions")) FName PropertyName, FRotator Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked transform property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedTransform(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedTransformPropertyOptions")) FName PropertyName, FTransform Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked object reference property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedObject(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedObjectPropertyOptions")) FName PropertyName, UObject* Value);

	UFUNCTION(BlueprintCallable, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ToolTip = "Writes a marked class reference property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedClass(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedClassPropertyOptions")) FName PropertyName, TSubclassOf<UObject> Value);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Writes a marked struct property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nThe target property must be marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedStruct(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedStructPropertyOptions")) FName PropertyName, const int32& Value);

	DECLARE_FUNCTION(execSetMarkedStruct);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", ArrayParm = "Value", ArrayTypeDependentParams = "Value", AutoCreateRefTerm = "Value", ToolTip = "Legacy generic array writer. Prefer the typed Replic Set Array node created from the variable Details panel; it keeps the array pin stable and is the recommended Blueprint workflow.\n\nFlow:\nClient or host -> server -> target actor.\n\nUse this raw Set Marked Array node only when you specifically need the generic function-call version.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedArray(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedArrayPropertyOptions")) FName PropertyName, const TArray<int32>& Value);

	DECLARE_FUNCTION(execSetMarkedArray);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true", DefaultToSelf = "ContextObject", ArrayParm = "Value", ArrayTypeDependentParams = "Value", AutoCreateRefTerm = "Value"))
	static bool SetMarkedArrayInternal(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const TArray<int32>& Value);

	DECLARE_FUNCTION(execSetMarkedArrayInternal);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", SetParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Writes a marked set property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nUse this for sets of Blueprint-supported types or structs that are marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedSet(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedSetPropertyOptions")) FName PropertyName, const TSet<int32>& Value);

	DECLARE_FUNCTION(execSetMarkedSet);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", MapParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Writes a marked map property through Replic.\n\nFlow:\nClient or host -> server -> target actor.\n\nUse this for maps of Blueprint-supported keys and values or structs that are marked for Replic in the Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked property. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked property on TargetObject.\n@param Value New value that should be written and replicated."))
	static bool SetMarkedMap(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedMapPropertyOptions")) FName PropertyName, const TMap<int32, int32>& Value);

	DECLARE_FUNCTION(execSetMarkedMap);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Item", AutoCreateRefTerm = "Item", ToolTip = "Adds an item to a marked Replic array on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative array -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked array. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked array property on TargetObject.\n@param Item Array item to add."))
	static bool AddToMarkedArray(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedArrayPropertyOptions")) FName PropertyName, const int32& Item);

	DECLARE_FUNCTION(execAddToMarkedArray);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Item", AutoCreateRefTerm = "Item", ToolTip = "Removes all matching items from a marked Replic array on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative array -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked array. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked array property on TargetObject.\n@param Item Array item to remove."))
	static bool RemoveFromMarkedArray(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedArrayPropertyOptions")) FName PropertyName, const int32& Item);

	DECLARE_FUNCTION(execRemoveFromMarkedArray);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Item", AutoCreateRefTerm = "Item", ToolTip = "Adds an item to a marked Replic set on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative set -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked set. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked set property on TargetObject.\n@param Item Set item to add."))
	static bool AddToMarkedSet(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedSetPropertyOptions")) FName PropertyName, const int32& Item);

	DECLARE_FUNCTION(execAddToMarkedSet);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Item", AutoCreateRefTerm = "Item", ToolTip = "Removes a matching item from a marked Replic set on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative set -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked set. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked set property on TargetObject.\n@param Item Set item to remove."))
	static bool RemoveFromMarkedSet(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedSetPropertyOptions")) FName PropertyName, const int32& Item);

	DECLARE_FUNCTION(execRemoveFromMarkedSet);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Key,Value", AutoCreateRefTerm = "Key,Value", ToolTip = "Adds or updates an entry in a marked Replic map on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative map -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked map. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked map property on TargetObject.\n@param Key Map key to add or update.\n@param Value New value that should be stored for the key."))
	static bool SetInMarkedMap(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedMapPropertyOptions")) FName PropertyName, const int32& Key, const int32& Value);

	DECLARE_FUNCTION(execSetInMarkedMap);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic", meta = (DefaultToSelf = "ContextObject", CustomStructureParam = "Key", AutoCreateRefTerm = "Key", ToolTip = "Removes an entry from a marked Replic map on the authoritative target.\n\nFlow:\nClient or host -> server -> mutate current authoritative map -> replicate result.\n\n@param ContextObject Object used to identify who is making the request.\n@param TargetObject Object that owns the marked map. Leave this empty only when the property is on Self.\n@param PropertyName Name of the Replic-marked map property on TargetObject.\n@param Key Map key to remove."))
	static bool RemoveFromMarkedMap(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedMapPropertyOptions")) FName PropertyName, const int32& Key);

	DECLARE_FUNCTION(execRemoveFromMarkedMap);

	UFUNCTION(BlueprintCallable, Category = "Replic|Events", meta = (DefaultToSelf = "ContextObject", AutoCreateRefTerm = "Arguments", ToolTip = "Calls a marked custom event through Replic.\n\nFlow:\nClient or host -> server -> dispatch.\n\nThe target custom event must have Replic enabled in its Details panel.\n\n@param ContextObject Object used to identify who is making the request. Usually Self on the calling Character, Pawn, PlayerController, Actor, or Component that has access to a ReplicTransportComponent.\n@param TargetObject Object that owns the marked custom event. Leave this empty only when the event is on Self. If the event belongs to another Blueprint, connect that actor or component here.\n@param EventName Name of the Replic-marked custom event on TargetObject.\n@param Arguments Serialized event arguments. The node builds these automatically from the event signature."))
	static bool CallMarkedEvent(UObject* ContextObject, UObject* TargetObject, UPARAM(meta = (GetOptions = "GetMarkedEventOptions")) FName EventName, const TArray<FReplicNamedValue>& Arguments);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named bool argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedBoolValue(FName Name, bool Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named int argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedIntValue(FName Name, int32 Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named float argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedFloatValue(FName Name, double Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named name argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedNameValue(FName Name, FName Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named string argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedStringValue(FName Name, const FString& Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named text argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedTextValue(FName Name, const FText& Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named vector argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedVectorValue(FName Name, FVector Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named rotator argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedRotatorValue(FName Name, FRotator Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named transform argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedTransformValue(FName Name, FTransform Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named object reference argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedObjectValue(FName Name, UObject* Value);

	UFUNCTION(BlueprintPure, Category = "Replic|Events", meta = (ToolTip = "Creates a named class reference argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedClassValue(FName Name, TSubclassOf<UObject> Value);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Events", meta = (CustomStructureParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Creates a named struct argument that can be passed into Call Marked Event. The name must match the custom event parameter name."))
	static FReplicNamedValue MakeNamedStructValue(FName Name, const int32& Value);

	DECLARE_FUNCTION(execMakeNamedStructValue);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Events", meta = (ArrayParm = "Value", AutoCreateRefTerm = "Value", ToolTip = "Creates a named array argument that can be passed into Call Marked Event.\n\nThe name must match the custom event parameter name.\n\nUse this for arrays of Blueprint-supported types or structs."))
	static FReplicNamedValue MakeNamedArrayValue(FName Name, const TArray<int32>& Value);

	DECLARE_FUNCTION(execMakeNamedArrayValue);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Events", meta = (SetParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Creates a named set argument that can be passed into Call Marked Event.\n\nThe name must match the custom event parameter name.\n\nUse this for sets of Blueprint-supported types or structs."))
	static FReplicNamedValue MakeNamedSetValue(FName Name, const TSet<int32>& Value);

	DECLARE_FUNCTION(execMakeNamedSetValue);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Events", meta = (MapParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Creates a named map argument that can be passed into Call Marked Event.\n\nThe name must match the custom event parameter name.\n\nUse this for maps of Blueprint-supported keys and values or structs."))
	static FReplicNamedValue MakeNamedMapValue(FName Name, const TMap<int32, int32>& Value);

	DECLARE_FUNCTION(execMakeNamedMapValue);

	UFUNCTION(BlueprintCallable, CustomThunk, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true", CustomStructureParam = "Value", AutoCreateRefTerm = "Value", ToolTip = "Internal wildcard event argument builder used by Replic K2 nodes."))
	static FReplicNamedValue MakeNamedGenericValue(FName Name, const int32& Value);

	DECLARE_FUNCTION(execMakeNamedGenericValue);

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedBoolPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedAnyPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedIntPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedFloatPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedBytePropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedEnumPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedNamePropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedStringPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedTextPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedVectorPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedRotatorPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedTransformPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedObjectPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedClassPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedStructPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedArrayPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedSetPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedMapPropertyOptions();

	UFUNCTION(BlueprintPure, Category = "Replic|Internal", meta = (BlueprintInternalUseOnly = "true"))
	static TArray<FString> GetMarkedEventOptions();

private:
	static bool GetMarkedValueIntoProperty(UObject* TargetObject, FName PropertyName, const FProperty* OutputProperty, void* OutputValuePtr);
	static bool SetMarkedValueFromProperty(UObject* ContextObject, UObject* TargetObject, FName PropertyName, const FProperty* ValueProperty, const void* ValuePtr);
	static bool RequestMarkedContainerDeltaFromProperties(
		UObject* ContextObject,
		UObject* TargetObject,
		FName PropertyName,
		EReplicContainerDeltaOperation Operation,
		const FProperty* PrimaryProperty,
		const void* PrimaryValuePtr,
		const FProperty* SecondaryProperty,
		const void* SecondaryValuePtr);
	static FReplicNamedValue MakeNamedValueFromProperty(FName Name, const FProperty* ValueProperty, const void* ValuePtr);
};
