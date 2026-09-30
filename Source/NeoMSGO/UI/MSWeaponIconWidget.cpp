#include "UI/MSWeaponIconWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Core/MSGameplayTags.h"
#include "Data/MSWeaponData.h"
#include "Layout/Clipping.h"
#include "Rendering/DrawElements.h"
#include "UI/MSPixelPainter.h"
#include "UI/MSWeaponPhotoSubsystem.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Weapons/MSDualHeatSwordWeaponData.h"

namespace
{
	constexpr int32 PhotoRetryLimit=120;
	constexpr float PhotoRetryIntervalSeconds=.25f;

	FBox2D MakeIconBox(const float MinX,const float MinY,const float MaxX,const float MaxY)
	{
		return FBox2D(FVector2D(MinX,MinY),FVector2D(MaxX,MaxY));
	}

	FLinearColor Shade(const FLinearColor& Base,const float Factor,const float Alpha=1.0f)
	{
		return FLinearColor(Base.R*Factor,Base.G*Factor,Base.B*Factor,Base.A*Alpha);
	}

	/**
	 * A profile is a deterministic visual contract derived from weapon data.
	 * The profiles intentionally follow the same model distinctions used by
	 * MSWeaponVisualComponent (head vulcan, shoulder pod, imported heat sword)
	 * and the data commandlet (scope, burst, charge, pellet and deployable
	 * variants). No hash or random value participates in this selection.
	 */
	enum class EWeaponIconProfile : uint8
	{
		Empty,
		MachineGun,
		HeadVulcan,
		MachineGunVulcan,
		MachineGunDrum,
		ScopeRifle,
		BeamRifle,
		BeamCarbine,
		BeamLauncher,
		BeamCannon,
		BeamCharge,
		Bazooka,
		BazookaShoulder,
		BazookaMortar,
		BazookaRocketRack,
		BazookaSmoke,
		BazookaCharge,
		MeleeSword,
		MeleeGreatSword,
		MeleeAxe,
		MeleeLance,
		MeleeKnuckle,
		MeleeTwinBlade,
		MeleeDagger,
		MeleeKunai,
		Shield,
		ShieldLight,
		ShieldFortress,
		Supply,
		SupplyPack,
		SupplyAmmo,
		ContinuousBeam,
		ContinuousBeamFocus,
		ContinuousBeamConvergence,
		Shotgun,
		ShotgunDense,
		ShotgunWide,
		Homing,
		HomingRack,
		HomingWide,
		Unknown
	};

	bool HasWeaponId(const FString& WeaponId,const TCHAR* Token)
	{
		return WeaponId.Contains(Token,ESearchCase::IgnoreCase);
	}

	EWeaponIconProfile ResolveCategoryProfile(const FGameplayTag& Category)
	{
		if(Category==MSTags::Weapon_Category_MachineGun) return EWeaponIconProfile::MachineGun;
		if(Category==MSTags::Weapon_Category_BeamRifle) return EWeaponIconProfile::BeamRifle;
		if(Category==MSTags::Weapon_Category_Bazooka) return EWeaponIconProfile::Bazooka;
		if(Category==MSTags::Weapon_Category_Melee) return EWeaponIconProfile::MeleeSword;
		if(Category==MSTags::Weapon_Category_Shield) return EWeaponIconProfile::Shield;
		if(Category==MSTags::Weapon_Category_Supply) return EWeaponIconProfile::Supply;
		if(Category==MSTags::Weapon_Category_ContinuousBeam) return EWeaponIconProfile::ContinuousBeam;
		if(Category==MSTags::Weapon_Category_Shotgun) return EWeaponIconProfile::Shotgun;
		if(Category==MSTags::Weapon_Category_Homing) return EWeaponIconProfile::Homing;
		return EWeaponIconProfile::Unknown;
	}

