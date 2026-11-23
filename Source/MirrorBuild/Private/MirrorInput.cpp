#include "MirrorInput.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Equipment/FGBuildGun.h"
#include "Equipment/FGBuildGunBuild.h"
#include "FGCharacterPlayer.h"
#include "Framework/Application/IInputProcessor.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "Hologram/FGHologram.h"
#include "InputCoreTypes.h"
#include "Misc/CoreDelegates.h"
#include "MirrorCore.h"
#include "Widgets/SViewport.h"

namespace
{
	AFGHologram* FindLocalHologram()
	{
		const UGameViewportClient* Viewport = GEngine ? GEngine->GameViewport.Get() : nullptr;
		UWorld* World = Viewport ? Viewport->GetWorld() : nullptr;
		APlayerController* Player = World ? World->GetFirstPlayerController() : nullptr;
		if (!Player || !Player->IsLocalController())
		{
			return nullptr;
		}
		const AFGCharacterPlayer* Character = Cast<AFGCharacterPlayer>(Player->GetPawn());
		AFGBuildGun* Gun = Character ? Character->GetBuildGun() : nullptr;
		if (!Gun || !Gun->IsEquipped() || !Gun->IsInState(EBuildGunState::BGS_BUILD))
		{
			return nullptr;
		}
		const UFGBuildGunStateBuild* State = Cast<UFGBuildGunStateBuild>(Gun->GetBuildGunStateFor(EBuildGunState::BGS_BUILD));
		AFGHologram* Hologram = State ? State->GetHologram() : nullptr;
		return IsValid(Hologram) ? Hologram : nullptr;
	}

	/**
	 * L with no modifier, only while holding something that can be mirrored.
	 * Every other key press goes through untouched.
	 */
	class FMirrorKey : public IInputProcessor
	{
	public:
		virtual void Tick(const float DeltaTime, FSlateApplication& SlateApp, TSharedRef<ICursor> Cursor) override
		{
			AFGHologram* Hologram = FindLocalHologram();
			if (!Hologram)
			{
				return;
			}
			const UClass* BuildClass = Hologram->GetBuildClass();
			if (BuildClass != LastClass.Get())
			{
				LastClass = BuildClass;
				LastHologram.Reset();
				bWanted = false;
			}
			if (!MirrorCore::CanMirror(BuildClass))
			{
				return;
			}
			// An upgrade sits on the old building's transform, so it has to take that building's mirror.
			// Otherwise upgrading a mirrored miner would leave it plain and back to front.
			if (const AActor* Replaced = Hologram->GetUpgradedActor())
			{
				MirrorCore::SetHologramMirrored(Hologram, MirrorCore::IsMirrored(Replaced));
				LastHologram.Reset();
				return;
			}
			if (!bWanted)
			{
				// Left over from aiming at a mirrored building to upgrade it.
				if (MirrorCore::IsMirrored(Hologram))
				{
					MirrorCore::SetHologramMirrored(Hologram, false);
				}
				return;
			}
			// Each placement gets a new hologram, and the mirror stays on like the rotation does.
			if (LastHologram.Get() == Hologram && !MirrorCore::IsMirrored(Hologram))
			{
				UE_LOG(LogMirrorBuild, Display, TEXT("%s lost its mirror while placing, put back"), *Hologram->GetName());
			}
			LastHologram = Hologram;
			MirrorCore::SetHologramMirrored(Hologram, true);
		}

		virtual bool HandleKeyDownEvent(FSlateApplication& SlateApp, const FKeyEvent& KeyEvent) override
		{
			if (KeyEvent.GetKey() != EKeys::L || KeyEvent.IsRepeat() || KeyEvent.IsControlDown()
				|| KeyEvent.IsShiftDown() || KeyEvent.IsAltDown())
			{
				return false;
			}
			// Typing in chat, search or any other window moves the focus off the game.
			const TSharedPtr<SViewport> GameView = SlateApp.GetGameViewport();
			if (!GameView.IsValid() || !GameView->HasKeyboardFocus())
			{
				return false;
			}
			AFGHologram* Hologram = FindLocalHologram();
			if (!Hologram || !MirrorCore::CanMirror(Hologram->GetBuildClass()))
			{
				return false;
			}
			if (Hologram->GetUpgradedActor())
			{
				return true; // an upgrade always keeps the old building's mirror
			}
			MirrorCore::Toggle(Hologram);
			bWanted = MirrorCore::IsMirrored(Hologram);
			LastClass = Hologram->GetBuildClass().Get();
			LastHologram = Hologram;
			UE_LOG(LogMirrorBuild, Display, TEXT("Mirror %s %s: scroll=%d locked=%d transform=%s"),
				*GetNameSafe(LastClass.Get()), bWanted ? TEXT("on") : TEXT("off"), Hologram->GetScrollRotateValue(),
				Hologram->IsHologramLocked() ? 1 : 0, *Hologram->GetActorTransform().ToString());
			return true;
		}

	private:
		TWeakObjectPtr<const UClass> LastClass;
		TWeakObjectPtr<AFGHologram> LastHologram;
		bool bWanted = false;
	};
}

void FMirrorInput::Register()
{
	// Slate isn't up yet when the module starts, and never is on a dedicated server.
	FCoreDelegates::OnPostEngineInit.AddLambda([]()
	{
		if (FSlateApplication::IsInitialized() && !IsRunningDedicatedServer())
		{
			FSlateApplication::Get().RegisterInputPreProcessor(MakeShared<FMirrorKey>());
			UE_LOG(LogMirrorBuild, Display, TEXT("Mirror key registered: L"));
		}
	});
}
