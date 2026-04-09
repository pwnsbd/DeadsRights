#pragma once
#include "CoreMinimal.h"
#include "ItemStorageTypes.generated.h"

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
	float CooldownDuration = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	float NextUsableTime = 0.f;

	// Wave index in which this item was picked up.
	// Stamped from UItemStorageComponent::CurrentWaveIndex at the time of pickup.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 WaveIndex = 0;

	bool IsEmpty() const { return ItemCategory == EItemCategory::None; }
};