	EWeaponIconProfile ResolveWeaponProfile(const UMSWeaponDataBase* Weapon)
	{
		if(!Weapon) return EWeaponIconProfile::Empty;

		const FGameplayTag Category=Weapon->CategoryTag;
		const FString WeaponId=Weapon->GetFName().ToString();
		if(Category==MSTags::Weapon_Category_MachineGun)
		{
			const UMSRangedWeaponData* Ranged=Cast<UMSRangedWeaponData>(Weapon);
			if(Weapon->bIsBuiltIn) return EWeaponIconProfile::HeadVulcan;
			if(Ranged && Ranged->bIsScopeWeapon) return EWeaponIconProfile::ScopeRifle;
			if(HasWeaponId(WeaponId,TEXT("AssaultVulcan")) || HasWeaponId(WeaponId,TEXT("VulcanMG")))
				return EWeaponIconProfile::MachineGunVulcan;
			if((Ranged && Ranged->VariantType==EMSWeaponVariantType::RapidFire && Weapon->MagazineSize>=80)
				|| HasWeaponId(WeaponId,TEXT("HeavyGatling")))
			{
				return EWeaponIconProfile::MachineGunDrum;
			}
			return EWeaponIconProfile::MachineGun;
		}

		if(Category==MSTags::Weapon_Category_BeamRifle)
		{
			const UMSRangedWeaponData* Ranged=Cast<UMSRangedWeaponData>(Weapon);
			if(Ranged && Ranged->bIsChargeable) return EWeaponIconProfile::BeamCharge;
			if(HasWeaponId(WeaponId,TEXT("BeamCannon"))) return EWeaponIconProfile::BeamCannon;
			if(HasWeaponId(WeaponId,TEXT("BeamLauncher"))) return EWeaponIconProfile::BeamLauncher;
			if(HasWeaponId(WeaponId,TEXT("Carbine")) || HasWeaponId(WeaponId,TEXT("BeamMachineGun"))
				|| (Ranged && Ranged->BurstCount>1))
			{
				return EWeaponIconProfile::BeamCarbine;
			}
			return EWeaponIconProfile::BeamRifle;
		}

		if(Category==MSTags::Weapon_Category_Bazooka)
		{
			const UMSRangedWeaponData* Ranged=Cast<UMSRangedWeaponData>(Weapon);
			if(Ranged && Ranged->bIsChargeable) return EWeaponIconProfile::BazookaCharge;
			if(Weapon->bIsBuiltIn || HasWeaponId(WeaponId,TEXT("ShoulderRocketPod")))
			{
				return EWeaponIconProfile::BazookaShoulder;
			}
			if(HasWeaponId(WeaponId,TEXT("SmokeGrenade"))) return EWeaponIconProfile::BazookaSmoke;
			if(HasWeaponId(WeaponId,TEXT("HeavyMortar"))) return EWeaponIconProfile::BazookaMortar;
			if(HasWeaponId(WeaponId,TEXT("MissilePod")) || HasWeaponId(WeaponId,TEXT("TripleRocket"))
				|| HasWeaponId(WeaponId,TEXT("GrenadeLauncher")) || (Ranged && Ranged->BurstCount>1))
			{
				return EWeaponIconProfile::BazookaRocketRack;
			}
			if(HasWeaponId(WeaponId,TEXT("ShoulderBazooka"))) return EWeaponIconProfile::BazookaShoulder;
			return EWeaponIconProfile::Bazooka;
		}

		if(Category==MSTags::Weapon_Category_Melee)
		{
			if(Weapon->IsA<UMSDualHeatSwordWeaponData>()) return EWeaponIconProfile::MeleeTwinBlade;
			if(HasWeaponId(WeaponId,TEXT("GreatSword"))) return EWeaponIconProfile::MeleeGreatSword;
			if(HasWeaponId(WeaponId,TEXT("HeatAxe"))) return EWeaponIconProfile::MeleeAxe;
			if(HasWeaponId(WeaponId,TEXT("HeatLancer"))) return EWeaponIconProfile::MeleeLance;
			if(HasWeaponId(WeaponId,TEXT("Knuckle"))) return EWeaponIconProfile::MeleeKnuckle;
			if(HasWeaponId(WeaponId,TEXT("TwinDagger"))) return EWeaponIconProfile::MeleeDagger;
			if(HasWeaponId(WeaponId,TEXT("kunai"))) return EWeaponIconProfile::MeleeKunai;
			if(const UMSMeleeWeaponData* Melee=Cast<UMSMeleeWeaponData>(Weapon))
			{
				switch(Melee->MeleeMotionType)
				{
				case EMSMeleeMotionType::Overhead: return EWeaponIconProfile::MeleeGreatSword;
				case EMSMeleeMotionType::Thrust: return EWeaponIconProfile::MeleeLance;
				case EMSMeleeMotionType::Punch: return EWeaponIconProfile::MeleeKnuckle;
				default: break;
				}
			}
			return EWeaponIconProfile::MeleeSword;
		}

		if(Category==MSTags::Weapon_Category_Shield)
		{
			const UMSShieldWeaponData* Shield=Cast<UMSShieldWeaponData>(Weapon);
			if(HasWeaponId(WeaponId,TEXT("LightShield")) || (Shield && Shield->GuardFrontalAngleDegrees<=55.0f))
				return EWeaponIconProfile::ShieldLight;
			if(HasWeaponId(WeaponId,TEXT("FortressShield")) || (Shield && Shield->GuardFrontalAngleDegrees>=80.0f))
				return EWeaponIconProfile::ShieldFortress;
			return EWeaponIconProfile::Shield;
		}

		if(Category==MSTags::Weapon_Category_Supply)
		{
			if(const UMSSupplyWeaponData* Supply=Cast<UMSSupplyWeaponData>(Weapon))
			{
				switch(Supply->DeployedItemType)
				{
				case EMSDeployedItemType::SupplyPackAmmo: return EWeaponIconProfile::SupplyAmmo;
				case EMSDeployedItemType::SupplyPackRepair: return EWeaponIconProfile::SupplyPack;
				default: break;
				}
			}
			return EWeaponIconProfile::Supply;
		}

		if(Category==MSTags::Weapon_Category_ContinuousBeam)
		{
			const UMSContinuousBeamWeaponData* Beam=Cast<UMSContinuousBeamWeaponData>(Weapon);
			if(HasWeaponId(WeaponId,TEXT("FocusBeam")) || (Beam && Beam->BeamRadius<=0.10f))
				return EWeaponIconProfile::ContinuousBeamFocus;
			if(HasWeaponId(WeaponId,TEXT("ConvergenceBeam")) || (Beam && Beam->BeamRadius>=0.20f))
				return EWeaponIconProfile::ContinuousBeamConvergence;
			return EWeaponIconProfile::ContinuousBeam;
		}

		if(Category==MSTags::Weapon_Category_Shotgun)
		{
			const UMSShotgunWeaponData* Shotgun=Cast<UMSShotgunWeaponData>(Weapon);
			if(Weapon->VariantType==EMSWeaponVariantType::Wide || (Shotgun && Shotgun->PelletSpreadDegrees>=12.0f))
				return EWeaponIconProfile::ShotgunWide;
			if(Shotgun && Shotgun->PelletCount>=9) return EWeaponIconProfile::ShotgunDense;
			return EWeaponIconProfile::Shotgun;
		}

		if(Category==MSTags::Weapon_Category_Homing)
		{
			if(Weapon->VariantType==EMSWeaponVariantType::Wide) return EWeaponIconProfile::HomingWide;
			const UMSRangedWeaponData* Ranged=Cast<UMSRangedWeaponData>(Weapon);
			if(HasWeaponId(WeaponId,TEXT("MultiHoming")) || (Ranged && Ranged->BurstCount>1) || Weapon->MagazineSize>=8)
				return EWeaponIconProfile::HomingRack;
			return EWeaponIconProfile::Homing;
		}

		return EWeaponIconProfile::Unknown;
	}

