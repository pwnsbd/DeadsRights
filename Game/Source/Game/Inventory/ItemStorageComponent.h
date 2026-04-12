#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemStorageTypes.h"
#include "ItemStorageComponent.generated.h"

// Fired when an item is successfully added. SlotIndex is where it landed.
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemAdded,  const FStoredItem&, Item, int32, SlotIndex);
// Fired when an item slot is used (cooldown triggered).
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnItemUsed,   const FStoredItem&, Item, int32, SlotIndex);
// Fired when a pickup is rejected because all slots are full.
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryFull);

/**
 * Generic item storage component ("backpack").
 * Attach to any actor that needs to carry items.
 * Tracks a full pickup history for wave counts and randomization queries.
 */
UCLASS(ClassGroup = (Custom), meta = (BlueprintSpawnableComponent))
class GAME_API UItemStorageComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UItemStorageComponent();

	// ---- Configuration ------------------------------------------------

	// Maximum number of simultaneously held items.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 MaxSlots = 4;

	// Set by external systems (e.g. Orchestrator::TriggerNextRun) each wave.
	// Stamped onto FStoredItem::WaveIndex at the moment of pickup.
	UPROPERTY(BlueprintReadWrite, Category = "Inventory")
	int32 CurrentWaveIndex = 0;

	// ---- State --------------------------------------------------------

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FStoredItem> Slots;

	// ---- Delegates ----------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnItemAdded OnItemAdded;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnItemUsed OnItemUsed;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryFull OnInventoryFull;

	// ---- Core API -----------------------------------------------------

	// Adds an item. Returns the slot index (>= 0) on success, INDEX_NONE if full.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 AddItem(const FStoredItem& Item);

	// Uses the item in a slot (starts/checks cooldown). Returns false if empty or on cooldown.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool UseSlot(int32 SlotIndex);

	// Clears a slot (item consumed, dropped, etc.).
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ClearSlot(int32 SlotIndex);

	// Clears all slots — called on game loss to reset the player's inventory.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void ClearAllSlots();

	// ---- Query / Stats API --------------------------------------------

	// Total items ever picked up (lifetime, never resets).
	UFUNCTION(BlueprintCallable, Category = "Inventory|Stats")
	int32 GetTotalItemsPickedUp() const;

	// Items picked up during a specific wave.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Stats")
	int32 GetItemsPickedUpInWave(int32 WaveIndex) const;

	// Number of currently occupied slots.
	UFUNCTION(BlueprintCallable, Category = "Inventory|Stats")
	int32 GetCurrentItemCount() const;

	// True if at least one slot is empty.
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasRoom() const;

	// Returns a random occupied slot index, or INDEX_NONE if all slots empty.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 GetRandomOccupiedSlot() const;

	// Returns indices of all slots matching the given category.
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	TArray<int32> GetSlotsByCategory(EItemCategory Category) const;

	UFUNCTION(BlueprintCallable, Category = "Inventory|Debug")
	void LogState(const FString& Context) const;

protected:
	virtual void BeginPlay() override;

private:
	// Complete history of every item ever added — never cleared.
	// Backs GetTotalItemsPickedUp() and GetItemsPickedUpInWave().
	UPROPERTY()
	TArray<FStoredItem> PickupHistory;
};
