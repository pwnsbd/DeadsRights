#include "GameAudioInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

void UGameAudioInstance::OnStart()
{
	Super::OnStart();

	if (!BackgroundMusic)
	{
		BackgroundMusic = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/CryptGameLoop.CryptGameLoop"));
	}

	if (!MenuToGameTransitionSound)
	{
		MenuToGameTransitionSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/BeginGameSound.BeginGameSound"));
	}

	StartBackgroundMusic();

	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UGameAudioInstance::HandlePreLoadMap);
}

void UGameAudioInstance::Shutdown()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	Super::Shutdown();
}

void UGameAudioInstance::HandlePreLoadMap(const FString &MapName)
{
	const UWorld *World = GetWorld();
	if (!World)
	{
		return;
	}

	const FString CurrentMapName = World->GetMapName();
	if (IsMainMenuMap(CurrentMapName) && IsGameLevelMap(MapName) && MenuToGameTransitionSound)
	{
		UGameplayStatics::SpawnSound2D(
			this,
			MenuToGameTransitionSound,
			MenuToGameTransitionVolume,
			1.0f,
			0.0f,
			nullptr,
			true);
	}
}

bool UGameAudioInstance::IsMainMenuMap(const FString &MapName) const
{
	return MapName.Contains(TEXT("MainMenuLevel"));
}

bool UGameAudioInstance::IsGameLevelMap(const FString &MapName) const
{
	return MapName.Contains(TEXT("GameLevel"));
}

void UGameAudioInstance::StartBackgroundMusic()
{
	if (!BackgroundMusic || BackgroundMusicComponent)
	{
		return;
	}

	BackgroundMusicComponent = UGameplayStatics::SpawnSound2D(
		this,
		BackgroundMusic,
		BackgroundMusicVolume,
		1.0f,
		0.0f,
		nullptr,
		true);

	if (BackgroundMusicComponent)
	{
		BackgroundMusicComponent->OnAudioFinished.AddDynamic(this, &UGameAudioInstance::RestartBackgroundMusic);
	}
}

void UGameAudioInstance::RestartBackgroundMusic()
{
	BackgroundMusicComponent = nullptr;
	StartBackgroundMusic();
}
