// Copyright NeoMSGO Project. All Rights Reserved.
//
// Real unversioned deck files (2026-09-24). UE omits top-level properties that equal the
// class default, so every earlier writer (Version == CurrentVersion at the time) left out
// Version. Such files used to load as today's v4 and were rejected because an old slot
// still held its shield. Fixtures are byte copies of the normal profile's
// NeoMSGO_Deck_v1.sav (v1/v2 era) and the verification profile's (v3 era); they contain
// only deck asset paths. No save slot is read or written by these tests.

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Core/MSGameplayTags.h"
#include "Data/MSModuleData.h"
#include "Data/MSMobileSuitData.h"
#include "Data/MSWeaponData.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Base64.h"
#include "Misc/ScopeExit.h"
#include "Player/MSPlayerCharacter.h"
#include "UI/MSDeckSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "Weapons/MSWeaponComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckUnversionedLegacyFileTest,
	"NeoMSGO.UI.Deck.Persistence.UnversionedLegacyFiles",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDeckCategoryOnlyShieldMigrationTest,
	"NeoMSGO.UI.Deck.Persistence.CategoryOnlyShieldMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSWeaponSlotRejectsNativeShieldTest,
	"NeoMSGO.Weapons.WeaponSlotRejectsNativeShield",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
	// Normal profile Saved/SaveGames/NeoMSGO_Deck_v1.sav, last written 2026-09-12 (no Version,
	// EquippedModule or bShieldEquipped tags). Slot 3 is Dreadnought with FS-3 as weapon 3.
	const TCHAR* const UserDeckV1V2Base64 =
		TEXT("R1ZBUwMAAAAKAgAA+gMAAAUABwAEAEbAEQMSAAAAKytVRTUrUmVsZWFzZS01LjcAAwAAAFkAAAAi1VScvk8mqEYHIZTQgrRhLAAAAKNckWL3S44cxxIOo/ed")
		TEXT("IcgrAAAAJA1AzHtO6eCDovmbJ8DA3AAAAADkMtiwDU+JH7d+z6yiSv02CgAAAChDxuFTTSyiho5so4y9F2QAAAAAPMFeN/tI5AbwhAC1fnEqJgQAAADtaLDk")
		TEXT("6UKU9AvaMaJBu0YuKAAAAD90/M+ARLBD3xSRk3MgHRclAAAAtUkrsOlEILu3MgSjYAPkUgMAAABcEOSktUmhWcRAxafu335UAAAAAMkxyDncR+ZaF5xEmnyO")
		TEXT("HD4AAAAAMxvweJhP6uvqhLS5olq5zBQAAAAPODFm4ENNLSfPCYBaqVZpAAAAAJ+L+BL8SnWIDNl8pim9OjgxAAAATOdaexBMcNKYV1ipWiohCw0AAAAYaSnX")
		TEXT("3UvWHahk4p2EOME8AwAAAHhSocL+Sue//5AXbFX3HVMBAAAA1KOsbsFM7EDti4a3xY9CCQMAAADddeUpJ0aj4HbSEJ3q3CwjEQAAAF2mQ69HSdN/jj5zmAW7")
		TEXT("wdkPAAAA7Gwma49Lxx7Z5AujB/xCCQEAAABhPfcN6kc/oumJJ7eaSUEMAQAAAIYYHWCET2Ss3tMWqtbH6g3hAAAAWyy8jeBDp1S7/GinYJCifQgAAAC3Bkxb")
		TEXT("+EpjJHC/W4Dd0PXNDwAAAGhjCOdYTCNrcBs5hJFeJhYRAAAA1rz/nVgBT0mCEiHiiKiSPAsAAACs0K7yb0H+mn+qZIb81ib6AQAAAAsfTxelRca06C4/sX2R")
		TEXT("+9AKAAAAg0r5NWxAWOL1CRijfCQQlikAAABuwY+24kIbi1whU7T+RIgFAQAAAAaF4bLCz3NCu/ROpQe6i3UBAAAANon1ZLpCG/2Jcpa6TvrQ1QEAAACB1X1p")
		TEXT("q0FP5uxRSqootre+eQAAAEJem9hGTb0kqKwShHkXZN89AAAAUl3aWUhJMhJ4WXi4i+m4cAkAAAAn2A5vlUgJpo2ZkZykDhiQAgAAAOOL1TCCQuqVWbHjpmqw")
		TEXT("69gBAAAA555/cTpJsOkykbOIB4E4GxYAAACqHB7iXkJHr9Rqv4m7qERMAAAAAH4VShOjSeLVPIROjTGe/pgDAAAA/AnEaIZJlXDSrGOJg1GGxAMAAAAuIBOV")
		TEXT("ebKh67rlf/Hyw5CrBAAAALPcfY67R9qAokbTn/ZNmJMBAAAAQ4xzkpxNiCm+mz2awJ//bgMAAACWUZar/AjYRY0i17eeVq14AQAAAM2wisveS4znkxNiqGLv")
		TEXT("6RQAAAAA8gpo+6NL71m1Gai6PUTIcwIAAACRhuCvUkkNOjtnc7YeLfJ8AgAAAL39tS4QTawBj/M2gdqlkzMFAAAATzWdUC9J5vayhUmnHGM8BwAAAADqt2Kk")
		TEXT("Ok6Z9B/swZmy4SSCBAAAABlNDENwSVRxaZtph+WwkN8PAAAAvTL+qhRMlVMlXmq23dEyEAEAAAB4VjQSeFY0EnhWNBJ4VjQSBQAAAI7hryNYTuFMUsJhjbe+")
		TEXT("U7kLAAAAHBvjtuwRn9KFn36F4nCZbwEAAABA61ZK3BH1EH4005LnasmyAwAAAIqZF4TsQ8C7GdGzgSInLQcTAAAAAEqK15dGWOi1Gai6tEZ9SBIAAACG+HlV")
		TEXT("H0w6k3sIuoMvuWFjAgAAAFK+L2ELQFPakU8NkXyFsZ8BAAAAAVPSefhBr0fnEM+tFlxBlwIAAAA2eiOkyUHqyvgYoo/zG2hYBQAAAHU/ToBJS4hwBozWpNy2")
		TEXT("fjwFAAAA9EjQHmhMLi+kU9CJLRCP8QEAAAApI6V2tUUjCUHYrpjYai/PBQAAAAdpvF+uQMhVhPFnjj/x/14BAAAA56MEY1lPWQC9IfyMTv0hdwAAAAD6evX8")
		TEXT("g0J2UFjmqbkyLaD/XQAAAPN6uySDT0ZWwi0vH/+WrUkFAAAADrdQmRdOGrQN+sy71n+BVwEAAAAxE+IoI0cfUGT6EIEe2hDqEgAAAN7wxGA0TCaLAXKTqswJ")
		TEXT("/10BAAAAJ8ImVLJFMUsf7ZubJrF/MgEAAACblUnc503AU4jqVpE5XXxeEwAAAPsMgqdZQ6cgFCxUjFDPI5YoAAAAquHtk3VzyhAoivlNoFexSQ4AAAB4u9/2")
		TEXT("5KBQu024GEAjr8tgCwAAAJi9552yRdRnc50OjGfj4f0AAAAAYgMnwR8KI6s2xx7Lq18ncQEAAABOfOeCpUMjM8UTa7TzDTGXAAAAACKsKCz+Rs8VEfAZvQU8")
		TEXT("KmUBAAAARy6dtgNAqOKkGHe/mdjEkgEAAADtCjERYU1VLqOaZ68sCKHFEQAAAIhE5wQXh6xLlIaxu87z+DkAAAAADZJjJzlLeA+zS26YbWaNqBsAAABgBfym")
		TEXT("SDO4RYrh5dBFzSkxAQAAALOyoYERdxKvsrAR4glUIx0BAAAAHwAAAC9TY3JpcHQvTmVvTVNHTy5NU0RlY2tTYXZlR2FtZQAABgAAAFNsb3RzAA4AAABBcnJh")
		TEXT("eVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAABMAAABNU0RlY2tTbG90U2F2ZURhdGEAAQAAABAAAAAvU2NyaXB0L05lb01TR08AAAAAAPwI")
		TEXT("AAAABAAAAAsAAABNb2JpbGVTdWl0AA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVj")
		TEXT("dAAAAAAAUQAAAAguAAAAL0dhbWUvRGF0YS9Nb2JpbGVTdWl0cy9EQV9NU19Bc3NhdWx0Q29tbWFuZGVyABcAAABEQV9NU19Bc3NhdWx0Q29tbWFuZGVyAAAA")
		TEXT("AAAQAAAARXF1aXBwZWRXZWFwb25zAA4AAABBcnJheVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAA")
		TEXT("FAAAAC9TY3JpcHQvQ29yZVVPYmplY3QAAAAAAP8AAAAIAwAAACsAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0hvbWluZ01pc3NpbGUAGAAAAERB")
		TEXT("X1dlYXBvbl9Ib21pbmdNaXNzaWxlAAAAAAAwAAAAL0dhbWUvRGF0YS9XZWFwb25zL0RBX1dlYXBvbl9NdWx0aUhvbWluZ01pc3NpbGUAHQAAAERBX1dlYXBv")
		TEXT("bl9NdWx0aUhvbWluZ01pc3NpbGUAAAAAAC0AAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0JlYW1MYXVuY2hlckJMNAAaAAAAREFfV2VhcG9uX0Jl")
		TEXT("YW1MYXVuY2hlckJMNAAAAAAABQAAAE5vbmUACwAAAE1vYmlsZVN1aXQADwAAAFN0cnVjdFByb3BlcnR5AAEAAAAPAAAAU29mdE9iamVjdFBhdGgAAQAAABQA")
		TEXT("AAAvU2NyaXB0L0NvcmVVT2JqZWN0AAAAAABLAAAACCsAAAAvR2FtZS9EYXRhL01vYmlsZVN1aXRzL0RBX01TX1RBVXJ1c0Fzc2F1bHQAFAAAAERBX01TX1RB")
		TEXT("VXJ1c0Fzc2F1bHQAAAAAABAAAABFcXVpcHBlZFdlYXBvbnMADgAAAEFycmF5UHJvcGVydHkAAQAAAA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRP")
		TEXT("YmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVjdAAAAAAAJQEAAAgDAAAAOgAAAC9HYW1lL0RhdGEvV2VhcG9ucy9EQV9XZWFwb25fVEFVcnVz")
		TEXT("X0NoYXJnZUJsYXN0ZXJMYXVuY2hlcgAnAAAAREFfV2VhcG9uX1RBVXJ1c19DaGFyZ2VCbGFzdGVyTGF1bmNoZXIAAAAAADAAAAAvR2FtZS9EYXRhL1dlYXBv")
		TEXT("bnMvREFfV2VhcG9uX011bHRpSG9taW5nTWlzc2lsZQAdAAAAREFfV2VhcG9uX011bHRpSG9taW5nTWlzc2lsZQAAAAAAMQAAAC9HYW1lL0RhdGEvV2VhcG9u")
		TEXT("cy9EQV9XZWFwb25fQ2hhcmdlR3JlYXRTd29yZENTMQAeAAAAREFfV2VhcG9uX0NoYXJnZUdyZWF0U3dvcmRDUzEAAAAAAAUAAABOb25lAAsAAABNb2JpbGVT")
		TEXT("dWl0AA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVjdAAAAAAAUQAAAAguAAAAL0dh")
		TEXT("bWUvRGF0YS9Nb2JpbGVTdWl0cy9EQV9NU19EcmVhZG5vdWdodEhlYXZ5ABcAAABEQV9NU19EcmVhZG5vdWdodEhlYXZ5AAAAAAAQAAAARXF1aXBwZWRXZWFw")
		TEXT("b25zAA4AAABBcnJheVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAAFAAAAC9TY3JpcHQvQ29yZVVP")
		TEXT("YmplY3QAAAAAAAUBAAAIAwAAACwAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0hlYXZ5TW9ydGFyRVg1ABkAAABEQV9XZWFwb25fSGVhdnlNb3J0")
		TEXT("YXJFWDUAAAAAADAAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX1Nob3VsZGVyQmF6b29rYVNCOQAdAAAAREFfV2VhcG9uX1Nob3VsZGVyQmF6b29r")
		TEXT("YVNCOQAAAAAALwAAAC9HYW1lL0RhdGEvV2VhcG9ucy9EQV9XZWFwb25fRm9ydHJlc3NTaGllbGRGUzMAHAAAAERBX1dlYXBvbl9Gb3J0cmVzc1NoaWVsZEZT")
		TEXT("MwAAAAAABQAAAE5vbmUACwAAAE1vYmlsZVN1aXQADwAAAFN0cnVjdFByb3BlcnR5AAEAAAAPAAAAU29mdE9iamVjdFBhdGgAAQAAABQAAAAvU2NyaXB0L0Nv")
		TEXT("cmVVT2JqZWN0AAAAAAA9AAAACCQAAAAvR2FtZS9EYXRhL01vYmlsZVN1aXRzL0RBX01TX1RBVXJ1cwANAAAAREFfTVNfVEFVcnVzAAAAAAAQAAAARXF1aXBw")
		TEXT("ZWRXZWFwb25zAA4AAABBcnJheVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAAFAAAAC9TY3JpcHQv")
		TEXT("Q29yZVVPYmplY3QAAAAAACUBAAAIAwAAADoAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX1RBVXJ1c19DaGFyZ2VCbGFzdGVyTGF1bmNoZXIAJwAA")
		TEXT("AERBX1dlYXBvbl9UQVVydXNfQ2hhcmdlQmxhc3RlckxhdW5jaGVyAAAAAAAwAAAAL0dhbWUvRGF0YS9XZWFwb25zL0RBX1dlYXBvbl9NdWx0aUhvbWluZ01p")
		TEXT("c3NpbGUAHQAAAERBX1dlYXBvbl9NdWx0aUhvbWluZ01pc3NpbGUAAAAAADEAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0NoYXJnZUdyZWF0U3dv")
		TEXT("cmRDUzEAHgAAAERBX1dlYXBvbl9DaGFyZ2VHcmVhdFN3b3JkQ1MxAAAAAAAFAAAATm9uZQAFAAAATm9uZQAAAAAA");
	// Saved/CodexRespawn20260918/AutomationUser deck (EquippedModule tags, no Version).
	// Breaker holds LS-2 and Dreadnought holds FS-3 as weapon 3.
	const TCHAR* const VerifyDeckV3Base64 =
		TEXT("R1ZBUwMAAAAKAgAA+gMAAAUABwAEAEbAEQMSAAAAKytVRTUrUmVsZWFzZS01LjcAAwAAAFkAAAAi1VScvk8mqEYHIZTQgrRhLAAAAKNckWL3S44cxxIOo/ed")
		TEXT("IcgrAAAAJA1AzHtO6eCDovmbJ8DA3AAAAADkMtiwDU+JH7d+z6yiSv02CgAAAChDxuFTTSyiho5so4y9F2QAAAAAPMFeN/tI5AbwhAC1fnEqJgQAAADtaLDk")
		TEXT("6UKU9AvaMaJBu0YuKAAAAD90/M+ARLBD3xSRk3MgHRclAAAAtUkrsOlEILu3MgSjYAPkUgMAAABcEOSktUmhWcRAxafu335UAAAAAMkxyDncR+ZaF5xEmnyO")
		TEXT("HD4AAAAAMxvweJhP6uvqhLS5olq5zBQAAAAPODFm4ENNLSfPCYBaqVZpAAAAAJ+L+BL8SnWIDNl8pim9OjgxAAAATOdaexBMcNKYV1ipWiohCw0AAAAYaSnX")
		TEXT("3UvWHahk4p2EOME8AwAAAHhSocL+Sue//5AXbFX3HVMBAAAA1KOsbsFM7EDti4a3xY9CCQMAAADddeUpJ0aj4HbSEJ3q3CwjEQAAAF2mQ69HSdN/jj5zmAW7")
		TEXT("wdkPAAAA7Gwma49Lxx7Z5AujB/xCCQEAAABhPfcN6kc/oumJJ7eaSUEMAQAAAIYYHWCET2Ss3tMWqtbH6g3hAAAAWyy8jeBDp1S7/GinYJCifQgAAAC3Bkxb")
		TEXT("+EpjJHC/W4Dd0PXNDwAAAGhjCOdYTCNrcBs5hJFeJhYRAAAA1rz/nVgBT0mCEiHiiKiSPAsAAACs0K7yb0H+mn+qZIb81ib6AQAAAAsfTxelRca06C4/sX2R")
		TEXT("+9AKAAAAg0r5NWxAWOL1CRijfCQQlikAAABuwY+24kIbi1whU7T+RIgFAQAAAAaF4bLCz3NCu/ROpQe6i3UBAAAANon1ZLpCG/2Jcpa6TvrQ1QEAAACB1X1p")
		TEXT("q0FP5uxRSqootre+eQAAAEJem9hGTb0kqKwShHkXZN89AAAAUl3aWUhJMhJ4WXi4i+m4cAkAAAAn2A5vlUgJpo2ZkZykDhiQAgAAAOOL1TCCQuqVWbHjpmqw")
		TEXT("69gBAAAA555/cTpJsOkykbOIB4E4GxYAAACqHB7iXkJHr9Rqv4m7qERMAAAAAH4VShOjSeLVPIROjTGe/pgDAAAA/AnEaIZJlXDSrGOJg1GGxAMAAAAuIBOV")
		TEXT("ebKh67rlf/Hyw5CrBAAAALPcfY67R9qAokbTn/ZNmJMBAAAAQ4xzkpxNiCm+mz2awJ//bgMAAACWUZar/AjYRY0i17eeVq14AQAAAM2wisveS4znkxNiqGLv")
		TEXT("6RQAAAAA8gpo+6NL71m1Gai6PUTIcwIAAACRhuCvUkkNOjtnc7YeLfJ8AgAAAL39tS4QTawBj/M2gdqlkzMFAAAATzWdUC9J5vayhUmnHGM8BwAAAADqt2Kk")
		TEXT("Ok6Z9B/swZmy4SSCBAAAABlNDENwSVRxaZtph+WwkN8PAAAAvTL+qhRMlVMlXmq23dEyEAEAAAB4VjQSeFY0EnhWNBJ4VjQSBQAAAI7hryNYTuFMUsJhjbe+")
		TEXT("U7kLAAAAHBvjtuwRn9KFn36F4nCZbwEAAABA61ZK3BH1EH4005LnasmyAwAAAIqZF4TsQ8C7GdGzgSInLQcTAAAAAEqK15dGWOi1Gai6tEZ9SBIAAACG+HlV")
		TEXT("H0w6k3sIuoMvuWFjAgAAAFK+L2ELQFPakU8NkXyFsZ8BAAAAAVPSefhBr0fnEM+tFlxBlwIAAAA2eiOkyUHqyvgYoo/zG2hYBQAAAHU/ToBJS4hwBozWpNy2")
		TEXT("fjwFAAAA9EjQHmhMLi+kU9CJLRCP8QEAAAApI6V2tUUjCUHYrpjYai/PBQAAAAdpvF+uQMhVhPFnjj/x/14BAAAA56MEY1lPWQC9IfyMTv0hdwAAAADzersk")
		TEXT("g09GVsItLx//lq1JBQAAAPp69fyDQnZQWOapuTItoP9dAAAADrdQmRdOGrQN+sy71n+BVwEAAACblUnc503AU4jqVpE5XXxeEwAAAPsMgqdZQ6cgFCxUjFDP")
		TEXT("I5YoAAAAquHtk3VzyhAoivlNoFexSQ4AAAB4u9/25KBQu024GEAjr8tgCwAAAJi9552yRdRnc50OjGfj4f0AAAAARy6dtgNAqOKkGHe/mdjEkgEAAAAxE+Io")
		TEXT("I0cfUGT6EIEe2hDqEgAAAN7wxGA0TCaLAXKTqswJ/10BAAAAJ8ImVLJFMUsf7ZubJrF/MgEAAABiAyfBHwojqzbHHsurXydxAQAAAO0KMRFhTVUuo5pnrywI")
		TEXT("ocURAAAATnzngqVDIzPFE2u08w0xlwAAAAAirCgs/kbPFRHwGb0FPCplAQAAAIhE5wQXh6xLlIaxu87z+DkAAAAADZJjJzlLeA+zS26YbWaNqBsAAACzsqGB")
		TEXT("EXcSr7KwEeIJVCMdAQAAAGAF/KZIM7hFiuHl0EXNKTEBAAAAHwAAAC9TY3JpcHQvTmVvTVNHTy5NU0RlY2tTYXZlR2FtZQAABgAAAFNsb3RzAA4AAABBcnJh")
		TEXT("eVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAABMAAABNU0RlY2tTbG90U2F2ZURhdGEAAQAAABAAAAAvU2NyaXB0L05lb01TR08AAAAAAKQK")
		TEXT("AAAABAAAAAsAAABNb2JpbGVTdWl0AA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVj")
		TEXT("dAAAAAAAUQAAAAguAAAAL0dhbWUvRGF0YS9Nb2JpbGVTdWl0cy9EQV9NU19Bc3NhdWx0Q29tbWFuZGVyABcAAABEQV9NU19Bc3NhdWx0Q29tbWFuZGVyAAAA")
		TEXT("AAAQAAAARXF1aXBwZWRXZWFwb25zAA4AAABBcnJheVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAA")
		TEXT("FAAAAC9TY3JpcHQvQ29yZVVPYmplY3QAAAAAAP8AAAAIAwAAACsAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0hvbWluZ01pc3NpbGUAGAAAAERB")
		TEXT("X1dlYXBvbl9Ib21pbmdNaXNzaWxlAAAAAAAwAAAAL0dhbWUvRGF0YS9XZWFwb25zL0RBX1dlYXBvbl9NdWx0aUhvbWluZ01pc3NpbGUAHQAAAERBX1dlYXBv")
		TEXT("bl9NdWx0aUhvbWluZ01pc3NpbGUAAAAAAC0AAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0JlYW1MYXVuY2hlckJMNAAaAAAAREFfV2VhcG9uX0Jl")
		TEXT("YW1MYXVuY2hlckJMNAAAAAAADwAAAEVxdWlwcGVkTW9kdWxlAA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1Nj")
		TEXT("cmlwdC9Db3JlVU9iamVjdAAAAAAAFgAAAAgFAAAATm9uZQAFAAAATm9uZQAAAAAABQAAAE5vbmUACwAAAE1vYmlsZVN1aXQADwAAAFN0cnVjdFByb3BlcnR5")
		TEXT("AAEAAAAPAAAAU29mdE9iamVjdFBhdGgAAQAAABQAAAAvU2NyaXB0L0NvcmVVT2JqZWN0AAAAAABNAAAACCwAAAAvR2FtZS9EYXRhL01vYmlsZVN1aXRzL0RB")
		TEXT("X01TX0JyZWFrZXJBc3NhdWx0ABUAAABEQV9NU19CcmVha2VyQXNzYXVsdAAAAAAAEAAAAEVxdWlwcGVkV2VhcG9ucwAOAAAAQXJyYXlQcm9wZXJ0eQABAAAA")
		TEXT("DwAAAFN0cnVjdFByb3BlcnR5AAEAAAAPAAAAU29mdE9iamVjdFBhdGgAAQAAABQAAAAvU2NyaXB0L0NvcmVVT2JqZWN0AAAAAADzAAAACAMAAAAuAAAAL0dh")
		TEXT("bWUvRGF0YS9XZWFwb25zL0RBX1dlYXBvbl9Bc3NhdWx0VnVsY2FuQVYyABsAAABEQV9XZWFwb25fQXNzYXVsdFZ1bGNhbkFWMgAAAAAAKAAAAC9HYW1lL0Rh")
		TEXT("dGEvV2VhcG9ucy9EQV9XZWFwb25fSGVhdEF4ZUhBMgAVAAAAREFfV2VhcG9uX0hlYXRBeGVIQTIAAAAAACwAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2Vh")
		TEXT("cG9uX0xpZ2h0U2hpZWxkTFMyABkAAABEQV9XZWFwb25fTGlnaHRTaGllbGRMUzIAAAAAAA8AAABFcXVpcHBlZE1vZHVsZQAPAAAAU3RydWN0UHJvcGVydHkA")
		TEXT("AQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAAFAAAAC9TY3JpcHQvQ29yZVVPYmplY3QAAAAAABYAAAAIBQAAAE5vbmUABQAAAE5vbmUAAAAAAAUAAABOb25l")
		TEXT("AAsAAABNb2JpbGVTdWl0AA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVjdAAAAAAA")
		TEXT("UQAAAAguAAAAL0dhbWUvRGF0YS9Nb2JpbGVTdWl0cy9EQV9NU19EcmVhZG5vdWdodEhlYXZ5ABcAAABEQV9NU19EcmVhZG5vdWdodEhlYXZ5AAAAAAAQAAAA")
		TEXT("RXF1aXBwZWRXZWFwb25zAA4AAABBcnJheVByb3BlcnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAAFAAAAC9T")
		TEXT("Y3JpcHQvQ29yZVVPYmplY3QAAAAAAAUBAAAIAwAAACwAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX0hlYXZ5TW9ydGFyRVg1ABkAAABEQV9XZWFw")
		TEXT("b25fSGVhdnlNb3J0YXJFWDUAAAAAADAAAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX1Nob3VsZGVyQmF6b29rYVNCOQAdAAAAREFfV2VhcG9uX1No")
		TEXT("b3VsZGVyQmF6b29rYVNCOQAAAAAALwAAAC9HYW1lL0RhdGEvV2VhcG9ucy9EQV9XZWFwb25fRm9ydHJlc3NTaGllbGRGUzMAHAAAAERBX1dlYXBvbl9Gb3J0")
		TEXT("cmVzc1NoaWVsZEZTMwAAAAAADwAAAEVxdWlwcGVkTW9kdWxlAA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1Nj")
		TEXT("cmlwdC9Db3JlVU9iamVjdAAAAAAAFgAAAAgFAAAATm9uZQAFAAAATm9uZQAAAAAABQAAAE5vbmUACwAAAE1vYmlsZVN1aXQADwAAAFN0cnVjdFByb3BlcnR5")
		TEXT("AAEAAAAPAAAAU29mdE9iamVjdFBhdGgAAQAAABQAAAAvU2NyaXB0L0NvcmVVT2JqZWN0AAAAAABVAAAACDAAAAAvR2FtZS9EYXRhL01vYmlsZVN1aXRzL0RB")
		TEXT("X01TX0ZpZWxkS2VlcGVyU3VwcG9ydAAZAAAAREFfTVNfRmllbGRLZWVwZXJTdXBwb3J0AAAAAAAQAAAARXF1aXBwZWRXZWFwb25zAA4AAABBcnJheVByb3Bl")
		TEXT("cnR5AAEAAAAPAAAAU3RydWN0UHJvcGVydHkAAQAAAA8AAABTb2Z0T2JqZWN0UGF0aAABAAAAFAAAAC9TY3JpcHQvQ29yZVVPYmplY3QAAAAAAAUBAAAIAwAA")
		TEXT("AC8AAAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX1JlcGFpclBvZERlcGxveWVyABwAAABEQV9XZWFwb25fUmVwYWlyUG9kRGVwbG95ZXIAAAAAADAA")
		TEXT("AAAvR2FtZS9EYXRhL1dlYXBvbnMvREFfV2VhcG9uX1N1cHBseVBhY2tEZXBsb3llcgAdAAAAREFfV2VhcG9uX1N1cHBseVBhY2tEZXBsb3llcgAAAAAALAAA")
		TEXT("AC9HYW1lL0RhdGEvV2VhcG9ucy9EQV9XZWFwb25fQmVhbU1hY2hpbmVHdW4AGQAAAERBX1dlYXBvbl9CZWFtTWFjaGluZUd1bgAAAAAADwAAAEVxdWlwcGVk")
		TEXT("TW9kdWxlAA8AAABTdHJ1Y3RQcm9wZXJ0eQABAAAADwAAAFNvZnRPYmplY3RQYXRoAAEAAAAUAAAAL1NjcmlwdC9Db3JlVU9iamVjdAAAAAAAFgAAAAgFAAAA")
		TEXT("Tm9uZQAFAAAATm9uZQAAAAAABQAAAE5vbmUABQAAAE5vbmUAAAAAAA==");

	TArray<uint8> DecodeFixture(const TCHAR* Base64)
	{
		TArray<uint8> Bytes;
		FBase64::Decode(FString(Base64), Bytes);
		return Bytes;
	}

	UMSDeckSubsystem* MakeLegacyTestDeck()
	{
		UGameInstance* GameInstance = NewObject<UGameInstance>(GEngine);
		return NewObject<UMSDeckSubsystem>(GameInstance);
	}

	FString WeaponName(const FMSDeckLoadout& Loadout, const int32 Index)
	{
		return Loadout.EquippedWeapons.IsValidIndex(Index) && !Loadout.EquippedWeapons[Index].IsNull()
			? Loadout.EquippedWeapons[Index].ToSoftObjectPath().GetAssetName() : FString();
	}

	FString SuitName(const UMSDeckSubsystem& Deck, const int32 Slot)
	{
		const UMSMobileSuitData* Suit = Deck.GetPresetInSlot(Slot);
		return Suit ? Suit->GetName() : FString();
	}

	bool HasShieldInWeaponSlots(const UMSDeckSubsystem& Deck)
	{
		for (int32 Slot = 0; Slot < UMSDeckSubsystem::SlotCount; ++Slot)
		{
			for (const TSoftObjectPtr<UMSWeaponDataBase>& Weapon : Deck.GetLoadoutInSlot(Slot).EquippedWeapons)
			{
				const UMSWeaponDataBase* Loaded = Weapon.LoadSynchronous();
				if (Loaded && (Loaded->IsA<UMSShieldWeaponData>() || Loaded->CategoryTag == MSTags::Weapon_Category_Shield))
				{
					return true;
				}
			}
		}
		return false;
	}
}

