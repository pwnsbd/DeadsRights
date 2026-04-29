#include "GameAudioInstance.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

void UGameAudioInstance::OnStart()
{
	Super::OnStart();

	if (!MenuMusic)
	{
		MenuMusic = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/CryptGameLoop.CryptGameLoop"));
	}

	if (!GameMusic)
	{
		GameMusic = LoadObject<USoundBase>(nullptr, TEXT("/Game/sound/crypt_game_default_rough_mix_ver_1_1.crypt_game_default_rough_mix_ver_1_1"));
	}

	StartMenuMusic();
	StartGameMusic();

	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PreLoadMap.AddUObject(this, &UGameAudioInstance::HandlePreLoadMap);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &UGameAudioInstance::HandlePostLoadMap);
}

void UGameAudioInstance::Shutdown()
{
	FCoreUObjectDelegates::PreLoadMap.RemoveAll(this);
	FCoreUObjectDelegates::PostLoadMapWithWorld.RemoveAll(this);
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
	if (IsMainMenuMap(CurrentMapName) && IsGameLevelMap(MapName))
	{
		CrossfadeToGameMusic();
	}
}

void UGameAudioInstance::HandlePostLoadMap(UWorld *LoadedWorld)
{
	if (LoadedWorld && IsGameLevelMap(LoadedWorld->GetMapName()))
	{
		ForceGameMusicAudible();
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

void UGameAudioInstance::StartMenuMusic()
{
	if (!MenuMusic || MenuMusicComponent)
	{
		return;
	}

	MenuMusicComponent = UGameplayStatics::SpawnSound2D(
		this,
		MenuMusic,
		CurrentMenuMusicVolume,
		1.0f,
		0.0f,
		nullptr,
		true);

	if (MenuMusicComponent)
	{
		MenuMusicComponent->OnAudioFinished.AddDynamic(this, &UGameAudioInstance::RestartMenuMusic);
	}
}

void UGameAudioInstance::RestartMenuMusic()
{
	MenuMusicComponent = nullptr;
	if (CurrentMenuMusicVolume > 0.0f)
	{
		StartMenuMusic();
	}
}

void UGameAudioInstance::StartGameMusic()
{
	if (!GameMusic || GameMusicComponent)
	{
		return;
	}

	GameMusicComponent = UGameplayStatics::SpawnSound2D(
		this,
		GameMusic,
		CurrentGameMusicVolume,
		1.0f,
		0.0f,
		nullptr,
		true);

	if (GameMusicComponent)
	{
		GameMusicComponent->OnAudioFinished.AddDynamic(this, &UGameAudioInstance::RestartGameMusic);
	}
}

void UGameAudioInstance::RestartGameMusic()
{
	GameMusicComponent = nullptr;
	if (CurrentGameMusicVolume > 0.0f)
	{
		StartGameMusic();
	}
}

void UGameAudioInstance::CrossfadeToGameMusic()
{
	CurrentMenuMusicVolume = 0.0f;
	CurrentGameMusicVolume = GameMusicVolume;

	StartMenuMusic();
	StartGameMusic();

	if (MenuMusicComponent)
	{
		MenuMusicComponent->AdjustVolume(MusicCrossfadeDuration, 0.0f);
	}

	if (GameMusicComponent)
	{
		GameMusicComponent->AdjustVolume(MusicCrossfadeDuration, GameMusicVolume);
	}
}

void UGameAudioInstance::CrossfadeToMenuMusic()
{
	CurrentMenuMusicVolume = MenuMusicVolume;
	CurrentGameMusicVolume = 0.0f;

	StartMenuMusic();

	if (MenuMusicComponent)
	{
		if (!MenuMusicComponent->IsPlaying())
		{
			MenuMusicComponent->Play();
		}
		MenuMusicComponent->AdjustVolume(MusicCrossfadeDuration, MenuMusicVolume);
	}

	if (GameMusicComponent)
	{
		GameMusicComponent->FadeOut(MusicCrossfadeDuration, 0.0f);
	}
}

void UGameAudioInstance::ForceGameMusicAudible()
{
	CurrentMenuMusicVolume = 0.0f;
	CurrentGameMusicVolume = GameMusicVolume;

	if (GameMusicComponent)
	{
		GameMusicComponent->Stop();
		GameMusicComponent = nullptr;
	}

	CurrentGameMusicVolume = 0.001f;
	StartGameMusic();

	if (MenuMusicComponent)
	{
		MenuMusicComponent->AdjustVolume(MusicCrossfadeDuration, 0.0f);
	}

	if (GameMusicComponent)
	{
		if (!GameMusicComponent->IsPlaying())
		{
			GameMusicComponent->Play();
		}
		CurrentGameMusicVolume = GameMusicVolume;
		GameMusicComponent->AdjustVolume(MusicCrossfadeDuration, GameMusicVolume);
	}
}
