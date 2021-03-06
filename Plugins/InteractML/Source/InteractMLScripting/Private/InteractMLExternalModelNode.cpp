//----
// InteractML - Interactive Machine Learning Plugin
// Copyright (c) 2021 Phoenix Perry and Rebecca Fiebrink
// Using the MIT License. https://github.com/Interactml
//----

#include "InteractMLExternalModelNode.h"

//unreal
#include "BlueprintActionDatabaseRegistrar.h" //FBlueprintActionDatabaseRegistrar
#include "BlueprintNodeSpawner.h" //UBlueprintNodeSpawner
#include "EdGraphSchema_K2.h" //UEdGraphSchema_K2
#include "KismetCompiler.h" //FKismetCompilerContext
#include "K2Node_CallFunction.h" //UK2Node_Function
#include "Engine/SimpleConstructionScript.h" //USimpleConstructionScript
#include "Kismet2/BlueprintEditorUtils.h" //MarkBlueprintAsStructurallyModified
#include "ToolMenu.h" //UToolMenu
#include "ScopedTransaction.h" //FScopedTransaction

//module
#include "InteractMLModel.h"
#include "InteractMLTrainingSet.h"
#include "InteractMLBlueprintLibrary.h"
#include "InteractMLConstants.h"

// PROLOGUE
#define LOCTEXT_NAMESPACE "InteractML"

// CONSTANTS & MACROS


// LOCAL CLASSES & TYPES


// pin and function name constants
//
namespace FInteractMLExternalModelNodePinNames
{
	//in
	static const FName DataPathInputPinName("Data Path");
	static const FName ModelTypeInputPinName("Model Type");
	//out
	static const FName ModelOutputPinName("Model");
	static const FName IsTrainedOutputPinName("Trained?");
}  	
namespace FInteractMLExternalModelNodeFunctionNames
{
	static const FName GetModelFunctionName(GET_FUNCTION_NAME_CHECKED(UInteractMLBlueprintLibrary, GetModel));
}
//UInteractMLBlueprintLibrary::GetModel(...)
namespace FInteractMLExternalModelNodeModelAccessFunctionPinNames
{
	static const FName ActorPinName("Actor");
	static const FName DataPathPinName("DataPath");
	static const FName ModelTypePinName("ModelType");
	static const FName NodeIDPinName("NodeID");
	static const FName IsTrainedPinName("IsTrained");
}

/////////////////////////////////// HELPERS /////////////////////////////////////



////////////////////// EXTERNAL TRAINING SET NODE CLASS /////////////////////////

// node title
//
FText UInteractMLExternalModelNode::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FString title = LOCTEXT("ExternalModelNodeTitle", "Model").ToString();
	
	//check what's needed
	switch (TitleType)
	{
		case ENodeTitleType::FullTitle:
			title.Append(TEXT("\n"));
			title.Append( LOCTEXT("ExternalModelNodeSubTitle", "External model data file").ToString() );
			break;
			
		case ENodeTitleType::MenuTitle:
		case ENodeTitleType::ListView:
		default:
			title.Append(TEXT(" ("));
			title.Append( LOCTEXT("ExternalModelNodeMenuDesc", "External").ToString() );
			title.Append(TEXT(")"));
			break;
				
		case ENodeTitleType::EditableTitle:
			title = ""; //not editable
			break;
	}
	
	return FText::FromString(title);
}

// node tooltip
//
FText UInteractMLExternalModelNode::GetTooltipText() const
{
	return LOCTEXT("ExternalModelNodeTooltip", "Directly access an external model data file");
}

// custom pins
//
void UInteractMLExternalModelNode::AllocateDefaultPins()
{
	//handle context actor pin
	Super::AllocateDefaultPins();

	//---- Inputs ----
		
	// Which data file to persist training data?	
	UEdGraphPin* data_pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, nullptr, FInteractMLExternalModelNodePinNames::DataPathInputPinName);
	data_pin->PinToolTip = LOCTEXT("ExternalModelNodeDataPathPinTooltip", "Path (optional) and Name to load/save training set data.").ToString();
	
	// What type of model?
	UEdGraphPin* type_pin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Byte, StaticEnum<EInteractMLModelType>(), FInteractMLExternalModelNodePinNames::ModelTypeInputPinName);
	type_pin->PinToolTip = LOCTEXT("ExternalModelNodeModelTypePinTooltip", "The type of the model being referred to by the data path.").ToString();
	GetDefault<UEdGraphSchema_K2>()->SetPinAutogeneratedDefaultValueBasedOnType( type_pin );
	
	//---- Outputs ----
	
	// Resulting model object
	UEdGraphPin* model_pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, UInteractMLModel::StaticClass(), FInteractMLExternalModelNodePinNames::ModelOutputPinName);
	model_pin->PinToolTip = LOCTEXT("ExternalModelNodeOutputPinTooltip", "Machine learning model.").ToString();
	
	//is trained pin
	UEdGraphPin* is_trained_pin = CreatePin( EGPD_Output, UEdGraphSchema_K2::PC_Boolean, nullptr, FInteractMLExternalModelNodePinNames::IsTrainedOutputPinName );
	is_trained_pin->PinToolTip = LOCTEXT( "ExternalModelNodeIsTrainedPinTooltip", "Indicates whether the current model has been trained and capable of running." ).ToString();
	
}