	/** Paint one compact 100x24 vector badge. All authored points stay in this rectangle. */
	void PaintMechanicalIcon(MSPixel::FPainter& Painter,const EWeaponIconProfile Profile,
		const FLinearColor& InColor)
	{
		const FLinearColor Shadow=Shade(InColor,.30f,.96f);
		const FLinearColor Body=Shade(InColor,.68f,.98f);
		const FLinearColor Mid=Shade(InColor,.86f,1.0f);
		const FLinearColor Highlight=Shade(InColor,1.20f,1.0f);
		const FLinearColor Edge=MSPixel::Hex(0xD7F4E9,.94f);
		const FLinearColor Gold=MSPixel::Hex(0xD6B23A,.96f);
		const FLinearColor Warning=MSPixel::Hex(0xF07A52,.96f);

		switch(Profile)
		{
		case EWeaponIconProfile::MachineGun:
			Painter.Polygon({{2,11},{15,8},{22,10},{17,16},{6,16}},Shadow);
			Painter.Gradient(16,5,30,10,Mid,Body); Painter.Rect(24,3,12,3,Highlight);
			Painter.Rect(44,8,43,3,Body); Painter.Rect(44,11,33,1,Shadow); Painter.Rect(31,15,9,8,Shadow);
			Painter.Box(86,6,10,8,1,Edge); Painter.Line({38,7},{83,7},1,Edge); return;

		case EWeaponIconProfile::HeadVulcan:
			// MSWeaponVisualComponent::HeadVulcanGroup has two parallel barrels and a sensor.
			Painter.Polygon({{4,8},{19,7},{25,10},{19,16},{5,15}},Shadow);
			Painter.Rect(20,7,35,3,Mid); Painter.Rect(20,13,35,3,Body); Painter.Rect(55,6,24,4,Highlight); Painter.Rect(55,14,24,4,Highlight);
			Painter.Box(79,5,16,14,1,Edge); Painter.Rect(29,10,10,2,Warning); return;

		case EWeaponIconProfile::MachineGunVulcan:
			// Handheld AssaultVulcan/VulcanMG40: twin compact barrels, without the head sensor plate.
			Painter.Polygon({{3,10},{16,7},{24,9},{20,16},{6,16}},Shadow); Painter.Chamfer(18,5,25,11,2,Body);
			Painter.Rect(41,7,39,3,Mid); Painter.Rect(41,13,39,3,Mid); Painter.Box(80,5,16,14,1,Edge);
			Painter.Rect(27,16,9,8,Shadow); Painter.Line({33,6},{79,6},1,Highlight); return;

		case EWeaponIconProfile::MachineGunDrum:
			Painter.Polygon({{2,11},{14,8},{22,10},{18,16},{6,16}},Shadow); Painter.Chamfer(17,5,31,11,2,Body);
			Painter.Circle({39,16},8,Shadow,20); Painter.Circle({39,16},5,Mid,20); Painter.Rect(46,8,39,4,Body); Painter.Rect(83,6,13,8,Edge);
			Painter.Line({23,6},{49,6},1,Highlight); return;

		case EWeaponIconProfile::ScopeRifle:
			// Scope and bipod are the data-driven bIsScopeWeapon distinction.
			Painter.Polygon({{2,11},{15,8},{21,10},{17,15},{6,15}},Shadow); Painter.Gradient(16,6,38,8,Mid,Body);
			Painter.Rect(26,2,19,3,Highlight); Painter.Rect(31,5,4,4,Edge); Painter.Rect(53,8,39,3,Body); Painter.Box(88,6,8,8,1,Edge);
			Painter.Line({31,16},{26,23},1,Shadow); Painter.Line({36,16},{42,23},1,Shadow); return;

		case EWeaponIconProfile::BeamRifle:
			Painter.Polygon({{2,11},{16,8},{23,9},{18,16},{6,16}},Shadow); Painter.Gradient(17,5,30,10,Mid,Body); Painter.Rect(25,2,12,3,Highlight);
			Painter.Rect(46,8,39,3,Body); Painter.Rect(80,7,15,5,Edge); Painter.Rect(33,15,5,8,Shadow); Painter.Line({20,6},{47,6},1,Edge); return;

		case EWeaponIconProfile::BeamCarbine:
			Painter.Polygon({{4,10},{18,6},{31,7},{34,16},{18,17},{7,14}},Shadow); Painter.Rect(30,8,29,7,Body); Painter.Rect(34,5,20,2,Highlight);
			Painter.Rect(59,7,26,3,Edge); Painter.Rect(59,12,26,3,Mid); Painter.Box(85,5,11,12,1,Edge); Painter.Rect(42,15,8,8,Shadow); return;

		case EWeaponIconProfile::BeamLauncher:
			Painter.Polygon({{2,12},{16,8},{25,9},{21,17},{6,17}},Shadow); Painter.Chamfer(18,5,40,13,2,Body); Painter.Rect(35,2,18,3,Highlight);
			Painter.Rect(58,6,22,11,Mid); Painter.Rect(80,4,16,15,Edge); Painter.Line({31,7},{77,7},1,Highlight); Painter.Rect(30,17,8,6,Shadow); return;

		case EWeaponIconProfile::BeamCannon:
			Painter.Polygon({{2,11},{15,7},{24,9},{20,17},{6,17}},Shadow); Painter.Chamfer(18,4,44,15,3,Body); Painter.Rect(32,2,18,3,Highlight);
			Painter.Rect(57,5,29,13,Mid); Painter.Box(82,2,14,20,1,Edge); Painter.Circle({87,12},5,Edge,20); Painter.Circle({87,12},2.5f,Shadow,16); return;

		case EWeaponIconProfile::BeamCharge:
			Painter.Polygon({{3,11},{16,8},{24,9},{20,16},{7,16}},Shadow); Painter.Gradient(18,5,35,11,Highlight,Body); Painter.Rect(31,2,9,3,Edge);
			Painter.Rect(55,8,31,4,Mid); Painter.Box(86,5,10,12,1,Edge); Painter.Line({50,3},{50,21},1,Gold); Painter.Line({44,6},{56,6},1,Gold); return;

		case EWeaponIconProfile::Bazooka:
			Painter.Chamfer(6,5,70,10,3,Shadow); Painter.Gradient(13,8,58,7,Mid,Body); Painter.Rect(21,16,10,8,Shadow); Painter.Rect(76,3,11,18,Body);
			Painter.Box(87,2,9,20,1,Edge); Painter.Line({18,7},{68,7},1,Highlight); return;

		case EWeaponIconProfile::BazookaShoulder:
			// ShoulderRocketPod is a rack in the model component, while hand bazookas are tubes.
			Painter.Chamfer(8,4,48,16,3,Body); Painter.Rect(18,2,12,3,Highlight); Painter.Circle({69,7},6,Shadow,20); Painter.Circle({69,7},3,Edge,16);
			Painter.Circle({69,17},6,Shadow,20); Painter.Circle({69,17},3,Edge,16); Painter.Box(78,3,18,18,1,Edge); Painter.Rect(51,9,20,3,Mid); return;

		case EWeaponIconProfile::BazookaMortar:
			Painter.Polygon({{8,17},{17,5},{56,5},{69,17}},Shadow); Painter.Chamfer(17,7,49,9,3,Body); Painter.Rect(25,2,15,5,Highlight); Painter.Rect(29,16,9,8,Shadow); Painter.Box(70,4,25,16,1,Edge); return;

		case EWeaponIconProfile::BazookaRocketRack:
			Painter.Chamfer(7,4,54,16,3,Body); Painter.Rect(17,2,10,3,Highlight);
			for(const float Y : {7.0f,12.0f,17.0f}) { Painter.Circle({69,Y},4,Shadow,16); Painter.Circle({69,Y},2,Edge,12); }
			Painter.Rect(77,5,18,14,Mid); Painter.Box(77,5,18,14,1,Edge); return;

		case EWeaponIconProfile::BazookaSmoke:
			Painter.Chamfer(22,7,34,13,3,Body); Painter.Rect(31,3,16,4,Highlight); Painter.Rect(17,10,8,8,Shadow); Painter.Rect(56,10,8,8,Shadow);
			Painter.Circle({74,10},6,Mid,18); Painter.Circle({82,7},5,Edge,18); Painter.Circle({88,13},6,Mid,18); return;

		case EWeaponIconProfile::BazookaCharge:
			Painter.Chamfer(7,5,72,10,3,Body); Painter.Rect(19,8,51,4,Mid); Painter.Rect(23,16,9,8,Shadow); Painter.Box(78,3,18,18,1,Edge);
			Painter.Circle({73,12},5,Gold,16); Painter.Circle({73,12},2,Shadow,12); return;

		case EWeaponIconProfile::MeleeSword:
			Painter.Polygon({{9,20},{28,2},{37,2},{21,20}},Edge); Painter.Polygon({{29,5},{35,3},{22,19},{18,19}},Mid); Painter.Rect(24,14,29,2,Gold);
			Painter.Rect(48,15,13,5,Shadow); Painter.Rect(61,14,7,7,Body); Painter.Line({31,4},{19,17},1,Highlight); return;

		case EWeaponIconProfile::MeleeGreatSword:
			Painter.Polygon({{8,21},{24,1},{40,1},{27,21}},Edge); Painter.Polygon({{24,4},{37,3},{26,19},{19,19}},Mid); Painter.Rect(20,15,37,3,Gold);
			Painter.Rect(50,17,14,5,Shadow); Painter.Rect(64,15,8,8,Body); return;

		case EWeaponIconProfile::MeleeAxe:
			Painter.Rect(13,3,5,18,Shadow); Painter.Rect(18,10,30,4,Gold); Painter.Polygon({{43,2},{68,3},{78,10},{68,20},{44,19},{53,11}},Edge);
			Painter.Polygon({{53,5},{67,6},{73,10},{65,16},{53,16}},Mid); Painter.Box(79,6,14,10,1,Edge); return;

		case EWeaponIconProfile::MeleeLance:
			Painter.Polygon({{3,12},{79,9},{95,12},{79,15}},Edge); Painter.Polygon({{3,12},{13,7},{18,12},{13,17}},Mid); Painter.Rect(28,8,3,8,Gold); Painter.Rect(39,7,3,10,Gold); Painter.Rect(50,8,3,8,Gold); Painter.Rect(20,16,10,5,Shadow); return;

		case EWeaponIconProfile::MeleeKnuckle:
			Painter.Rect(9,9,16,8,Shadow); Painter.Chamfer(22,4,37,16,4,Body); Painter.Rect(27,1,7,9,Edge); Painter.Rect(36,1,7,9,Edge); Painter.Rect(45,2,7,8,Edge);
			Painter.Rect(30,18,23,4,Shadow); Painter.Line({60,8},{89,8},2,Gold); Painter.Polygon({{87,5},{96,8},{87,11}},Edge); return;

		case EWeaponIconProfile::MeleeTwinBlade:
			Painter.Polygon({{5,18},{23,2},{29,2},{14,20}},Edge); Painter.Polygon({{27,20},{43,3},{49,3},{36,21}},Edge); Painter.Line({21,5},{10,17},1,Highlight); Painter.Line({41,6},{31,18},1,Highlight);
			Painter.Rect(45,15,12,5,Shadow); Painter.Rect(58,14,8,7,Body); Painter.Rect(18,15,28,2,Gold); return;

		case EWeaponIconProfile::MeleeDagger:
			Painter.Polygon({{7,16},{26,5},{32,6},{18,18}},Edge); Painter.Polygon({{22,21},{39,9},{45,10},{31,22}},Edge); Painter.Rect(29,13,22,2,Gold);
			Painter.Rect(48,15,18,5,Shadow); Painter.Box(66,13,8,8,1,Body); return;

		case EWeaponIconProfile::MeleeKunai:
			Painter.Polygon({{8,12},{37,2},{50,12},{37,22}},Edge); Painter.Polygon({{18,12},{37,6},{43,12},{37,18}},Mid); Painter.Rect(47,10,36,4,Shadow);
			Painter.Circle({88,12},5,Gold,16); Painter.Circle({88,12},2,Shadow,12); return;

		case EWeaponIconProfile::Shield:
			Painter.Polygon({{17,2},{55,2},{75,6},{71,17},{47,23},{23,18},{18,7}},Mid); Painter.Polygon({{25,5},{51,5},{66,8},{63,14},{47,19},{29,15}},Shadow);
			Painter.Line({22,4},{69,4},1,Edge); Painter.Line({47,5},{47,21},1,Highlight); Painter.Polygon({{40,19},{54,19},{47,23}},Edge); return;

		case EWeaponIconProfile::ShieldLight:
			Painter.Polygon({{31,2},{61,4},{72,9},{61,19},{47,23},{34,18},{27,9}},Body); Painter.Polygon({{38,6},{58,7},{65,10},{58,16},{47,19},{38,15}},Shadow);
			Painter.Line({47,4},{47,21},1,Edge); Painter.Polygon({{42,17},{53,17},{47,22}},Highlight); return;

		case EWeaponIconProfile::ShieldFortress:
			Painter.Polygon({{20,2},{72,2},{80,7},{76,19},{47,23},{17,19},{14,7}},Mid); Painter.Rect(22,6,50,12,Shadow); Painter.Box(22,6,50,12,1,Edge);
			Painter.Rect(45,4,4,18,Highlight); Painter.Rect(27,10,40,2,Gold); Painter.Polygon({{39,18},{55,18},{47,23}},Edge); return;

		case EWeaponIconProfile::Supply:
			Painter.Rect(35,2,29,4,Edge); Painter.Rect(41,5,17,3,Shadow); Painter.Chamfer(19,7,55,14,3,Body); Painter.Rect(24,10,12,9,Shadow); Painter.Rect(57,10,12,9,Shadow);
			Painter.Line({22,9},{71,9},1,Highlight); Painter.Line({47,10},{47,20},1,Edge); Painter.Rect(43,17,8,3,Gold); return;

		case EWeaponIconProfile::SupplyPack:
			Painter.Polygon({{24,8},{39,4},{67,6},{75,17},{63,21},{27,19},{19,13}},Body); Painter.Rect(35,2,25,4,Edge); Painter.Rect(32,10,35,3,Highlight); Painter.Box(25,8,45,12,1,Edge);
			Painter.Rect(43,14,12,5,Gold); Painter.Line({60,13},{69,18},1,Shadow); return;

		case EWeaponIconProfile::SupplyAmmo:
			Painter.Rect(31,4,12,17,Shadow); Painter.Rect(45,2,12,19,Body); Painter.Rect(59,5,12,16,Mid); Painter.Rect(28,2,46,3,Edge); Painter.Line({29,10},{73,10},1,Highlight); Painter.Line({29,16},{73,16},1,Gold);
			Painter.Box(75,5,18,15,1,Edge); Painter.Rect(80,9,8,7,Warning); return;

		case EWeaponIconProfile::ContinuousBeam:
			Painter.Polygon({{2,9},{12,6},{28,7},{30,17},{11,18},{2,14}},Shadow); Painter.Chamfer(26,5,12,10,2,Body); Painter.Rect(58,3,22,18,Highlight); Painter.Rect(80,1,12,22,Mid);
			Painter.Circle({92,12},5,Edge,20); Painter.Circle({92,12},2.3f,Shadow,16); Painter.Line({8,5},{25,5},1,Edge); return;

		case EWeaponIconProfile::ContinuousBeamFocus:
			Painter.Polygon({{3,9},{15,6},{29,8},{29,16},{13,18},{3,14}},Shadow); Painter.Chamfer(26,5,12,10,2,Body); Painter.Rect(58,7,23,10,Mid); Painter.Rect(81,9,14,6,Highlight);
			Painter.Circle({93,12},3.5f,Edge,18); Painter.Line({8,5},{25,5},1,Edge); Painter.Line({48,12},{81,12},1,Gold); return;

		case EWeaponIconProfile::ContinuousBeamConvergence:
			Painter.Polygon({{2,10},{13,6},{29,8},{29,16},{12,18},{2,14}},Shadow); Painter.Chamfer(26,4,14,12,2,Body); Painter.Polygon({{53,4},{80,8},{96,12},{80,16},{53,20}},Highlight);
			Painter.Rect(78,7,16,10,Mid); Painter.Circle({94,12},5,Edge,20); Painter.Circle({94,12},2,Shadow,16); Painter.Line({9,5},{25,5},1,Edge); return;

		case EWeaponIconProfile::Shotgun:
			Painter.Polygon({{3,10},{18,8},{25,10},{19,16},{6,16}},Shadow); Painter.Chamfer(18,6,12,5,2,Body); Painter.Rect(43,6,48,3,Edge); Painter.Rect(43,11,48,3,Body); Painter.Rect(32,15,15,4,Highlight);
			Painter.Rect(48,14,7,9,Shadow); Painter.Box(88,5,8,11,1,Edge); return;

		case EWeaponIconProfile::ShotgunDense:
			Painter.Polygon({{2,11},{18,7},{26,10},{19,17},{5,17}},Shadow); Painter.Chamfer(18,5,15,8,2,Body); for(const float Y : {5.0f,9.0f,13.0f}) Painter.Rect(43,Y,47,2,Edge);
			Painter.Rect(34,16,13,7,Shadow); Painter.Box(88,4,8,16,1,Edge); return;

		case EWeaponIconProfile::ShotgunWide:
			Painter.Polygon({{3,11},{19,7},{28,10},{20,17},{6,17}},Shadow); Painter.Chamfer(19,5,14,9,2,Body); Painter.Polygon({{42,5},{72,8},{94,12},{72,16},{42,19}},Mid);
			Painter.Line({44,8},{88,12},1,Highlight); Painter.Rect(48,17,9,6,Shadow); Painter.Box(87,4,9,16,1,Edge); return;

		case EWeaponIconProfile::Homing:
			for(const float Y : {4.0f,12.0f,20.0f}) { Painter.Rect(12,Y-1.5f,57,3,Body); Painter.Polygon({{69,Y-2.5f},{86,Y},{69,Y+2.5f}},Edge); Painter.Polygon({{18,Y-1.5f},{12,Y-4.0f},{12,Y-1.5f}},Shadow); Painter.Polygon({{18,Y+1.5f},{12,Y+4.0f},{12,Y+1.5f}},Shadow); }
			Painter.Line({27,1},{27,23},1,Highlight); return;

		case EWeaponIconProfile::HomingRack:
			Painter.Chamfer(10,3,53,18,3,Body); Painter.Rect(19,5,37,2,Highlight); for(const float X : {69.0f,80.0f,91.0f}) { Painter.Polygon({{X-5,8},{X+2,8},{X+6,12},{X+2,16},{X-5,16}},Mid); Painter.Circle({X,12},2,Edge,12); }
			Painter.Rect(26,17,18,4,Shadow); Painter.Box(62,3,34,18,1,Edge); return;

		case EWeaponIconProfile::HomingWide:
			Painter.Polygon({{8,12},{25,4},{69,4},{93,12},{69,20},{25,20}},Body); Painter.Polygon({{22,9},{67,7},{83,12},{67,17},{22,15}},Mid); Painter.Polygon({{15,7},{8,3},{13,12}},Shadow); Painter.Polygon({{15,17},{8,21},{13,12}},Shadow);
			Painter.Circle({88,12},4,Edge,16); Painter.Circle({88,12},2,Shadow,12); return;

		case EWeaponIconProfile::Unknown:
			Painter.Chamfer(30,3,40,18,3,Body); Painter.Box(30,3,40,18,1,Edge); Painter.Line({36,7},{64,17},1,Highlight); return;

		case EWeaponIconProfile::Empty:
		default:
			return;
		}
	}
}