bool FMSDeckUnversionedLegacyFileTest::RunTest(const FString&)
{
	const TArray<uint8> UserBytes = DecodeFixture(UserDeckV1V2Base64);
	const TArray<uint8> VerifyBytes = DecodeFixture(VerifyDeckV3Base64);
	if (!TestEqual(TEXT("user fixture decodes to the original 4296 bytes"), UserBytes.Num(), 4296)
		|| !TestEqual(TEXT("verification fixture decodes to the original 4720 bytes"), VerifyBytes.Num(), 4720))
	{
		return false;
	}

	// The defect: without a Version tag the loaded object reports today's default.
	TStrongObjectPtr<USaveGame> Raw(UGameplayStatics::LoadGameFromMemory(UserBytes));
	const UMSDeckSaveGame* RawDeck = Cast<UMSDeckSaveGame>(Raw.Get());
	if (!TestNotNull(TEXT("user fixture is a deck save"), RawDeck)) return false;
	TestEqual(TEXT("absent Version loads as the current default"), RawDeck->Version, UMSDeckSaveGame::CurrentVersion);
	TStrongObjectPtr<UMSDeckSubsystem> OldBehaviour(MakeLegacyTestDeck());
	TestFalse(TEXT("reading it as v4 rejects the whole deck (old behaviour)"),
		OldBehaviour->RestoreSaveGameForAutomationTests(RawDeck));

	TestEqual(TEXT("user file is inferred as a pre-module v1/v2 save"),
		UMSDeckSubsystem::ResolveSerializedSaveVersion(UserBytes, RawDeck->Version), 1);
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeLegacyTestDeck());
	if (!TestTrue(TEXT("user file now migrates"), Deck->RestoreSaveBytesForAutomationTests(UserBytes))) return false;
	TestFalse(TEXT("no native shield remains in any weapon slot"), HasShieldInWeaponSlots(*Deck));
	const FMSDeckLoadout Assault = Deck->GetLoadoutInSlot(0);
	TestTrue(TEXT("slot 1 keeps its three weapons"), SuitName(*Deck, 0) == TEXT("DA_MS_AssaultCommander")
		&& WeaponName(Assault, 0) == TEXT("DA_Weapon_HomingMissile")
		&& WeaponName(Assault, 1) == TEXT("DA_Weapon_MultiHomingMissile")
		&& WeaponName(Assault, 2) == TEXT("DA_Weapon_BeamLauncherBL4") && !Assault.bShieldEquipped);
	const FMSDeckLoadout Dreadnought = Deck->GetLoadoutInSlot(2);
	TestTrue(TEXT("Dreadnought keeps EX-5/SB-9, weapon 3 stays empty and FS-3 becomes the native shield"),
		SuitName(*Deck, 2) == TEXT("DA_MS_DreadnoughtHeavy")
		&& WeaponName(Dreadnought, 0) == TEXT("DA_Weapon_HeavyMortarEX5")
		&& WeaponName(Dreadnought, 1) == TEXT("DA_Weapon_ShoulderBazookaSB9")
		&& WeaponName(Dreadnought, 2).IsEmpty() && Dreadnought.bShieldEquipped);
	TestTrue(TEXT("TAU slots are untouched"), SuitName(*Deck, 1) == TEXT("DA_MS_TAUrusAssault")
		&& SuitName(*Deck, 3) == TEXT("DA_MS_TAUrus")
		&& WeaponName(Deck->GetLoadoutInSlot(1), 0) == TEXT("DA_Weapon_TAUrus_ChargeBlasterLauncher")
		&& WeaponName(Deck->GetLoadoutInSlot(3), 2) == TEXT("DA_Weapon_ChargeGreatSwordCS1"));

	// v3-era verification profile file.
	TStrongObjectPtr<USaveGame> RawVerify(UGameplayStatics::LoadGameFromMemory(VerifyBytes));
	const UMSDeckSaveGame* RawVerifyDeck = Cast<UMSDeckSaveGame>(RawVerify.Get());
	TestEqual(TEXT("verification file is inferred as v3"), UMSDeckSubsystem::ResolveSerializedSaveVersion(
		VerifyBytes, RawVerifyDeck ? RawVerifyDeck->Version : -1), 3);
	TStrongObjectPtr<UMSDeckSubsystem> VerifyDeck(MakeLegacyTestDeck());
	if (TestTrue(TEXT("verification file now migrates"), VerifyDeck->RestoreSaveBytesForAutomationTests(VerifyBytes)))
	{
		TestFalse(TEXT("verification deck has no shield in weapon slots"), HasShieldInWeaponSlots(*VerifyDeck));
		const FMSDeckLoadout Breaker = VerifyDeck->GetLoadoutInSlot(1);
		TestTrue(TEXT("Breaker keeps AV-2/HA-2 and gets LS-2 as the native shield"),
			WeaponName(Breaker, 0) == TEXT("DA_Weapon_AssaultVulcanAV2")
			&& WeaponName(Breaker, 1) == TEXT("DA_Weapon_HeatAxeHA2")
			&& WeaponName(Breaker, 2).IsEmpty() && Breaker.bShieldEquipped);
		TestTrue(TEXT("FieldKeeper weapons unchanged"),
			WeaponName(VerifyDeck->GetLoadoutInSlot(3), 0) == TEXT("DA_Weapon_RepairPodDeployer")
			&& WeaponName(VerifyDeck->GetLoadoutInSlot(3), 2) == TEXT("DA_Weapon_BeamMachineGun"));
	}

	// The current writer still omits Version (equal to the default) but always writes the
	// bShieldEquipped tag inside Slots, so its files keep v4 meaning.
	TStrongObjectPtr<UMSDeckSaveGame> Current(Deck->CreateSaveGameForAutomationTests());
	UMSModuleData* PersistedModule = LoadObject<UMSModuleData>(nullptr,
		TEXT("/Game/Data/Modules/DA_Module_Transzomm.DA_Module_Transzomm"));
	if (!TestNotNull(TEXT("module fixture for current save round trip"), PersistedModule)) return false;
	Current->Slots[0].EquippedModule = FSoftObjectPath(PersistedModule);
	Current->SelectedSlotIndex = 2;
	TArray<uint8> CurrentBytes;
	TestTrue(TEXT("current deck serializes"), UGameplayStatics::SaveGameToMemory(Current.Get(), CurrentBytes));
	TestEqual(TEXT("current writer is read as v5"),
		UMSDeckSubsystem::ResolveSerializedSaveVersion(CurrentBytes, UMSDeckSaveGame::CurrentVersion), 5);
	TStrongObjectPtr<UMSDeckSubsystem> RoundTrip(MakeLegacyTestDeck());
	TestTrue(TEXT("current writer round-trips"), RoundTrip->RestoreSaveBytesForAutomationTests(CurrentBytes));
	TestTrue(TEXT("round-trip keeps the migrated shield flag and empty weapon 3"),
		RoundTrip->GetLoadoutInSlot(2).bShieldEquipped && WeaponName(RoundTrip->GetLoadoutInSlot(2), 2).IsEmpty()
		&& WeaponName(RoundTrip->GetLoadoutInSlot(2), 0) == TEXT("DA_Weapon_HeavyMortarEX5"));
	TestEqual(TEXT("current raw save preserves a non-null module"), RoundTrip->GetLoadoutInSlot(0).EquippedModule.Get(), PersistedModule);
	TestEqual(TEXT("current raw save preserves a non-zero selected slot"), RoundTrip->GetSelectedSlotIndex(), 2);

	// An explicitly written version is used as-is.
	Current->Version = 2;
	TArray<uint8> ExplicitBytes;
	UGameplayStatics::SaveGameToMemory(Current.Get(), ExplicitBytes);
	TestEqual(TEXT("explicit Version tag wins"), UMSDeckSubsystem::ResolveSerializedSaveVersion(ExplicitBytes, 2), 2);
	return true;
}

