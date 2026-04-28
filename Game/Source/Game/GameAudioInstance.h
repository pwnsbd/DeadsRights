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

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void CrossfadeToMenuMusic();

	UFUNCTION(BlueprintCallable, Category = "Audio")
	void CrossfadeToGameMusic();

protected:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase *MenuMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	USoundBase *GameMusic = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float MenuMusicVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float GameMusicVolume = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	float MusicCrossfadeDuration = 2.0f;

private:
	UPROPERTY()
	UAudioComponent *MenuMusicComponent = nullptr;

	UPROPERTY()
	UAudioComponent *GameMusicComponent = nullptr;

	float CurrentMenuMusicVolume = 1.0f;
	float CurrentGameMusicVolume = 0.001f;

	void HandlePreLoadMap(const FString &MapName);
	void HandlePostLoadMap(UWorld *LoadedWorld);
	UFUNCTION()
	void RestartMenuMusic();

	UFUNCTION()
	void RestartGameMusic();
	void StartMenuMusic();
	void StartGameMusic();
	void ForceGameMusicAudible();
	bool IsMainMenuMap(const FString &MapName) const;
	bool IsGameLevelMap(const FString &MapName) const;
};