TArray<FBox2D> UMSWeaponIconWidget::GetSilhouetteBoxes(const FGameplayTag& T)
{
	TArray<FBox2D> B;
	// Normalized category geometry is retained as a stable test/fallback contract.
	if(T==MSTags::Weapon_Category_MachineGun)
	{
		B.Add(MakeIconBox(.02f,.40f,.16f,.62f)); B.Add(MakeIconBox(.16f,.36f,.56f,.60f)); B.Add(MakeIconBox(.56f,.42f,.98f,.52f)); B.Add(MakeIconBox(.30f,.60f,.42f,.95f));
	}
	else if(T==MSTags::Weapon_Category_BeamRifle)
	{
		B.Add(MakeIconBox(.02f,.44f,.16f,.64f)); B.Add(MakeIconBox(.16f,.40f,.58f,.62f)); B.Add(MakeIconBox(.24f,.22f,.44f,.36f)); B.Add(MakeIconBox(.58f,.44f,.98f,.54f));
	}
	else if(T==MSTags::Weapon_Category_Bazooka)
	{
		B.Add(MakeIconBox(.02f,.36f,.86f,.62f)); B.Add(MakeIconBox(.86f,.28f,.98f,.70f)); B.Add(MakeIconBox(.38f,.62f,.50f,.92f));
	}
	else if(T==MSTags::Weapon_Category_Melee)
	{
		B.Add(MakeIconBox(.44f,.02f,.56f,.58f)); B.Add(MakeIconBox(.28f,.58f,.72f,.68f)); B.Add(MakeIconBox(.45f,.68f,.55f,.96f));
	}
	else if(T==MSTags::Weapon_Category_Shield)
	{
		B.Add(MakeIconBox(.26f,.05f,.74f,.55f)); B.Add(MakeIconBox(.32f,.55f,.68f,.76f)); B.Add(MakeIconBox(.42f,.76f,.58f,.94f));
	}
	else if(T==MSTags::Weapon_Category_Supply)
	{
		B.Add(MakeIconBox(.34f,.14f,.66f,.30f)); B.Add(MakeIconBox(.18f,.30f,.82f,.90f));
	}
	else if(T==MSTags::Weapon_Category_ContinuousBeam)
	{
		B.Add(MakeIconBox(.02f,.42f,.42f,.62f)); B.Add(MakeIconBox(.42f,.34f,.80f,.68f)); B.Add(MakeIconBox(.80f,.24f,.94f,.78f));
	}
	else if(T==MSTags::Weapon_Category_Shotgun)
	{
		B.Add(MakeIconBox(.02f,.42f,.24f,.68f)); B.Add(MakeIconBox(.24f,.40f,.98f,.56f)); B.Add(MakeIconBox(.44f,.56f,.66f,.72f));
	}
	else if(T==MSTags::Weapon_Category_Homing)
	{
		B.Add(MakeIconBox(.12f,.40f,.74f,.60f)); B.Add(MakeIconBox(.74f,.44f,.94f,.56f)); B.Add(MakeIconBox(.10f,.24f,.22f,.40f)); B.Add(MakeIconBox(.10f,.60f,.22f,.76f));
	}
	else
	{
		B.Add(MakeIconBox(.30f,.30f,.70f,.70f));
	}
	return B;
}

