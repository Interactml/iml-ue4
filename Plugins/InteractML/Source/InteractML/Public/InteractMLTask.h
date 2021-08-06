//----
// InteractML - University Of Arts London
//----

#pragma once

//unreal
#include "CoreMinimal.h"

//module
#include "InteractMLHelpers.h"
#include "InteractMLModel.h"

//general declarations

//task type
enum class EInteractMLTaskType
{
	None,
	Train,
	Run,
};

// InteractML Task
// Base for encapsulation of training or running a model, and any state they may need
// Can be dispatched off to other threads to run asynchronously if needed
// Monolithic (rather then inheritance hierarchy) to make handling of results simpler
//
struct FInteractMLTask : public TSharedFromThis<FInteractMLTask>
{
	//////////////// INPUT //////////////

	//type of operation
	EInteractMLTaskType Type;

	//model to operate on
	class UInteractMLModel* Model;

	//model state that running operates in the context of
	class FInteractMLModelState* Context;

	//training input
	std::vector<trainingExampleFloat> Examples;

	//run inputs (single sampling)
	std::vector<float> Inputs;

	//run inputs (series sampling)
	std::vector<std::vector<float>> InputSeries;

	//////////////// OUTPUT //////////////

	//successful operation?
	bool bSuccess;

	//run output values
	TArray<float> Outputs;	


	//////////////// EXECUTION //////////////

	FInteractMLTask(UInteractMLModel* model, EInteractMLTaskType action)
		: Type(action)
		, Model(model)
	{
	}
	
	//main operation (on background thread)
	void Run();

	//apply results (on main thread)
	void Apply();

};
