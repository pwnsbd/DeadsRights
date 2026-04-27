#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "GameAudioInstance.generated.h"

class UAudioComponent;
class USoundBase;

UCLASS()
class GAME_API UGameAudioInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void OnStart() override;
	virtual void Shutdown() override;

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase *BackgroundMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float BackgroundMusicVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase *MenuToGameTransitionSound = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float MenuToGameTransitionVolume = 1.0f;

private:
	UPROPERTY()
	UAudioComponent *BackgroundMusicComponent = nullptr;

	void HandlePreLoadMap(const FString &MapName);
	void RestartBackgroundMusic();
	void StartBackgroundMusic();
	bool IsMainMenuMap(const FString &MapName) const;
	bool IsGameLevelMap(const FString &MapName) const;
};