FBox2D UMSWeaponIconWidget::CalculatePhotoCoverRect(const FVector2D& AllottedSize,const FVector2D& SourceSize)
{
	if(AllottedSize.X<=0.0f || AllottedSize.Y<=0.0f || SourceSize.X<=0.0f || SourceSize.Y<=0.0f)
	{
		return FBox2D(FVector2D::ZeroVector,FVector2D(FMath::Max(0.0f,AllottedSize.X),FMath::Max(0.0f,AllottedSize.Y)));
	}

	const double Scale=FMath::Max(AllottedSize.X/SourceSize.X,AllottedSize.Y/SourceSize.Y);
	const FVector2D DrawSize=SourceSize*Scale;
	const FVector2D Offset=(AllottedSize-DrawSize)*0.5f;
	return FBox2D(Offset,Offset+DrawSize);
}

void UMSWeaponIconWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	WidgetTree->RootWidget=WidgetTree->ConstructWidget<UCanvasPanel>();
}

void UMSWeaponIconWidget::SetWeapon(const UMSWeaponDataBase* InWeapon,const FLinearColor& InColor)
{
	bPhotoPresentation=false;
	PendingPhotoWeapon.Reset(); PendingPhotoRetries=0; PendingPhotoRetryAccumulator=0.0f;
	WeaponPhoto=nullptr; PhotoBrush.SetResourceObject(nullptr);
	Color=InColor;
	if(!InWeapon)
	{
		Category=FGameplayTag(); Boxes.Reset(); ProfileId=static_cast<uint8>(EWeaponIconProfile::Empty); bHasWeapon=false; return;
	}
	Category=InWeapon->CategoryTag; Boxes=GetSilhouetteBoxes(Category); ProfileId=static_cast<uint8>(ResolveWeaponProfile(InWeapon)); bHasWeapon=true;
}

