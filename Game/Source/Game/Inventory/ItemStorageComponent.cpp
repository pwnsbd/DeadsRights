#include "ItemStorageComponent.h"

UItemStorageComponent::UItemStorageComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UItemStorageComponent::BeginPlay()
{
	Super::BeginPlay();
	Slots.SetNum(MaxSlots); // initialises each element to a default (empty) FStoredItem
}

// ---- Core API ---------------------------------------------------------------

int32 UItemStorageComponent::AddItem(const FStoredItem& Item)
{
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].IsEmpty())
		{
			Slots[i]           = Item;
			Slots[i].WaveIndex = CurrentWaveIndex;

			PickupHistory.Add(Slots[i]);
			OnItemAdded.Broadcast(Slots[i], i);

			UE_LOG(LogTemp, Log, TEXT("[ItemStorage] Added '%s' to slot %d (wave %d)"),
				*Item.ItemName, i, CurrentWaveIndex);
			return i;
		}
	}

	OnInventoryFull.Broadcast();
	UE_LOG(LogTemp, Warning, TEXT("[ItemStorage] Inventory full — could not add '%s'"), *Item.ItemName);
	return INDEX_NONE;
}

bool UItemStorageComponent::UseSlot(int32 SlotIndex)
{
	if (!Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemStorage] UseSlot: invalid index %d"), SlotIndex);
		return false;
	}

	FStoredItem& Slot = Slots[SlotIndex];
	if (Slot.IsEmpty())
	{
		UE_LOG(LogTemp, Log, TEXT("[ItemStorage] UseSlot %d: slot is empty"), SlotIndex);
		return false;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < Slot.NextUsableTime)
	{
		const float Remaining = Slot.NextUsableTime - Now;
		UE_LOG(LogTemp, Log, TEXT("[ItemStorage] UseSlot %d: '%s' on cooldown (%.1fs remaining)"),
			SlotIndex, *Slot.ItemName, Remaining);
		return false;
	}

	Slot.NextUsableTime = Now + Slot.CooldownDuration;
	OnItemUsed.Broadcast(Slot, SlotIndex);

	UE_LOG(LogTemp, Log, TEXT("[ItemStorage] Used '%s' in slot %d (cooldown %.1fs)"),
		*Slot.ItemName, SlotIndex, Slot.CooldownDuration);
	return true;
}

void UItemStorageComponent::ClearSlot(int32 SlotIndex)
{
	if (Slots.IsValidIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("[ItemStorage] Cleared slot %d ('%s')"),
			SlotIndex, *Slots[SlotIndex].ItemName);
		Slots[SlotIndex] = FStoredItem{};
	}
}

// ---- Query / Stats ----------------------------------------------------------

int32 UItemStorageComponent::GetTotalItemsPickedUp() const
{
	return PickupHistory.Num();
}

int32 UItemStorageComponent::GetItemsPickedUpInWave(int32 WaveIndex) const
{
	int32 Count = 0;
	for (const FStoredItem& Item : PickupHistory)
	{
		if (Item.WaveIndex == WaveIndex)
			++Count;
	}
	return Count;
}

int32 UItemStorageComponent::GetCurrentItemCount() const
{
	int32 Count = 0;
	for (const FStoredItem& Slot : Slots)
	{
		if (!Slot.IsEmpty())
			++Count;
	}
	return Count;
}

bool UItemStorageComponent::HasRoom() const
{
	for (const FStoredItem& Slot : Slots)
	{
		if (Slot.IsEmpty())
			return true;
	}
	return false;
}

int32 UItemStorageComponent::GetRandomOccupiedSlot() const
{
	TArray<int32> OccupiedIndices;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (!Slots[i].IsEmpty())
			OccupiedIndices.Add(i);
	}

	if (OccupiedIndices.IsEmpty())
		return INDEX_NONE;

	return OccupiedIndices[FMath::RandRange(0, OccupiedIndices.Num() - 1)];
}

TArray<int32> UItemStorageComponent::GetSlotsByCategory(EItemCategory Category) const
{
	TArray<int32> Result;
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		if (Slots[i].ItemCategory == Category)
			Result.Add(i);
	}
	return Result;
}

void UItemStorageComponent::LogState(const FString& Context) const
{
	UE_LOG(LogTemp, Log, TEXT("[ItemStorage] --- State (%s) ---"), *Context);
	for (int32 i = 0; i < Slots.Num(); ++i)
	{
		const FStoredItem& Slot = Slots[i];
		if (Slot.IsEmpty())
		{
			UE_LOG(LogTemp, Log, TEXT("  Slot %d: [empty]"), i);
		}
		else
		{
			const float Now       = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
			const float Remaining = FMath::Max(0.f, Slot.NextUsableTime - Now);
			UE_LOG(LogTemp, Log, TEXT("  Slot %d: %s | Cooldown: %.1fs remaining"),
				i, *Slot.ItemName, Remaining);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("  Total picked up: %d | Current wave: %d"),
		PickupHistory.Num(), CurrentWaveIndex);
}