bool FMSDeckCategoryOnlyShieldMigrationTest::RunTest(const FString&)
{
	TStrongObjectPtr<UMSDeckSubsystem> Deck(MakeLegacyTestDeck());
	UPackage* Package = CreatePackage(*FString::Printf(TEXT("/Engine/Transient/MSDeckCategoryOnlyShield_%s"),
		*FGuid::NewGuid().ToString()));
	UMSMobileSuitData* Suit = NewObject<UMSMobileSuitData>(Package, TEXT("CategoryOnlyShieldSuit"));
	UMSRangedWeaponData* GunA = NewObject<UMSRangedWeaponData>(Package, TEXT("CategoryOnlyShieldGunA"));
	UMSRangedWeaponData* GunB = NewObject<UMSRangedWeaponData>(Package, TEXT("CategoryOnlyShieldGunB"));
	UMSRangedWeaponData* CategoryOnlyShield = NewObject<UMSRangedWeaponData>(Package, TEXT("CategoryOnlyShield"));
	UMSShieldWeaponData* NativeShield = NewObject<UMSShieldWeaponData>(Package, TEXT("CategoryOnlyNativeShield"));
	CategoryOnlyShield->CategoryTag = MSTags::Weapon_Category_Shield;
	Suit->NativeShield = NativeShield;

	TStrongObjectPtr<UMSDeckSaveGame> Legacy(NewObject<UMSDeckSaveGame>());
	Legacy->Version = 3;
	Legacy->Slots.SetNum(UMSDeckSubsystem::SlotCount);
	Legacy->Slots[0].MobileSuit = FSoftObjectPath(Suit);
	Legacy->Slots[0].EquippedWeapons = {
		FSoftObjectPath(GunA), FSoftObjectPath(CategoryOnlyShield), FSoftObjectPath(GunB)};

	TestTrue(TEXT("v3 category-only shield record is accepted"), Deck->RestoreSaveGameForAutomationTests(Legacy.Get()));
	const FMSDeckLoadout Migrated = Deck->GetLoadoutInSlot(0);
	TestTrue(TEXT("category-only legacy shield becomes the independent equipped state"), Migrated.bShieldEquipped);
	TestEqual(TEXT("category-only shield leaves its original slot empty"), Migrated.EquippedWeapons[1].Get(),
		static_cast<UMSWeaponDataBase*>(nullptr));
	TestEqual(TEXT("weapon before category-only shield keeps its index"), Migrated.EquippedWeapons[0].Get(),
		static_cast<UMSWeaponDataBase*>(GunA));
	TestEqual(TEXT("weapon after category-only shield keeps its index"), Migrated.EquippedWeapons[2].Get(),
		static_cast<UMSWeaponDataBase*>(GunB));
	return true;
}