void UMSWeaponIconWidget::SetIcon(const FGameplayTag& InCategory,const FLinearColor& InColor)
{
	bPhotoPresentation=false;
	PendingPhotoWeapon.Reset(); PendingPhotoRetries=0; PendingPhotoRetryAccumulator=0.0f;
	WeaponPhoto=nullptr; PhotoBrush.SetResourceObject(nullptr);
	Color=InColor; Category=InCategory; Boxes=GetSilhouetteBoxes(InCategory); ProfileId=static_cast<uint8>(ResolveCategoryProfile(InCategory));
	// Legacy callers intentionally request a category placeholder, including an invalid tag.
	bHasWeapon=true;
}

void UMSWeaponIconWidget::SetWeaponPhoto(const UMSWeaponDataBase* InWeapon,const FLinearColor& InColor)
{
	SetWeapon(InWeapon,InColor);
	bPhotoPresentation=true;
	PendingPhotoWeapon=const_cast<UMSWeaponDataBase*>(InWeapon);
	PendingPhotoRetries=InWeapon ? PhotoRetryLimit : 0;
	PendingPhotoRetryAccumulator=0.0f;
	if(GetWorld())
		if(auto* Photos=GetWorld()->GetSubsystem<UMSWeaponPhotoSubsystem>()) WeaponPhoto=Photos->GetPhoto(InWeapon);
	PhotoBrush.SetResourceObject(WeaponPhoto);PhotoBrush.DrawAs=ESlateBrushDrawType::Image;
	PhotoBrush.ImageSize=WeaponPhoto
		? FVector2D(WeaponPhoto->GetSizeX(),WeaponPhoto->GetSizeY())
		: FVector2D(384,192);
	if(WeaponPhoto) { PendingPhotoWeapon.Reset(); PendingPhotoRetries=0; PendingPhotoRetryAccumulator=0.0f; }
}

