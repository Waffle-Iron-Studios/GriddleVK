
// Macil (version 1) ---------------------------------------------------------

class Macil1 : Actor
{
	Default
	{
		Health 95;
		Radius 20;
		Height 56;
		Speed 8;
		Painchance 250;
		Monster;
		-COUNTKILL
		+NOTDMATCH
		+NOICEDEATH
		+NOSPLASHALERT
		+NODAMAGE
		+NEVERRESPAWN
		DamageFactor "Fire", 0.5;
		MinMissileChance 150;
		SeeSound "macil/sight";
		PainSound "macil/pain";
		ActiveSound "macil/active";
		CrushPainSound "misc/pcrush";
		Tag "$TAG_MACIL1";
		Obituary "$OB_MACIL";
		DropItem "BoxOfBullets";
		MaxStepHeight 16;
		MaxDropoffHeight 32;
	}
	States
	{
	Spawn:
		LEDR C 5 A_Look2;
		Loop;
		LEDR A 8;
		Loop;
		LEDR B 8;
		Loop;
		LEAD ABCD 6 A_Wander;
		Loop;
	See:
		LEAD AABBCCDD 3 A_Chase;
		Loop;
	Missile:
	Death:
		LEAD E 2 A_FaceTarget;
		LEAD F 2 BRIGHT A_ShootGun;
		LEAD E 1 A_SentinelRefire;
		Loop;
	Pain:
		LEAD Y 3;
		LEAD Y 3 A_Pain;
		Goto See;
	}
}
		

// Macil (version 2) ---------------------------------------------------------

class Macil2 : Macil1
{
	Default
	{
		Painchance 200;
		+COUNTKILL
		+SPECTRAL
		-NODAMAGE
		Tag "$TAG_MACIL2";
		DeathSound "macil/slop";
		DropItem "None";
		DamageFactor "SpectralLow", 0;
	}
	States
	{
	Missile:
		LEAD E 4 A_FaceTarget;
		LEAD F 4 BRIGHT A_ShootGun;
		LEAD E 2 A_SentinelRefire;
		Loop;
	Death:
		LEAD G 5;
		LEAD H 5 A_Scream;
		LEAD IJ 4;
		LEAD K 3;
		LEAD L 3 A_NoBlocking;
		LEAD MNOPQRSTUV 3;
		LEAD W 3 A_SpawnItemEx("AlienSpectre4", 0, 0, 0, 0, 0, random[spectrespawn](0,255)*0.0078125, 0, SXF_NOCHECKPOSITION);
		LEAD X -1;
		Stop;
	}
}


