#pragma once
#include "CoreMinimal.h"
#include "ItemStorageTypes.generated.h"

class AActor;

// Generic item category tag.
// Values intentionally match EArtifactType in Artifact.h so
// AddArtifactToInventory() can static_cast between them safely.
UENUM(BlueprintType)
enum class EItemCategory : uint8
{
	None        UMETA(DisplayName = "None"),
	Beam        UMETA(DisplayName = "Beam"),
	PhaseWalk   UMETA(DisplayName = "Phase Walk"),
	PathFinder  UMETA(DisplayName = "Path Finder"),
	Barrier     UMETA(DisplayName = "Barrier")
};

USTRUCT(BlueprintType)
struct GAME_API FStoredItem
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	EItemCategory ItemCategory = EItemCategory::None;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	FString ItemName = TEXT("Empty");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	FString Description = TEXT("");

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	float CooldownDuration = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	float NextUsableTime = 0.f;

	// Wave index in which this item was picked up.
	// Stamped from UItemStorageComponent::CurrentWaveIndex at the time of pickup.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 WaveIndex = 0;

	// The artifact actor kept alive (hidden) in the world so its ability can be activated on use.
	// Destroyed when the slot is cleared (depleted, dropped, or game reset).
	UPROPERTY()
	TObjectPtr<AActor> SourceActor = nullptr;

	bool IsEmpty() const { return ItemCategory == EItemCategory::None; }
};