void UMSWeaponIconWidget::TryResolvePendingPhoto()
{
	if(!PendingPhotoWeapon.IsValid() || !GetWorld()) return;
	const UMSWeaponDataBase* ExpectedWeapon=PendingPhotoWeapon.Get();
	if(auto* Photos=GetWorld()->GetSubsystem<UMSWeaponPhotoSubsystem>())
	{
		if(UTexture2D* Photo=Photos->GetPhoto(ExpectedWeapon))
		{
			// GetPhoto is synchronous today, but keep the identity check next to
			// assignment so a future deferred renderer cannot apply an old slot.
			if(PendingPhotoWeapon.Get()!=ExpectedWeapon) return;
			WeaponPhoto=Photo;
			PhotoBrush.SetResourceObject(Photo); PhotoBrush.DrawAs=ESlateBrushDrawType::Image;
			PhotoBrush.ImageSize=FVector2D(Photo->GetSizeX(),Photo->GetSizeY());
			PendingPhotoWeapon.Reset(); PendingPhotoRetries=0; PendingPhotoRetryAccumulator=0.0f;
		}
	}
}

void UMSWeaponIconWidget::NativeTick(const FGeometry& MyGeometry,const float InDeltaTime)
{
	Super::NativeTick(MyGeometry,InDeltaTime);
	if(PendingPhotoRetries<=0 || !PendingPhotoWeapon.IsValid()) return;
	PendingPhotoRetryAccumulator+=FMath::Max(0.0f,InDeltaTime);
	if(PendingPhotoRetryAccumulator<PhotoRetryIntervalSeconds) return;
	PendingPhotoRetryAccumulator=0.0f;
	--PendingPhotoRetries;
	TryResolvePendingPhoto();
}

