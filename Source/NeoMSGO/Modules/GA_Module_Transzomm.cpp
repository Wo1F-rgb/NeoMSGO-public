// Copyright NeoMSGO Project. All Rights Reserved.

#include "Modules/GA_Module_Transzomm.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Audio/MSAudioPlayback.h"
#include "Audio/MSAudioPreset.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSBoostComponent.h"
#include "Combat/MSModuleGameplayEffects.h"
#include "Combat/MSUnitBase.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSModuleData.h"
#include "Player/MSCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UGA_Module_Transzomm::UGA_Module_Transzomm()
{
	InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
	static ConstructorHelpers::FObjectFinder<USoundBase> ActivateFinder(
		TEXT("/Game/Audio/Imported/SFX_ModuleActivate.SFX_ModuleActivate"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SortieFinder(
		TEXT("/Game/Audio/Imported/SFX_SortieTransition.SFX_SortieTransition"));
	ModuleActivateSound = ActivateFinder.Object;
	SortieTransitionSound = SortieFinder.Object;
}

void UGA_Module_Transzomm::BeginDestroy()
{
	CleanupActiveModule(false);
	Super::BeginDestroy();
}

bool UGA_Module_Transzomm::TryActivateModule(AActor* ModuleOwner, const UMSModuleData* Data)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ModuleOwner);
	if (!ModuleOwner || !Data || !ASC || ActiveASC.IsValid())
	{
		return false;
	}

	FGameplayTagContainer BlockedTags;
	BlockedTags.AddTag(MSTags::State_Module_EffectActive);
	BlockedTags.AddTag(MSTags::State_Module_Cooldown);
	BlockedTags.AddTag(MSTags::State_Overheated);
	BlockedTags.AddTag(MSTags::State_Dead);
	if (ASC->HasAnyMatchingGameplayTags(BlockedTags))
	{
		return false;
	}

	// Register before applying so even an externally removed, very short effect cannot race cleanup.
	ActiveOwner = ModuleOwner;
	ActiveASC = ASC;
	ActiveData = Data;
	EffectTagEventHandle = ASC->RegisterGameplayTagEvent(MSTags::State_Module_EffectActive,
		EGameplayTagEventType::NewOrRemoved).AddUObject(this, &UGA_Module_Transzomm::HandleEffectTagChanged);
	DeadTagEventHandle = ASC->RegisterGameplayTagEvent(MSTags::State_Dead,
		EGameplayTagEventType::NewOrRemoved).AddUObject(this, &UGA_Module_Transzomm::HandleDeadTagChanged);

	const bool bEffectApplied = ApplyDurationEffect(UGE_ModuleEffect::StaticClass(),
		MSTags::Data_Module_Duration, Data->EffectDurationSeconds);
	const bool bCooldownApplied = ApplyDurationEffect(UGE_ModuleCooldown::StaticClass(),
		MSTags::Data_Module_Cooldown, Data->CooldownSeconds);
	if (!bEffectApplied || !bCooldownApplied)
	{
		FGameplayTagContainer ModuleTags;
		ModuleTags.AddTag(MSTags::State_Module_EffectActive);
		ModuleTags.AddTag(MSTags::State_Module_Cooldown);
		ASC->RemoveActiveEffectsWithGrantedTags(ModuleTags);
		CleanupActiveModule(false);
		return false;
	}

	if (ACharacter* Character = Cast<ACharacter>(ModuleOwner))
	{
		if (UMSCharacterMovementComponent* Movement =
			Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			Movement->SetModuleSpeedMultiplier(Data->SpeedMultiplier);
		}
	}
	if (UMSBoostComponent* Boost = ModuleOwner->FindComponentByClass<UMSBoostComponent>())
	{
		Boost->SetModuleConsumptionMultiplier(Data->BoostConsumptionMultiplier);
	}
	if (AMSUnitBase* Unit = Cast<AMSUnitBase>(ModuleOwner))
	{
		if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
		{
			Animator->SetModuleAfterimageOverride(Data->bEnableAfterimage, Data->AfterimageColor);
			Animator->SetModuleBodyAura(true, Data->AfterimageColor);
		}
	}
	MSAudio::PlayMetaSound2D(ModuleOwner,
		MSAudioPreset::Resolve(EMSAudioUsage::ModuleActivate, ModuleActivateSound), 1000.0f, 0.55f, 0.30f);
	return true;
}

