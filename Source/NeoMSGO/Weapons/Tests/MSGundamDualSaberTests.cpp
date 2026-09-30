// Copyright NeoMSGO Project. All Rights Reserved.
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include <initializer_list>
#include "AbilitySystemComponent.h"
#include "Animation/MSDualSaberMotion.h"
#include "Animation/MSMechAnimatorComponent.h"
#include "Animation/MSMechRigComponent.h"
#include "Animation/MSMeshVisualComponent.h"
#include "Combat/MSAttributeSet_Vital.h"
#include "Combat/MSAttributeSet_Boost.h"
#include "Combat/MSDamageGameplayEffects.h"
#include "Combat/MSGameplayEffectContext.h"
#include "Combat/MSHitReactionComponent.h"
#include "Combat/MSShieldComponent.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/MSGameplayTags.h"
#include "Core/MSScale.h"
#include "Components/BoxComponent.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Player/MSPlayerCharacter.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/GA_Melee.h"
#include "Weapons/MSWeaponComponent.h"
#include "Weapons/MSWeaponVisualComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGundamDualSaberTest,
	"NeoMSGO.Weapons.GundamDualSaber.RuntimeAndImportedBlades",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSGundamDualSaberMotionTest,
	"NeoMSGO.Animation.GundamDualSaber.DirectionsAndTiming",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSGundamDualSaberMotionTest::RunTest(const FString& Parameters)
{
	for (int32 Stage = 10; Stage <= 14; ++Stage)
	{
		const auto T = FMSStandardSaberMotion::GetTiming(Stage);
		TestTrue(TEXT("ordered startup, active and recovery"), T.ActiveStart > 0 && T.ActiveEnd > T.ActiveStart && T.Duration > T.ActiveEnd);
		const float ExpectedWorldCm = Stage == 14 ? 6000.0f : (Stage == 10 ? 400.0f : 200.0f);
		TestTrue(TEXT("configured forward travel uses the intended environment distance once"),
			FMath::IsNearlyEqual(T.LungeDistanceCm * MSGO::MSScale, ExpectedWorldCm, .01f));
		TestTrue(TEXT("normal follow-up travel remains smaller than the first stage"), Stage == 10 || Stage == 14 || T.LungeDistanceCm < FMSStandardSaberMotion::GetTiming(10).LungeDistanceCm);
		const auto Begin = FMSStandardSaberMotion::Sample(Stage,T.ActiveStart);
		const auto End = FMSStandardSaberMotion::Sample(Stage,T.ActiveEnd);
		auto Dir = [](const FMSStandardSaberPose& P, bool L)
		{ return (L ? P.UpperArmLeft * P.ForearmLeft * P.WeaponMountLeft : P.UpperArm * P.Forearm * P.WeaponMount).RotateVector(FVector::ForwardVector); };
		if (Stage == 10) TestTrue(TEXT("right hand cuts left-low to right-high"), Dir(Begin,false).Y < 0 && Dir(Begin,false).Z < 0 && Dir(End,false).Y > 0 && Dir(End,false).Z > 0);
		if (Stage == 11) TestTrue(TEXT("left hand cuts right-low to left-high"), Dir(Begin,true).Y > 0 && Dir(Begin,true).Z < 0 && Dir(End,true).Y < 0 && Dir(End,true).Z > 0);
		if (Stage == 12) TestTrue(TEXT("both blades close inward"), Dir(Begin,false).Y > .5 && Dir(Begin,true).Y < -.5 && Dir(End,false).Y < 0 && Dir(End,true).Y > 0);
		if (Stage == 13) TestTrue(TEXT("both blades rise outward"), Dir(Begin,false).Z < 0 && Dir(Begin,true).Z < 0 && Dir(End,false).Y > 0 && Dir(End,true).Y < 0 && Dir(End,false).Z > 0 && Dir(End,true).Z > 0);
		if (Stage == 14)
		{
			float Previous = Begin.BodyYaw;
			for (int32 I=1; I<=60; ++I)
			{
				const auto P = FMSStandardSaberMotion::Sample(Stage,FMath::Lerp(T.ActiveStart,T.ActiveEnd,I/60.0f));
				TestTrue(TEXT("clockwise spin progresses continuously"), P.BodyYaw > Previous);
				Previous = P.BodyYaw;
			}
			TestTrue(TEXT("D completes a full turn"), FMath::IsNearlyEqual(End.BodyYaw-Begin.BodyYaw,360.0f,.01f));
		}
		const auto Ready = FMSStandardSaberMotion::Sample(FMSStandardSaberMotion::DualReady,0);
		const auto Terminal = FMSStandardSaberMotion::Sample(Stage,T.Duration);
		TestTrue(TEXT("both hands recover to ready"), Terminal.UpperArm.Equals(Ready.UpperArm,.001f) && Terminal.UpperArmLeft.Equals(Ready.UpperArmLeft,.001f));
		TestTrue(TEXT("torso recovers without reversed full spin"), Terminal.Torso.Equals(Ready.Torso,.001f));
	}
	TestFalse(TEXT("GM pose is not dual"), FMSStandardSaberMotion::Sample(0,.2f).bDual);
	TestTrue(TEXT("GM second stage remains terminal"), !FMSStandardSaberMotion::HasFollowUp(1));
	return true;
}