// pin access helpers : inputs
//
UEdGraphPin* UInteractMLExternalModelNode::GetDataPathInputPin() const
{
	UEdGraphPin* Pin = FindPin(FInteractMLExternalModelNodePinNames::DataPathInputPinName);
	check(Pin == NULL || Pin->Direction == EGPD_Input);
	return Pin;
}
UEdGraphPin* UInteractMLExternalModelNode::GetModelTypeInputPin() const
{
	UEdGraphPin* Pin = FindPin(FInteractMLExternalModelNodePinNames::ModelTypeInputPinName);
	check(Pin == NULL || Pin->Direction == EGPD_Input);
	return Pin;
}

// pin access helpers : outputs
//
UEdGraphPin* UInteractMLExternalModelNode::GetModelOutputPin() const
{
	UEdGraphPin* Pin = FindPin( FInteractMLExternalModelNodePinNames::ModelOutputPinName);
	check( Pin == NULL || Pin->Direction == EGPD_Output );
	return Pin;
}
UEdGraphPin* UInteractMLExternalModelNode::GetIsTrainedOutputPin() const
{
	UEdGraphPin* Pin = FindPin( FInteractMLExternalModelNodePinNames::IsTrainedOutputPinName);
	check( Pin == NULL || Pin->Direction == EGPD_Output );
	return Pin;
}

// runtime node operation functionality hookup
//
void UInteractMLExternalModelNode::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph )
{
	Super::ExpandNode( CompilerContext, SourceGraph );
	
	//generate node disambiguation/context
	FString NodeID = NodeGuid.ToString( EGuidFormats::Digits );
	
	//input pins : exec (execution triggered)
	UEdGraphPin* MainExecPin = GetExecPin();
	//input pins : data
	UEdGraphPin* MainDataPathPin = GetDataPathInputPin();
	UEdGraphPin* MainModelTypePin = GetModelTypeInputPin();
	//output pins : exec (execution continues)
	UEdGraphPin* MainThenPin = FindPin( UEdGraphSchema_K2::PN_Then );	
	//output pins : data
	UEdGraphPin* MainModelOutputPin = GetModelOutputPin();
	UEdGraphPin* MainIsTrainedOutputPin = GetIsTrainedOutputPin();
	
	//internal model training fn
	UFunction* AccessFn = FindModelAccessFunction();
	UK2Node_CallFunction* CallAccessFn = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallAccessFn->SetFromFunction( AccessFn );
	CallAccessFn->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation( CallAccessFn, this);
	//training set access fn pins
	UEdGraphPin* AccessFnExecPin = CallAccessFn->GetExecPin();
	UEdGraphPin* AccessFnThenPin = CallAccessFn->GetThenPin();
	UEdGraphPin* AccessFnResultPin = CallAccessFn->GetReturnValuePin();
	UEdGraphPin* AccessFnActorPin = CallAccessFn->FindPinChecked( FInteractMLExternalModelNodeModelAccessFunctionPinNames::ActorPinName );
	UEdGraphPin* AccessFnDataPathPin = CallAccessFn->FindPinChecked( FInteractMLExternalModelNodeModelAccessFunctionPinNames::DataPathPinName );
	UEdGraphPin* AccessFnModelTypePin = CallAccessFn->FindPinChecked( FInteractMLExternalModelNodeModelAccessFunctionPinNames::ModelTypePinName );
	UEdGraphPin* AccessFnNodeIDPin = CallAccessFn->FindPinChecked( FInteractMLExternalModelNodeModelAccessFunctionPinNames::NodeIDPinName );
	UEdGraphPin* AccessFnIsTrainedPin = CallAccessFn->FindPinChecked( FInteractMLExternalModelNodeModelAccessFunctionPinNames::IsTrainedPinName );
	
	//chain functionality together
	CompilerContext.MovePinLinksToIntermediate(*MainExecPin, *AccessFnExecPin);
	CompilerContext.MovePinLinksToIntermediate(*MainThenPin, *AccessFnThenPin);
	
	//hook up train fn pins
	ConnectContextActor(CompilerContext, SourceGraph, AccessFnActorPin);
	CompilerContext.MovePinLinksToIntermediate(*MainDataPathPin, *AccessFnDataPathPin);
	CompilerContext.MovePinLinksToIntermediate(*MainModelTypePin, *AccessFnModelTypePin);
	AccessFnNodeIDPin->DefaultValue = NodeID;
	CompilerContext.MovePinLinksToIntermediate(*MainModelOutputPin, *AccessFnResultPin);
	CompilerContext.MovePinLinksToIntermediate(*MainIsTrainedOutputPin, *AccessFnIsTrainedPin);
	
	//After we are done we break all links to this node (not the internally created one)
	//leaving the newly created internal nodes left to do the work
	BreakAllNodeLinks();
}

// locate function used to train the model
//
UFunction* UInteractMLExternalModelNode::FindModelAccessFunction() const
{
	UClass* LibraryClass = UInteractMLBlueprintLibrary::StaticClass();
	return LibraryClass->FindFunctionByName( FInteractMLExternalModelNodeFunctionNames::GetModelFunctionName );
}

#undef LOCTEXT_NAMESPACE