void UGA_Module_Transzomm::PlaySortieTransition(UObject* WorldContextObject)
{
	const UGA_Module_Transzomm* Defaults = GetDefault<UGA_Module_Transzomm>();
	MSAudio::PlayMetaSound2D(WorldContextObject,
		MSAudioPreset::Resolve(EMSAudioUsage::Sortie, Defaults->SortieTransitionSound),
		1000.0f, 0.70f, 0.60f);
}

bool UGA_Module_Transzomm::ApplyDurationEffect(const TSubclassOf<UGameplayEffect> EffectClass,
	const FGameplayTag& DurationTag, const float DurationSeconds) const
{
	UAbilitySystemComponent* ASC = ActiveASC.Get();
	if (!ASC || DurationSeconds <= 0.0f)
	{
		return false;
	}
	const FGameplayEffectSpecHandle Spec = ASC->MakeOutgoingSpec(EffectClass, 1.0f, ASC->MakeEffectContext());
	if (!Spec.IsValid())
	{
		return false;
	}
	Spec.Data->SetSetByCallerMagnitude(DurationTag, DurationSeconds);
	return ASC->ApplyGameplayEffectSpecToSelf(*Spec.Data).IsValid();
}

void UGA_Module_Transzomm::HandleEffectTagChanged(const FGameplayTag, const int32 NewCount)
{
	if (NewCount == 0 && ActiveASC.IsValid())
	{
		const bool bAlive = !ActiveASC->HasMatchingGameplayTag(MSTags::State_Dead);
		CleanupActiveModule(bAlive);
	}
}

void UGA_Module_Transzomm::HandleDeadTagChanged(const FGameplayTag, const int32 NewCount)
{
	UAbilitySystemComponent* ASC = ActiveASC.Get();
	if (NewCount > 0 && ASC)
	{
		FGameplayTagContainer ModuleTags;
		ModuleTags.AddTag(MSTags::State_Module_EffectActive);
		ModuleTags.AddTag(MSTags::State_Module_Cooldown);
		ASC->RemoveActiveEffectsWithGrantedTags(ModuleTags);
	}
}

void UGA_Module_Transzomm::CleanupActiveModule(const bool bApplyForcedOverheat)
{
	UAbilitySystemComponent* ASC = ActiveASC.Get();
	AActor* Owner = ActiveOwner.Get();
	const UMSModuleData* Data = ActiveData.Get();
	if (ASC && EffectTagEventHandle.IsValid())
	{
		ASC->RegisterGameplayTagEvent(MSTags::State_Module_EffectActive,
			EGameplayTagEventType::NewOrRemoved).Remove(EffectTagEventHandle);
	}
	if (ASC && DeadTagEventHandle.IsValid())
	{
		ASC->RegisterGameplayTagEvent(MSTags::State_Dead,
			EGameplayTagEventType::NewOrRemoved).Remove(DeadTagEventHandle);
	}
	EffectTagEventHandle.Reset();
	DeadTagEventHandle.Reset();

	if (ACharacter* Character = Cast<ACharacter>(Owner))
	{
		if (UMSCharacterMovementComponent* Movement =
			Cast<UMSCharacterMovementComponent>(Character->GetCharacterMovement()))
		{
			Movement->SetModuleSpeedMultiplier(1.0f);
		}
	}
	if (Owner)
	{
		if (UMSBoostComponent* Boost = Owner->FindComponentByClass<UMSBoostComponent>())
		{
			Boost->SetModuleConsumptionMultiplier(1.0f);
		}
		if (AMSUnitBase* Unit = Cast<AMSUnitBase>(Owner))
		{
			if (UMSMechAnimatorComponent* Animator = Unit->GetMechAnimatorComponent())
			{
				Animator->SetModuleAfterimageOverride(false, FLinearColor::Transparent);
				Animator->SetModuleBodyAura(false, FLinearColor::Transparent);
			}
		}
	}

	if (bApplyForcedOverheat && ASC && Data)
	{
		if (UMSAttributeSet_Boost* BoostSet = const_cast<UMSAttributeSet_Boost*>(ASC->GetSet<UMSAttributeSet_Boost>()))
		{
			BoostSet->SetBoost(0.0f);
		}
		ActiveASC = ASC; // ApplyDurationEffect reads the active ASC.
		ApplyDurationEffect(UGE_ModuleForcedOverheat::StaticClass(),
			MSTags::Data_Module_OverheatDuration, Data->ForcedOverheatSeconds);
	}

	ActiveOwner.Reset();
	ActiveASC.Reset();
	ActiveData.Reset();
}