bool FMSWeaponSlotRejectsNativeShieldTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("MSWeaponSlotShieldWorld"));
	FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
	Context.SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->BeginPlay();
	ON_SCOPE_EXIT
	{
		GEngine->DestroyWorldContext(World);
		World->DestroyWorld(false);
	};
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AMSPlayerCharacter* Unit = World->SpawnActor<AMSPlayerCharacter>(
		AMSPlayerCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UMSWeaponComponent* Weapons = Unit ? Unit->GetWeaponComponent() : nullptr;
	UMSWeaponDataBase* Mortar = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_HeavyMortarEX5.DA_Weapon_HeavyMortarEX5"));
	UMSWeaponDataBase* Fortress = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_FortressShieldFS3.DA_Weapon_FortressShieldFS3"));
	UMSWeaponDataBase* Bazooka = LoadObject<UMSWeaponDataBase>(nullptr,
		TEXT("/Game/Data/Weapons/DA_Weapon_ShoulderBazookaSB9.DA_Weapon_ShoulderBazookaSB9"));
	if (!TestNotNull(TEXT("weapon component"), Weapons) || !TestNotNull(TEXT("EX-5"), Mortar)
		|| !TestNotNull(TEXT("FS-3"), Fortress) || !TestNotNull(TEXT("SB-9"), Bazooka)) return false;

	// An unnormalized deploy loadout (e.g. a legacy record) reaching the final boundary.
	Weapons->InitializeSlots({Mortar, Fortress, Bazooka});
	TestEqual(TEXT("three slot numbers are kept"), Weapons->GetSlotCount(), 3);
	TestTrue(TEXT("weapons keep their numbers and the shield slot is empty"),
		Weapons->GetWeaponData(0) == Mortar && Weapons->GetWeaponData(1) == nullptr && Weapons->GetWeaponData(2) == Bazooka);
	TestFalse(TEXT("the empty shield slot cannot be selected"), Weapons->SwitchToSlot(1));

	// A Shield-category definition that is not shield data is rejected the same way.
	UMSRangedWeaponData* CategoryOnly = NewObject<UMSRangedWeaponData>(Unit);
	CategoryOnly->CategoryTag = MSTags::Weapon_Category_Shield;
	Weapons->InitializeSlots({CategoryOnly, Mortar});
	TestTrue(TEXT("Shield-category entry stays empty and the next weapon is active"),
		Weapons->GetWeaponData(0) == nullptr && Weapons->GetWeaponData(1) == Mortar && Weapons->GetActiveSlotIndex() == 1);
	return true;
}

#endif