int32 UMSWeaponIconWidget::NativePaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
	const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const
{
	const int32 RetLayer=Super::NativePaint(Args,AllottedGeometry,MyCullingRect,OutDrawElements,LayerId,InWidgetStyle,bParentEnabled);
	const FVector2D Size=AllottedGeometry.GetLocalSize();
	if(WeaponPhoto && Size.X>1 && Size.Y>1)
	{
		const FBox2D PhotoRect=CalculatePhotoCoverRect(Size,PhotoBrush.ImageSize);
		const FVector2D DrawSize=PhotoRect.Max-PhotoRect.Min;
		// PhotoRect intentionally extends outside the widget for narrow catalog/detail
		// slots. Keep the crop local to this icon so it cannot bleed into labels or
		// the neighbouring deck controls.
		FSlateClippingZone ClipZone(AllottedGeometry);
		ClipZone.SetAlwaysClip(true);
		OutDrawElements.PushClip(ClipZone);
		FSlateDrawElement::MakeBox(OutDrawElements,RetLayer+1,
			AllottedGeometry.ToPaintGeometry(FVector2f(DrawSize),FSlateLayoutTransform(FVector2f(PhotoRect.Min))),
			&PhotoBrush,ESlateDrawEffect::None,FLinearColor::White);
		OutDrawElements.PopClip();
		return RetLayer+1;
	}
	if(bPhotoPresentation||Size.X<=1.0f||Size.Y<=1.0f||!bHasWeapon||Boxes.IsEmpty()) return RetLayer;
	MSPixel::FPainter Painter(AllottedGeometry,OutDrawElements,RetLayer+1,FVector2D(100.0f,24.0f));
	PaintMechanicalIcon(Painter,static_cast<EWeaponIconProfile>(ProfileId),Color); Painter.Finish(); return RetLayer+1;
}
