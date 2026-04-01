#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MazeRunner.generated.h"

class UStaticMeshComponent;
class ACubeToSphere;

/**
 * desc : Delegate signature for when the AI runner successfully finishes navigating its current path.
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPathCompletedSignature);

/**
 * AMazeRunner
 * Job: Acts as the persistent AI agent that navigates the sphere maze.
 * - Receives local-space paths from the Orchestrator.
 * - Mathematically hugs the spherical surface while moving.
 * - Broadcasts an event when it reaches its destination to request a new target.
 */
UCLASS()
class GAME_API AMazeRunner : public AActor
{
	GENERATED_BODY()

public:
	// =========================================================
	// Functions (Public)
	// =========================================================

	/**
	 * desc : Default constructor. Initializes the mesh component and sets default scale.
	 * args : None
	 * result: None
	 */
	AMazeRunner();

	/**
	 * desc : Called every frame. Handles local space movement, spherical surface alignment, and waypoint tracking.
	 * args : DeltaTime - The time elapsed since the last frame.
	 * result: None
	 */
	virtual void Tick(float DeltaTime) override;

	/**
	 * desc : Hands the calculated local-space path to the AI so it can begin moving.
	 * args :
	 * - NewLocalPath: Array of waypoints relative to the sphere's local space (center 0,0,0).
	 * - InSphereActor: Pointer to the spherical planet so the runner can track it.
	 * result: None
	 */
	void SetPath(const TArray<FVector> &NewLocalPath, ACubeToSphere *InSphereActor);

	// =========================================================
	// Events & Components (Public)
	// =========================================================

	/** Event dispatcher triggered when the runner reaches the final waypoint in its path. */
	UPROPERTY(BlueprintAssignable)
	FOnPathCompletedSignature OnPathCompleted;

protected:
	/** The visual representation of the runner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent *MeshComp;

	/** How fast the runner moves along the path (Units per second). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement")
	float MovementSpeed = 300.0f;

private:
	// =========================================================
	// Internal State (Private)
	// =========================================================

	/** The current path the runner is following, mapped in local space. */
	TArray<FVector> PathToFollow;

	/** The index of the specific waypoint the runner is currently walking towards. */
	int32 CurrentTargetIndex = 0;

	/** State flag controlling whether the Tick function should process movement. */
	bool bIsMoving = false;

	/** Pointer to the active sphere planet. Used to validate references. */
	ACubeToSphere *TargetSphere = nullptr;
};