bool FMSGundamDualSaberTest::RunTest(const FString& Parameters)
{
	UMSMeleeWeaponData* Source = LoadObject<UMSMeleeWeaponData>(nullptr,TEXT("/Game/Data/Weapons/DA_Weapon_MPU01_BeamSaber.DA_Weapon_MPU01_BeamSaber"));
	if (!TestNotNull(TEXT("existing detailed saber asset"),Source)) return false;
	UMSMeleeWeaponData* N = LoadObject<UMSMeleeWeaponData>(nullptr,TEXT("/Game/Data/Weapons/DA_Weapon_Gundam_DualBeamSaber_N.DA_Weapon_Gundam_DualBeamSaber_N"));
	UMSMeleeWeaponData* D = LoadObject<UMSMeleeWeaponData>(nullptr,TEXT("/Game/Data/Weapons/DA_Weapon_Gundam_DualBeamSaber_D.DA_Weapon_Gundam_DualBeamSaber_D"));
	if (!TestNotNull(TEXT("N dual saber asset"),N) || !TestNotNull(TEXT("D dual saber asset"),D)) return false;
	TestTrue(TEXT("N and D both use dual motion"), N->bUseGundamDualSaberMotion && D->bUseGundamDualSaberMotion);
	TestEqual(TEXT("N dash consumes reduced boost"), N->DashMeleeBoostCost, 20.0f);
	TestEqual(TEXT("D dash retains former boost cost"), D->DashMeleeBoostCost, 30.0f);
	TestEqual(TEXT("D actual base power"), D->Power, 550);
	TestEqual(TEXT("D displayed base power"), D->GetDisplayPower(), 5500);
	TestEqual(TEXT("D dash base damage after x3"), D->Power * 3, 1650);
	TestEqual(TEXT("D down point remains 800"), D->DownPoint, 800);
	TestEqual(TEXT("D uses the N detailed blade"), D->WeaponMesh.ToSoftObjectPath().ToString(), N->WeaponMesh.ToSoftObjectPath().ToString());
	struct FScopedWorld
	{
		UWorld* World;
		FScopedWorld()
		{
			World=UWorld::CreateWorld(EWorldType::Game,false,TEXT("GundamDualSaberWorld"));
			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL()); World->BeginPlay();
			if (!World->GetBegunPlay()) World->GetWorldSettings()->NotifyBeginPlay();
		}
		~FScopedWorld() { World->BeginTearingDown(); GEngine->DestroyWorldContext(World); World->DestroyWorld(false); }
	} Scoped;
	UWorld* World=Scoped.World;
	auto Tick = [World](float Seconds)
	{
		while (Seconds > KINDA_SMALL_NUMBER) { const float Dt=FMath::Min(Seconds,.01f); World->Tick(LEVELTICK_All,Dt); Seconds-=Dt; ++GFrameCounter; }
	};
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto SpawnUnit = [&]()
	{
		auto* U=World->SpawnActor<AMSPlayerCharacter>(AMSPlayerCharacter::StaticClass(),FVector(10000,20000,3000),FRotator(0,35,0),Spawn);
		if (U) U->GetCharacterMovement()->DisableMovement();
		return U;
	};
	AMSPlayerCharacter* Unit=SpawnUnit();
	AMSPlayerCharacter* Target=SpawnUnit();
	if (!TestNotNull(TEXT("attacker"),Unit) || !TestNotNull(TEXT("enemy"),Target)) return false;
	Target->SetActorLocation(FVector(0,-50000,0)); Target->SetGenericTeamId(FGenericTeamId(2));
	Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Vital::GetMaxArmorAttribute(),100000);
	Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Vital::GetArmorAttribute(),100000);
	Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Vital::GetMaxDownPointAttribute(),10000);
	TStrongObjectPtr<UMSMeleeWeaponData> Weapon(DuplicateObject<UMSMeleeWeaponData>(Source,GetTransientPackage()));
	Weapon->bUseStandardBeamSaberMotion=false; Weapon->bUseGundamDualSaberMotion=true;
	Weapon->WeaponMeshMount=EMSWeaponMeshMount::DualHands; Weapon->bDisablesNativeShield=true;
	Weapon->Power=100; Weapon->DownPoint=1; Weapon->FireInterval=.1f; Weapon->MagazineSize=0;
	Weapon->DashMeleeBoostCost=20.0f;
	TStrongObjectPtr<UGA_Melee> Ability(NewObject<UGA_Melee>(Unit));
	constexpr float InputChargeMultiplier=1.f;
	auto Fire = [&](bool Dash=false) { return Ability->PerformMelee(Unit,Unit->GetActorLocation(),Unit->GetActorForwardVector(),Dash,InputChargeMultiplier); };
	// Real imported bind hierarchies, without editing any saved data or compiled meshes.
	for (const TCHAR* Name : {TEXT("DA_MS_TAUsw"),TEXT("DA_MS_AssaultCommander"),TEXT("DA_MS_MPU01_ThreeJS")})
	{
		const FString Path=FString::Printf(TEXT("/Game/Data/MobileSuits/%s.%s"),Name,Name);
		UMSMobileSuitData* Suit=LoadObject<UMSMobileSuitData>(nullptr,*Path);
		if (!TestNotNull(Path,Suit)) return false;
		Unit->InitializeUnit(Suit); Unit->SetGenericTeamId(FGenericTeamId(1)); Unit->GetCharacterMovement()->DisableMovement();
		const UPoseableMeshComponent* SwitchedMesh=Unit->GetMeshVisualComponent()->GetPoseableMesh();
		const USkeletalMesh* SwitchedAsset=SwitchedMesh ? Cast<USkeletalMesh>(SwitchedMesh->GetSkinnedAsset()) : nullptr;
		if (!TestNotNull(TEXT("switched mesh asset"),SwitchedAsset)) return false;
		const auto& Ref=SwitchedAsset->GetRefSkeleton();
		const auto& LocalBones=SwitchedMesh->GetBoneSpaceTransforms();
		if (!TestEqual(TEXT("switched mesh bone count"),LocalBones.Num(),Ref.GetNum())) return false;
		for (int32 Bone=0; Bone<Ref.GetNum(); ++Bone)
		{
			TestTrue(FString::Printf(TEXT("%s %s uses new suit local bind translation"),Name,*Ref.GetBoneName(Bone).ToString()),
				LocalBones[Bone].GetTranslation().Equals(Ref.GetRefBonePose()[Bone].GetTranslation(),.001f));
			TestTrue(FString::Printf(TEXT("%s %s uses new suit local bind scale"),Name,*Ref.GetBoneName(Bone).ToString()),
				LocalBones[Bone].GetScale3D().Equals(Ref.GetRefBonePose()[Bone].GetScale3D(),.001f));
		}
		Unit->GetWeaponComponent()->InitializeSlots({Weapon.Get()});
		Unit->GetWeaponVisualComponent()->RefreshFromWeaponComponent();
		if (!TestTrue(TEXT("dual stage starts on imported rig"),Fire())) return false;
		for (int32 Stage=10; Stage<=14; ++Stage)
		{
			Ability->StandardSaberAttackIndex=Stage;
			const auto T=FMSStandardSaberMotion::GetTiming(Stage);
			for (float Time : {T.ActiveStart,(T.ActiveStart+T.ActiveEnd)*.5f,T.ActiveEnd})
			{
				// Sample before changing the rendered pose. A server's 120 Hz blade
				// sweep must not borrow the previous frame's hip angle during D.
				FVector SampleStarts[2], SampleEnds[2];
				for (int32 Hand=0; Hand<2; ++Hand)
					Ability->SampleStandardSaberBlade(Time,SampleStarts[Hand],SampleEnds[Hand],Hand==1);
				Unit->GetMechAnimatorComponent()->PreviewStandardSaberPose(Stage,Time);
				Unit->GetMeshVisualComponent()->TransferPoseForAutomationTests();
				if (Stage==FMSStandardSaberMotion::DualDash)
				{
					const auto P=FMSStandardSaberMotion::Sample(Stage,Time);
					const FQuat Body=FRotator(0,P.BodyYaw,0).Quaternion();
					UMSMechRigComponent* Rig=Unit->GetMechRigComponent();
					TestTrue(TEXT("D hip follows whole-body yaw"),Rig->GetJoint(TEXT("Hip"))->GetRelativeTransform().GetRotation().Equals(Body,.0001f));
					for (int32 Side=0; Side<2; ++Side)
					{
						const FName Thigh(Side ? TEXT("Thigh_R") : TEXT("Thigh_L"));
						const FName Shin(Side ? TEXT("Shin_R") : TEXT("Shin_L"));
						const float ThighAngle=Side ? P.ThighRight : P.ThighLeft;
						const float Knee=Side ? P.KneeRight : P.KneeLeft;
						TestTrue(TEXT("D thigh position rotates around body axis"),Rig->GetJoint(Thigh)->GetRelativeLocation().Equals(Body.RotateVector(Rig->GetJointRestRelativeLocation(Thigh)),.001f));
						TestTrue(TEXT("D thigh yaw preserves stride pitch"),Rig->GetJoint(Thigh)->GetRelativeTransform().GetRotation().Equals(Body*FRotator(ThighAngle,0,0).Quaternion(),.0001f));
						TestTrue(TEXT("D knee flex remains local"),Rig->GetJoint(Shin)->GetRelativeTransform().GetRotation().Equals(FRotator(-Knee,0,0).Quaternion(),.0001f));
					}
				}
				for (int32 Hand=0; Hand<2; ++Hand)
				{
					auto* Held=Hand ? Unit->GetWeaponVisualComponent()->GetImportedLeftHeldMeshComponent() : Unit->GetWeaponVisualComponent()->GetImportedHeldMeshComponent();
					if (!TestNotNull(TEXT("actual held component"),Held) || !TestNotNull(TEXT("detailed blade mesh"),Held->GetStaticMesh().Get())) return false;
					TestTrue(TEXT("each hand displays the detailed source saber"),Held->IsVisible() && Held->GetStaticMesh()==Source->WeaponMesh.LoadSynchronous());
					TestEqual(TEXT("correct hand attachment"),Held->GetAttachSocketName(),FName(Hand ? TEXT("weapon_mount_l") : TEXT("weapon_mount_r")));
					const FVector Start=SampleStarts[Hand],End=SampleEnds[Hand];
					const FVector VisibleTip=Held->GetComponentTransform().TransformPosition(FVector(0,0,Held->GetStaticMesh()->GetBoundingBox().Max.Z));
					TestTrue(FString::Printf(TEXT("%s stage %d hand %d exact imported grip (error %.6f cm)"),Name,Stage,Hand,FVector::Distance(Start,Held->GetComponentLocation())),Start.Equals(Held->GetComponentLocation(),.1f));
					TestTrue(FString::Printf(TEXT("%s stage %d hand %d exact imported tip (error %.6f cm)"),Name,Stage,Hand,FVector::Distance(End,VisibleTip)),End.Equals(VisibleTip,.1f));
				}
			}
		}
		Unit->GetMechAnimatorComponent()->PreviewStandardSaberPose(FMSStandardSaberMotion::DualDash,
			FMSStandardSaberMotion::GetTiming(FMSStandardSaberMotion::DualDash).Duration);
		for (const FName JointName : {FName(TEXT("Hip")),FName(TEXT("Thigh_L")),FName(TEXT("Thigh_R")),FName(TEXT("Shin_L")),FName(TEXT("Shin_R"))})
		{
			const UMSMechRigComponent* Rig=Unit->GetMechRigComponent();
			const USceneComponent* Joint=Rig->GetJoint(JointName);
			TestTrue(TEXT("D end restores lower-body orientation"),Joint->GetRelativeTransform().GetRotation().Equals(FQuat::Identity,.0001f));
			TestTrue(TEXT("D end restores lower-body joint offset"),Joint->GetRelativeLocation().Equals(Rig->GetJointRestRelativeLocation(JointName),.001f));
		}
		Ability->CancelMelee(); Tick(1.1f);
	}
	TestTrue(TEXT("first stage starts"),Fire());
	TestFalse(TEXT("early input cannot buffer"),Fire());
	for (int32 Stage=10; Stage<=13; ++Stage)
	{
		TestEqual(TEXT("ordered four-stage combo"),Ability->GetLastStandardSaberAttackIndex(),Stage);
		const FVector StageStart = Unit->GetActorLocation();
		Tick(.35f);
		TestTrue(TEXT("actual normal stage sweeps forward by its tuned distance"),
			FMath::IsNearlyEqual(FVector::Dist2D(StageStart,Unit->GetActorLocation()),Stage == 10 ? 400.0f : 200.0f,1.0f));
		if (Stage<13) TestTrue(TEXT("latter-half input buffers next stage"),Fire());
		else TestFalse(TEXT("fourth stage cannot buffer a fifth"),Fire());
		Tick(.36f);
	}
	TestFalse(TEXT("terminal combo state released"),Ability->StandardSaberActor.IsValid());
	Tick(.4f);
	TestTrue(TEXT("new normal attack resets to first"),Fire());
	TestEqual(TEXT("new combo first index"),Ability->GetLastStandardSaberAttackIndex(),10);
	Ability->CancelMelee(); Tick(.2f);
	// Left-only second stage: real blade active interval causes GAS damage, idle right cannot.
	TestTrue(TEXT("left-stage fixture begins through public attack"),Fire());
	TestTrue(TEXT("left-stage transition"),Ability->StartStandardSaberStage(11));
	FVector L0,L1,R0,R1;
	Ability->SampleStandardSaberBlade(.25f,L0,L1,true);
	Ability->SampleStandardSaberBlade(.25f,R0,R1,false);
	Target->SetActorLocation(FMath::Lerp(L0,L1,.8f));
	const float Before=Target->GetVitalSet()->GetArmor();
	const float NormalDPBefore=Target->GetVitalSet()->GetDownPoint();
	const UMSMobileSuitData* EquippedSuit=Unit->GetMobileSuitData();
	const float ExpectedNormalDP=Weapon->DownPoint
		* FMath::Max(0.f,EquippedSuit?EquippedSuit->MeleeCorrection:1.f)
		* InputChargeMultiplier * FMath::Max(0.f,Unit->GetCombatModifiers().DamageMultiplier);
	// Observe packets delivered by the real blade sweep, not the ability's cached
	// numbers: forced knockdown deliberately clears the accumulated DP immediately.
	float ObservedNormalDP=-1.f, ObservedDashDP=-1.f;
	int32 NormalDamagePackets=0, DashDamagePackets=0;
	const FDelegateHandle DamageObserver=Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
		{
			if (!Spec.Def || !Spec.Def->IsA<UGE_Damage>() || Spec.GetContext().GetOriginalInstigator()!=Unit) return;
			const auto* Context=FMSGameplayEffectContext::Get(Spec.GetContext());
			if (Context && Context->IsForceKnockdown())
			{
				++DashDamagePackets;
				ObservedDashDP=Spec.GetSetByCallerMagnitude(MSTags::Data_DownPoint,false,-1.f);
			}
			else
			{
				++NormalDamagePackets;
				ObservedNormalDP=Spec.GetSetByCallerMagnitude(MSTags::Data_DownPoint,false,-1.f);
			}
		});
	Ability->ProcessStandardSaberInterval(0,.10f);
	TestEqual(TEXT("startup has no damage"),Target->GetVitalSet()->GetArmor(),Before);
	Ability->ProcessStandardSaberInterval(.24f,.25f);
	const float After=Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("left blade alone damages enemy"),After<Before);
	TestEqual(TEXT("normal blade delivered exactly one non-forced damage GE"),NormalDamagePackets,1);
	TestTrue(TEXT("normal delivered GE carries authored DP with suit and charge corrections"),
		FMath::IsNearlyEqual(ObservedNormalDP,ExpectedNormalDP,.0001f));
	TestTrue(TEXT("normal damage GE actually accumulates that base DP"),
		FMath::IsNearlyEqual(Target->GetVitalSet()->GetDownPoint()-NormalDPBefore,ObservedNormalDP,.01f));
	TestEqual(TEXT("one enemy hit in left-only stage"),Ability->GetLastHitCount(),1);
	Ability->ProcessStandardSaberInterval(.24f,.25f);
	TestEqual(TEXT("repeat interval cannot hit twice"),Target->GetVitalSet()->GetArmor(),After);
	// Two hands share one actor set even if the enemy moves onto the other real blade.
	TestTrue(TEXT("two-hand stage begins"),Ability->StartStandardSaberStage(12));
	Ability->SampleStandardSaberBlade(.24f,L0,L1,true); Ability->SampleStandardSaberBlade(.24f,R0,R1,false);
	Target->SetActorLocation((L0+L1)*.5f); Ability->ApplyStandardSaberBladeSegment(L0,L1);
	const float SharedAfter=Target->GetVitalSet()->GetArmor();
	TestTrue(TEXT("new stage may hit the same actor again"),SharedAfter<After);
	Target->SetActorLocation((R0+R1)*.5f); Ability->ApplyStandardSaberBladeSegment(R0,R1);
	TestEqual(TEXT("other hand cannot double damage the same enemy"),Target->GetVitalSet()->GetArmor(),SharedAfter);
	Ability->CancelMelee(); Tick(.2f); Target->SetActorLocation(FVector(0,-50000,0));
	const FVector DashStart = Unit->GetActorLocation();
	Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Boost::GetBoostAttribute(), 100.0f);
	const float NBoostBefore = Unit->GetBoostSet()->GetBoost();
	TestTrue(TEXT("travel fixture starts a real D attack"),Fire(true));
	TestTrue(TEXT("N D attack consumes 20 boost"), FMath::IsNearlyEqual(Unit->GetBoostSet()->GetBoost(), NBoostBefore-20.0f,.01f));
	Tick(.4f);
	TestTrue(TEXT("D actually travels sixty environment metres"),FMath::IsNearlyEqual(FVector::Dist2D(DashStart,Unit->GetActorLocation()),6000.0f,1.0f));
	Tick(.6f);
	TestTrue(TEXT("D recovery does not keep moving forward"),FMath::IsNearlyEqual(FVector::Dist2D(DashStart,Unit->GetActorLocation()),6000.0f,1.0f));
	// A physical blocker must shorten the longer dash; no teleport or collision bypass.
	AActor* Wall=World->SpawnActor<AActor>();
	UBoxComponent* Box=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(Box);Wall->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(50,10000,10000));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();
	const FVector WallStart=Unit->GetActorLocation();
	Wall->SetActorLocation(WallStart+Unit->GetActorForwardVector()*2000.0f);Wall->SetActorRotation(Unit->GetActorRotation());
	TestTrue(TEXT("blocked D starts"),Fire(true));Tick(.4f);
	const float BlockedTravel=FVector::Dist2D(WallStart,Unit->GetActorLocation());
	TestTrue(TEXT("longer D advances before reaching blocker"),BlockedTravel>100.0f);
	TestTrue(TEXT("longer D sweep stops before the physical wall"),BlockedTravel<2000.0f);
	Ability->CancelMelee();Wall->Destroy();Tick(.3f);
	TestTrue(TEXT("dash begins"),Fire(true));
	TestEqual(TEXT("D is distinct terminal stage"),Ability->GetLastStandardSaberAttackIndex(),14);
	const float DashAmount=Ability->StandardSaberDamage, DashDP=Ability->StandardSaberDownPoint;
	Ability->SampleStandardSaberBlade(.28f,L0,L1,true);
	Target->SetActorLocation((L0+L1)*.5f);
	const float DashArmorBefore=Target->GetVitalSet()->GetArmor();
	Ability->ProcessStandardSaberInterval(.275f,.28f);
	Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(DamageObserver);
	TestTrue(TEXT("D actual blade applies triple normal damage through GAS"),FMath::IsNearlyEqual(DashArmorBefore-Target->GetVitalSet()->GetArmor(),(Before-After)*3,.05f));
	TestEqual(TEXT("actual D hit delivers exactly one forced-knockdown damage GE"),DashDamagePackets,1);
	TestEqual(TEXT("D delivered GE preserves normal base DP instead of tripling it"),ObservedDashDP,ObservedNormalDP);
	TestEqual(TEXT("D delivered GE agrees with the ability DP"),ObservedDashDP,DashDP);
	TestTrue(TEXT("D actual hit enters knockdown even far below the DG threshold"),
		Target->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_KnockedDown)
		&& Target->GetHitReactionComponent()->GetPhase()==EMSHitReactionPhase::FallingDown);
	TestEqual(TEXT("forced knockdown consumes and resets accumulated DP"),Target->GetVitalSet()->GetDownPoint(),0.f);
	Target->SetActorLocation(FVector(0,-50000,0));
	Ability->CancelMelee(); Tick(.2f); TestTrue(TEXT("normal begins for ratio check"),Fire());
	TestTrue(TEXT("D power exactly triples normal"),FMath::IsNearlyEqual(DashAmount,Ability->StandardSaberDamage*3));
	TestEqual(TEXT("D leaves DP unchanged"),DashDP,Ability->StandardSaberDownPoint);
	Ability->CancelMelee(); Tick(.2f);
	Weapon->Power=D->Power; Weapon->DownPoint=D->DownPoint;
	Weapon->DashMeleeBoostCost=D->DashMeleeBoostCost;
	Unit->GetAbilitySystemComponent()->SetNumericAttributeBase(UMSAttributeSet_Boost::GetBoostAttribute(), 100.0f);
	const float DBoostBefore = Unit->GetBoostSet()->GetBoost();
	TestTrue(TEXT("D variant dash starts with the shared dual motion"),Fire(true));
	TestTrue(TEXT("D variant dash consumes 30 boost"),FMath::IsNearlyEqual(Unit->GetBoostSet()->GetBoost(),DBoostBefore-30.0f,.01f));
	TestTrue(TEXT("D variant applies 550 base power times three through the ability"),
		FMath::IsNearlyEqual(Ability->StandardSaberDamage,1650.0f * ExpectedNormalDP,.01f));
	TestTrue(TEXT("D variant keeps 800 base DP through the ability"),
		FMath::IsNearlyEqual(Ability->StandardSaberDownPoint,800.0f * ExpectedNormalDP,.01f));
	Ability->CancelMelee(); Tick(.2f);
	Weapon->Power=100; Weapon->DownPoint=1; Weapon->DashMeleeBoostCost=20.0f;
	for (const FGameplayTag Tag : TArray<FGameplayTag>{MSTags::State_Dead,MSTags::State_KnockedDown,MSTags::State_Staggered,MSTags::State_EmergencyRecall})
	{
		TestTrue(TEXT("swing starts before interruption"),Fire()); Tick(.35f); TestTrue(TEXT("pending input exists"),Fire());
		Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(Tag,1); Tick(.03f);
		TestFalse(TEXT("interruption releases stage"),Ability->StandardSaberActor.IsValid());
		TestFalse(TEXT("interruption clears pending input"),Ability->bStandardSaberPendingFollowUp);
		TestFalse(TEXT("interruption releases recovery"),Unit->GetAbilitySystemComponent()->HasMatchingGameplayTag(MSTags::State_MeleeRecovery));
		Unit->GetAbilitySystemComponent()->SetLooseGameplayTagCount(Tag,0); Tick(.2f);
	}
	// Shield suppression uses the existing native-shield policy, not a replacement shield.
	TStrongObjectPtr<UMSMobileSuitData> ShieldSuit(NewObject<UMSMobileSuitData>());
	TStrongObjectPtr<UMSShieldWeaponData> ShieldData(NewObject<UMSShieldWeaponData>());
	ShieldSuit->NativeShield=ShieldData.Get();
	Unit->GetShieldComponent()->InitializeFromMobileSuit(ShieldSuit.Get());
	TestFalse(TEXT("selected dual saber disables native guard while idle"),Unit->GetShieldComponent()->CanProvideGuard());
	Unit->GetWeaponComponent()->InitializeSlots({Weapon.Get(),Source});
	TestTrue(TEXT("attack before weapon switch"),Fire());
	TestTrue(TEXT("switch to old GM saber"),Unit->GetWeaponComponent()->SwitchToSlot(1)); Tick(1.2f);
	TestFalse(TEXT("switch clears dual combo"),Ability->StandardSaberActor.IsValid());
	TestFalse(TEXT("switch hides left blade"),Unit->GetWeaponVisualComponent()->GetImportedLeftHeldMeshComponent()->IsVisible());
	TestTrue(TEXT("normal selected weapon restores native guard"),Unit->GetShieldComponent()->CanProvideGuard());
	TestTrue(TEXT("old GM saber still starts"),Fire());
	TestEqual(TEXT("GM retains original first index"),Ability->GetLastStandardSaberAttackIndex(),0);
	Ability->CancelMelee();
	return true;
}
#endif
