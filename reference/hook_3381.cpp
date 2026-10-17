// ============================================================================
// EraCH3 — Fortnite 21.30 CL-21088273 Android  (FIXED & COMPLETED)
// ============================================================================
// Build ID of target libUnreal.so:
//   5b371871c152651ff2b038c01c9504c1a82de1f8
//
// This is the corrected version of hook.cpp. All engine-class offsets and
// function signatures below were verified against the 21.30 MobileDumper-7
// SDK (GObjects dump + ClassesInfo/StructsInfo/FunctionsInfo/EnumsInfo).
//
// FIXES/PORTS (see CHANGES.md for the full list):
//  [F01] FQuickBarSlot.bEnabled lives at +0x10 bit0 (was writing +0x18 =
//        UsedByItemGuid, corrupting the GUID and never enabling the slot).
//  [F02] Reflection paths "FortQuickBar.Slots"/"FortQuickBarSlot.Items" do
//        not exist — the reflected struct names are "QuickBar"/"QuickBarSlot".
//  [F03] "Engine.Actor.SetMirrored" does not exist — use
//        BuildingSMActor.SetMirrored (bIsMirrored).
//  [F04] FortPlayerController.ServerAttemptAircraftJump does not exist in
//        21.30 — it lives on UFortControllerComponent_Aircraft.
//  [F05] JumpFromHudStart is dispatched on UAthenaHUDContext, not on the
//        controller — the old PE filter never matched (touch jump dead).
//  [F06] AFortPickup has no ItemDefinition/Count members in 21.30 — the
//        entry lives in PrimaryPickupItemEntry (FFortItemEntry).
//  [F07] FCreateBuildingActorData: BuildingClass ptr at 0x40 (was 0x48),
//        bMirrored at 0x38, param size 0x50.
//  [F08] ServerEditBuildingActor params: BuildingActorToEdit(ptr),
//        NewBuildingClass(ptr), RotationIterations(uint8), bMirrored(bool)
//        — old struct read bMirrored from the wrong offset.
//  [F09] GetAnimationHardReference in 21.30 takes BodyType/Gender/
//        PawnContext (old "Type/Animation" params are gone) — emotes fixed.
//  [F10] Edit tool: EditActor + OnRep_EditActor live on FortWeap_EditingTool,
//        not FortWeapon.
//  [F11] ServerOnExitVehicle ExitForceBehavior is a uint8 enum (old code
//        bound int32 → bind failed).
//  [F12] Removed dead OnRep calls (FortQuickBars OnRep_Primary/Secondary,
//        PC.OnRep_QuickBar, PlayerState.OnRep_CharacterParts,
//        Pawn.OnRep_CustomizationLoadout) — none exist in 21.30.
//  [F13] Removed K2_GiveAbility/GiveAbility grant paths — no such UFUNCTIONs
//        in 21.30; abilities flow through EquipFortAbilitySet only.
//  [F14] GC / SavePackage guards in WorldTick (offsets existed, never used).
//  [F15] IsStandalone gating before any session work (was never called).
//  [F16] Call: Blueprint event override resolution + receiver validation
//        + named/positional param binding (ported from the 10.40 Era core).
//  [F17] Inventory: native NewGuid + reconciliation verification + duplicate
//        protection (ported from Era's give()).
//  [F18] Pickups: stacking, max-stack transfer, weapon slot selection
//        (ported from Era's local_pickup).
//  [F19] Two-bar quickbar layout: weapons on Primary(0), build pieces on
//        Secondary(1), binding through EquipItemToQuickBarSlot with
//        GetItemInQuickbarSlot read-back verification.
//  [F20] Local loot rolling for containers with rollback (replaces the
//        dead BP-only path; exported-table style from the Era core).
//  [F21] Building through the local preview (LastBuildPreviewGridSnapLoc/
//        Rot + CurrentBuildableClass) with dedup + touch-build polling.
//  [F22] Locker cosmetics via MCP (CosmeticLoadoutPC + GetLoadoutForPlayer)
//        with hero specializations + backpack fallback.
//  [F23] SetMaxHealth/SetHealth(100) after possession; bBuildFree for free
//        building; bReadyToStartMatch direct bit fallback (0xA28 bit5).
//  [F24] Lobby return interception (ServerReturnToMainMenu → open Frontend
//        after tick) + fly/walk/ghost/god/teleport + era.* console commands.
//  [F25] Prologue verification for every hooked RVA before DobbyHook.
// ============================================================================
#include <jni.h>
#include <pthread.h>
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <cstddef>
#include <atomic>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <set>
#include <algorithm>
#include <android/log.h>

static FILE* g_logFile = nullptr;
static void InitLog() {
    const char* p[] = {
        "/storage/emulated/0/Android/data/com.epicgames.fortnite/files/era_log.txt",
        "/sdcard/Android/data/com.epicgames.fortnite/files/era_log.txt",
        "/data/local/tmp/era_log.txt",
        "/sdcard/era_log.txt",
        nullptr};
    for (int i = 0; p[i]; ++i) { g_logFile = fopen(p[i], "w"); if (g_logFile) break; }
}
static void LogImpl(const char* fmt, ...) {
    char line[1024]; va_list ap; va_start(ap, fmt);
    vsnprintf(line, sizeof(line), fmt, ap); va_end(ap);
    __android_log_write(ANDROID_LOG_INFO, "EraCH3", line);
    if (g_logFile) { fprintf(g_logFile, "%s\n", line); fflush(g_logFile); }
}
#define LOG(...) LogImpl(__VA_ARGS__)

using int8=int8_t; using int16=int16_t; using int32=int32_t; using int64=int64_t;
using uint8=uint8_t; using uint16=uint16_t; using uint32=uint32_t; using uint64=uint64_t;
using TCHAR=char16_t;

// ============================================================================
// PROFILE — All offsets for Fortnite 21.30 (libUnreal.so CL-21088273).
// Code RVAs byte-verified against the shipped library; class offsets
// verified against the 21.30 MobileDumper-7 dump.
// ============================================================================
namespace profile {
// Core engine (prologue-verified: valid ARM64 prologues at every RVA)
constexpr uintptr_t ProcessEvent         = 0x07611B5C;
constexpr uintptr_t StaticFindObject     = 0x0762A664;
constexpr uintptr_t StaticLoadObject     = 0x0761C568;
constexpr uintptr_t StaticLoadClass      = 0x0762CFB8;
constexpr uintptr_t NameAppendString     = 0x074937CC;
constexpr uintptr_t Free                 = 0x07355370;
constexpr uintptr_t WorldTick            = 0x0949685C;
constexpr uintptr_t EngineExec           = 0x098BB160;
constexpr uintptr_t IsStandalone         = 0x0945F840;
constexpr uintptr_t GetPlayerController  = 0x093A1D48;
constexpr uintptr_t BeginSpawn           = 0x093A37E4;
constexpr uintptr_t FinishSpawn          = 0x0939F1B8;
constexpr uintptr_t SpawnObject          = 0x093A3388;
constexpr uintptr_t GenericArrayAdd      = 0x0944A668;
constexpr uintptr_t GenericArrayRemove   = 0x0944B2DC;
constexpr uintptr_t IsGarbageCollecting  = 0x0756E138;
constexpr uintptr_t IsSavingPackage      = 0x0760A59C;
constexpr uintptr_t GObjects             = 0x0DCD6E08;
constexpr uintptr_t Realloc              = 0x0735E520;
constexpr uintptr_t CopyScriptStruct     = 0x07535848;   // UScriptStruct::CopyScriptStruct
constexpr uintptr_t GetInterfaceAddress  = 0x0761AA88;

// Object layout (UE5.0, Dumper-7 preset — matches MobileDumper-7)
constexpr unsigned ObjectClass  = 0x10;
constexpr unsigned ObjectName   = 0x18;
constexpr unsigned ObjectOuter  = 0x20;
constexpr unsigned FieldNext    = 0x28;
constexpr unsigned StructSuper  = 0x40;
constexpr unsigned StructChildren   = 0x48;
constexpr unsigned StructChildProps = 0x50;
constexpr unsigned FunctionParamSize = 0xB6;
constexpr unsigned PropFlags       = 0x38;
constexpr unsigned PropOffset      = 0x44;
constexpr unsigned PropElementSize = 0x34;
constexpr unsigned BoolSize        = 0x80;
constexpr unsigned BoolByteOffset  = 0x81;
constexpr unsigned BoolFieldMask   = 0x82;
constexpr unsigned FFieldName = 0x28;
constexpr unsigned FFieldNext = 0x20;

// CPF flags
constexpr uint64_t CPF_Parm       = 0x00000080ULL;
constexpr uint64_t CPF_OutParm    = 0x00000100ULL;
constexpr uint64_t CPF_ConstParm  = 0x00000200ULL;
constexpr uint64_t CPF_ReturnParm = 0x00000400ULL;

// FFortItemEntry (21.30 struct layout — verified)
constexpr unsigned ItemEntry_Count          = 0x0C;
constexpr unsigned ItemEntry_ItemDefinition = 0x18;
constexpr unsigned ItemEntry_Durability     = 0x24;
constexpr unsigned ItemEntry_LoadedAmmo     = 0x2C;
constexpr unsigned ItemEntry_PhantomAmmo    = 0x30;
constexpr unsigned ItemEntry_ItemGuid       = 0x68;
constexpr unsigned ItemEntry_TrackerGuid    = 0x78;
constexpr unsigned ItemEntry_StateValues    = 0xD0;

// Fortnite classes (verified)
constexpr unsigned FortPlayerState_HeroType            = 0x0430;
constexpr unsigned FortPlayerState_CharacterData       = 0x0580;
constexpr unsigned FortPlayerState_AbilitySystemComp   = 0x09F0;
constexpr unsigned FortPlayerController_MyPlayerInfo   = 0x2088;
constexpr unsigned FortPlayerController_ClientQuickBars= 0x1A60;
constexpr unsigned FortPlayerController_QuickBarReady  = 0x1A68;   // byte after ClientQuickBars, checked by native GetItemInQuickbarSlot gate
constexpr unsigned FortPlayerController_ReadyByte      = 0x0A28;   // bReadyToStartMatch bitfield byte
constexpr unsigned FortPlayerController_ReadyBit       = 5;
constexpr unsigned RegisteredPlayerInfo_HeroInstance   = 0x2D0;    // TempAthenaMenuHeroInstance
constexpr unsigned FortHero_CharacterParts             = 0x238;
constexpr unsigned FortHero_Specializations            = 0x218;
constexpr unsigned FortPlayerPawn_bIsSkydiving         = 0x1A1D;
constexpr unsigned FortPlayerPawn_bIsParachuteOpen     = 0x1A1E;
constexpr unsigned FortPlayerPawn_CosmeticLoadout      = 0x2A08;
constexpr unsigned FortPawn_CurrentWeapon              = 0x07F0;
constexpr unsigned FortPawn_AbilitySystemComponent     = 0x1008;
constexpr unsigned BuildingWall_bDoorOpen_Byte         = 0x0FDE;   // plain bool byte (0/1)
constexpr unsigned PlayerPawn_Athena_Speed_Sprint      = 0x53EC;

// ⭐ Movement offsets (from SDK)
constexpr unsigned FortPawn_bWantsToSlide            = 0x1030;
constexpr unsigned FortPlayerPawn_bIsSliding_Byte    = 0x06DB;
constexpr unsigned FortPlayerPawn_bIsSliding_Bit     = 3;
constexpr unsigned FortPlayerPawn_bIsSurfaceSwim_Byte= 0x0770;
constexpr unsigned FortPlayerPawn_bIsSurfaceSwim_Bit = 4;
constexpr unsigned CharacterMovement_MovementMode    = 0x019C;
constexpr unsigned CharacterMovement_CustomMode      = 0x019D;

// TacticalSprint (UFortPlayerControllerComponent_TacticalSprint, verified)
constexpr unsigned TacticalSprint_Flags              = 0xC0;
constexpr uint8_t TacticalSprint_Bit_Enabled   = 0x01;
constexpr uint8_t TacticalSprint_Bit_WantsSprint = 0x02;
constexpr uint8_t TacticalSprint_Bit_IsSprinting = 0x08;

// ⭐ QuickBars (21.30 — libUnreal.so)
constexpr uintptr_t InitializeAthenaQuickBars    = 0x056607ec;
constexpr unsigned  QuickBars_VtableSlot         = 0x738;
constexpr unsigned  QuickBars_PrimaryOffset      = 0x278;   // FQuickBar.PrimaryQuickBar
constexpr unsigned  QuickBars_SecondaryOffset    = 0x308;   // FQuickBar.SecondaryQuickBar
constexpr unsigned  QuickBars_CreativeOffset     = 0x398;   // FQuickBar.CreativeQuickBar
constexpr unsigned  QuickBar_SlotsOffset         = 0x10;    // FQuickBar.Slots
constexpr unsigned  QuickBarSlot_ItemsOffset     = 0x00;    // FQuickBarSlot.Items
constexpr unsigned  QuickBarSlot_EnabledOffset   = 0x10;    // FQuickBarSlot.bEnabled (bit0)
constexpr unsigned  QuickBarSlot_EnabledBit      = 0x01;
constexpr unsigned  QuickBarSlot_OccupiedBit     = 0x08;    // bIsOccupied (bit3, same byte)
constexpr unsigned  QuickBarSlot_SlotSize        = 0x28;
constexpr unsigned  Controller_ClientQuickBars   = 0x1a60;
constexpr unsigned  Controller_QuickBarReadyByte = 0x1a68;
constexpr uintptr_t GetItemInQuickbarSlot_Gate   = 0x06673524;
constexpr unsigned  QuickBars_VtableSlot_GetItemInSlot = 0x6f0;

// ⭐ Local building / reticle / touch (AFortPlayerController, verified)
constexpr unsigned Controller_CurrentBuildableClass = 0x1898;
constexpr unsigned Controller_BuildPreviewRotIter   = 0x1858;
constexpr unsigned Controller_BuildPreviewMirrored  = 0x1859;
constexpr unsigned Controller_bBuildFree            = 0x185B;
constexpr unsigned Controller_TouchPrimaryFire      = 0x0BB8;
constexpr unsigned Controller_PreviewSnapLoc        = 0x19B0;   // FVector (LWC)
constexpr unsigned Controller_PreviewSnapRot        = 0x19C8;   // FRotator (LWC)
constexpr unsigned Controller_LocationUnderReticle  = 0x24C0;
constexpr unsigned Controller_InteractionComponent  = 0x28D8;
constexpr unsigned Controller_CosmeticLoadoutPC     = 0x1E70;   // FFortAthenaLoadout
constexpr unsigned AFortPickup_PrimaryEntry         = 0x02F8;
constexpr unsigned AFortPickup_SourceFlags          = 0x0540;
constexpr unsigned AFortPickup_SpawnSource          = 0x0541;
constexpr unsigned AFortGameState_GamePhase         = 0x2150;
constexpr unsigned AFortGameState_SafeZoneIndicator = 0x19A8;
constexpr unsigned AFortGameState_PlaylistInfo      = 0x2170;   // FPlaylistPropertyArray
constexpr unsigned EditingTool_EditActor            = 0x1020;
constexpr unsigned Pickaxe_WeaponDefinition         = 0x0790;
}

extern "C" int DobbyHook(void* target, void* replace, void** origin);

// ============================================================================
// MEMORY HELPERS
// ============================================================================
static inline bool IsBadPtr(const void* p) {
    if (!p) return true;
    uintptr_t v = (uintptr_t)p;
    if (v < 0x10000) return true;
    if (v > 0x0000FFFFFFFFFFFFULL) return true;
    return false;
}
template<class T> static T Read(const void* p, size_t off = 0) {
    T r{}; if (p) memcpy(&r, (const char*)p + off, sizeof(T)); return r;
}
template<class T> static void Write(void* p, size_t off, const T& v) {
    if (p) memcpy((char*)p + off, &v, sizeof(T));
}

static uintptr_t g_base = 0;
static std::atomic<int> g_status{0};
static std::atomic<int> g_gameTid{0};
static thread_local bool g_internal = false;

struct Guard { bool s = g_internal; Guard() { g_internal = true; } ~Guard() { g_internal = s; } };
template<class F> static F Fn(uintptr_t rva) { return reinterpret_cast<F>(g_base + rva); }

// ============================================================================
// UE5 STRUCTS (LWC — 21.30 uses double-precision FVector/FRotator)
// ============================================================================
struct FStringNative { TCHAR* Data; int32 Count; int32 Max; };
struct FName         { uint32 CompIdx; uint32 DispIdx; };
struct Array         { void* Data; int32 Count; int32 Max; };
struct Guid          { uint32 A, B, C, D; };
struct Vector        { double X, Y, Z; };
struct Rotator       { double Pitch, Yaw, Roll; };

struct alignas(16) Transform {
    double Rotation[4]{0, 0, 0, 1};
    double Location[3]{0, 0, 0};
    uint8_t _padL[8]{};
    double Scale[3]{1, 1, 1};
    uint8_t _padS[8]{};
};
static_assert(sizeof(Vector) == 24, "Vector LWC");
static_assert(sizeof(Transform) == 96, "Transform LWC");

struct FUObjectItem { void* Object; int32 Flags; int32 Cluster; int32 Serial; };
struct GObjects_t {
    uint8 _pad0[0x10];
    void** Objects;
    uint8 _pad1[0x8];
    int32 MaxElements;
    int32 NumElements;
    int32 MaxChunks;
    int32 NumChunks;
    void* GetById(uint32 id) {
        if (!Objects) return nullptr;
        uint32 ci = id / 0x10000, wi = id % 0x10000;
        if ((int32)ci >= NumChunks) return nullptr;
        auto* chunk = (FUObjectItem*)Objects[ci];
        if (!chunk) return nullptr;
        return chunk[wi].Object;
    }
};
struct TScriptInterface { void* Object; void* Interface; };

// ============================================================================
// FORWARD DECLS
// ============================================================================
static void* FindObj(const std::string& path);
static std::string ObjName(void* obj);
static bool IsA(void* obj, void* type);
static bool SubclassOfFast(void* klass, void* targetClass);
static void* GetProp(const char* path);
static void* GetField(void* obj, void* prop);
static void SetBool(void* obj, const char* path, bool enabled);
static std::u16string ToWide(const std::string& s);
static std::string Narrow(const char16_t* s, int max);
static void* GetObjField(void* obj, const char* path);
static void* LoadAsset(const std::string& path, const char* type_path);
static void* LoadClass(const char* path, const char* base_path);
static void* GetPC(void* world, int index);
static void* incoming(void* function, void* data, const char* key, int expected_size = 0);
static GObjects_t* GObjectArray();

// ============================================================================
// TYPE CHECKS
// ============================================================================
static bool SubclassOfFast(void* klass, void* targetClass) {
    if (!klass || !targetClass || IsBadPtr(klass) || IsBadPtr(targetClass)) return false;
    for (int d = 0; d < 64; ++d) {
        if (klass == targetClass) return true;
        void* sup = Read<void*>(klass, profile::StructSuper);
        if (!sup || sup == klass || IsBadPtr(sup)) break;
        klass = sup;
    }
    return false;
}
static bool IsA(void* obj, void* type) {
    if (!obj || !type || IsBadPtr(obj) || IsBadPtr(type)) return false;
    void* klass = Read<void*>(obj, profile::ObjectClass);
    if (!klass || IsBadPtr(klass)) return false;
    return SubclassOfFast(klass, type);
}
static GObjects_t* GObjectArray() {
    if (!g_base) return nullptr;
    GObjects_t* go = Read<GObjects_t*>((void*)(g_base + profile::GObjects));
    if (!go || IsBadPtr(go)) return nullptr;
    if (go->NumElements < 0 || go->NumElements > 4000000) return nullptr;
    if (go->NumChunks < 0 || go->NumChunks > 128) return nullptr;
    return go;
}

// ============================================================================
// STRING HELPERS
// ============================================================================
static std::u16string ToWide(const std::string& s) { return std::u16string(s.begin(), s.end()); }
static std::string Narrow(const char16_t* s, int max) {
    std::string out;
    if (s) for (int i = 0; i < max && s[i]; ++i) out.push_back(s[i] < 128 ? (char)s[i] : '?');
    return out;
}
static void* FindObj(const std::string& path) {
    auto w = ToWide(path);
    return Fn<void*(*)(void*, void*, const char16_t*, bool)>(profile::StaticFindObject)(nullptr, nullptr, w.c_str(), false);
}
static bool SafeFieldName(void* field, uint32 nameOff, std::string& out) {
    if (!field || IsBadPtr(field)) return false;
    uint32 compIdx = Read<uint32>(field, nameOff + 0);
    uint32 dispIdx = Read<uint32>(field, nameOff + 4);
    if (compIdx == 0xFFFFFFFF || compIdx > 0x8000000) return false;
    if (dispIdx == 0xFFFFFFFF || dispIdx > 0x8000000) return false;
    FStringNative ns{};
    Fn<void(*)(const void*, FStringNative*)>(profile::NameAppendString)((char*)field + nameOff, &ns);
    out = Narrow(ns.Data, 128);
    if (ns.Data) Fn<void(*)(void*)>(profile::Free)(ns.Data);
    return true;
}
static std::string ObjName(void* obj) {
    if (!obj || IsBadPtr(obj)) return {};
    uint32 compIdx = Read<uint32>(obj, profile::ObjectName + 0);
    uint32 dispIdx = Read<uint32>(obj, profile::ObjectName + 4);
    if (compIdx == 0xFFFFFFFF || compIdx > 0x8000000) return {};
    if (dispIdx == 0xFFFFFFFF || dispIdx > 0x8000000) return {};
    static pthread_mutex_t m = PTHREAD_MUTEX_INITIALIZER;
    static std::map<void*, std::string> cache;
    pthread_mutex_lock(&m);
    auto it = cache.find(obj);
    if (it != cache.end()) { std::string r = it->second; pthread_mutex_unlock(&m); return r; }
    pthread_mutex_unlock(&m);
    FStringNative s{};
    Fn<void(*)(const void*, FStringNative*)>(profile::NameAppendString)((char*)obj + profile::ObjectName, &s);
    std::string out = Narrow(s.Data, std::min(std::max(s.Count, 0), 512));
    if (s.Data) Fn<void(*)(void*)>(profile::Free)(s.Data);
    pthread_mutex_lock(&m);
    if (cache.size() < 200000) cache[obj] = out;
    pthread_mutex_unlock(&m);
    return out;
}
// [F17/F20] Find an item definition by its short name through GObjects
// (ported from the Era core by_short_name()).
static void* ByShortName(const std::string& wanted) {
    GObjects_t* go = GObjectArray();
    if (!go) return nullptr;
    void* definition_type = FindObj("/Script/FortniteGame.FortItemDefinition");
    if (!definition_type) return nullptr;
    for (int32 i = 0; i < go->NumElements; i += 1) {
        void* object = go->GetById((uint32)i);
        if (object && !IsBadPtr(object) && IsA(object, definition_type) && ObjName(object) == wanted)
            return object;
    }
    return nullptr;
}

// ============================================================================
// PROPERTY ACCESS
// ============================================================================
static void* GetProp(const char* path) {
    if (!path) return nullptr;
    std::string s(path);
    if (void* direct = FindObj(s)) return direct;
    auto pos = s.rfind('.');
    if (pos == std::string::npos) return nullptr;
    std::string classPath = s.substr(0, pos);
    std::string propName  = s.substr(pos + 1);
    if (void* alt = FindObj(classPath + ":" + propName)) return alt;
    void* klass = FindObj(classPath);
    if (!klass || IsBadPtr(klass)) return nullptr;
    for (int depth = 0; depth < 32 && klass && !IsBadPtr(klass); ++depth) {
        void* f = Read<void*>(klass, profile::StructChildProps);
        for (int i = 0; f && !IsBadPtr(f) && i < 1024; ++i) {
            std::string n;
            if (SafeFieldName(f, profile::FFieldName, n) && n == propName) return f;
            f = Read<void*>(f, profile::FFieldNext);
        }
        void* c = Read<void*>(klass, profile::StructChildren);
        for (int i = 0; c && !IsBadPtr(c) && i < 1024; ++i) {
            std::string n;
            if (SafeFieldName(c, profile::ObjectName, n) && n == propName) return c;
            c = Read<void*>(c, profile::FieldNext);
        }
        klass = Read<void*>(klass, profile::StructSuper);
    }
    return nullptr;
}
static void* GetField(void* obj, void* prop) {
    if (!obj || !prop) return nullptr;
    return (char*)obj + Read<int>(prop, profile::PropOffset);
}
static void SetBool(void* obj, const char* path, bool enabled) {
    void* p = GetProp(path);
    if (!p || !obj) return;
    uint8_t size = Read<uint8_t>(p, profile::BoolSize);
    uint8_t byte = Read<uint8_t>(p, profile::BoolByteOffset);
    uint8_t mask = Read<uint8_t>(p, profile::BoolFieldMask);
    if (!size || size > 8 || byte >= size || !mask) return;
    auto dst = (uint8_t*)GetField(obj, p) + byte;
    *dst = enabled ? (*dst | mask) : (*dst & ~mask);
}
static bool GetBool(void* obj, const char* path) {
    void* p = GetProp(path);
    if (!p || !obj) return false;
    uint8_t size = Read<uint8_t>(p, profile::BoolSize);
    uint8_t byte = Read<uint8_t>(p, profile::BoolByteOffset);
    uint8_t mask = Read<uint8_t>(p, profile::BoolFieldMask);
    if (!size || size > 8 || byte >= size || !mask) return false;
    return (Read<uint8_t>(GetField(obj, p), byte) & mask) != 0;
}
static void* GetObjField(void* obj, const char* path) {
    void* p = GetProp(path);
    if (!p || !obj) return nullptr;
    if (Read<int>(p, profile::PropElementSize) != 8) return nullptr;
    return Read<void*>(GetField(obj, p));
}
template<class T> static bool GetFieldValue(void* obj, const char* path, T& out) {
    void* p = GetProp(path);
    if (!p || !obj || Read<int>(p, profile::PropElementSize) != sizeof(T)) return false;
    out = Read<T>(GetField(obj, p));
    return true;
}

// ============================================================================
// HOOK TYPEDEFS
// ============================================================================
using PE_t   = void(*)(void*, void*, void*);
using Tick_t = void(*)(void*, int, float);
using Exec_t = bool(*)(void*, void*, const char16_t*, void*);

static PE_t   orig_PE = nullptr;
static Tick_t orig_Tick = nullptr;
static Exec_t orig_Exec = nullptr;

// ============================================================================
// CALL SYSTEM  — [F16] upgraded:
//   • Blueprint event override resolution (event_receiver) so BP events such
//     as BuildingActor.BlueprintOnInteract run the receiver's override
//     instead of the empty base event (ported from the Era core).
//   • Receiver validation in run(): refuses to execute a function on an
//     object that is not a subclass of the function's outer class.
//   • set_any(): bind by trying several reflection names (dumper renames
//     duplicate params like "Class" → "Class_0").
//   • set_at(): positional binding for nameless native params
//     (e.g. AFortPawn.SetMaxHealth(float)).
// ============================================================================
class Call {
    void* m_func = nullptr;
    struct Param { std::string name; int offset, size; uint64_t flags; };
    std::vector<Param> m_params;
    alignas(16) std::array<uint8_t, 8192> m_data{};
    bool m_valid = false;

    void add_param(void* p, uint64_t flags, int total, bool ffield) {
        if (!(flags & profile::CPF_Parm)) return;
        int off = Read<int>(p, profile::PropOffset);
        int sz  = Read<int>(p, profile::PropElementSize);
        if (off < 0 || sz < 1 || off + sz > total) return;
        std::string pname;
        int nameOff = ffield ? profile::FFieldName : profile::ObjectName;
        if (!SafeFieldName(p, nameOff, pname)) return;
        m_params.push_back({pname, off, sz, flags});
    }
public:
    explicit Call(const char* path, void* event_receiver = nullptr) {
        m_func = FindObj(path);
        if (!m_func) return;
        // [F16] Blueprint override resolution: walk the receiver's super
        // chain from most-derived to base and prefer the matching Function
        // found there over the (possibly empty) base event we looked up.
        if (event_receiver && !IsBadPtr(event_receiver)) {
            std::string ev = ObjName(m_func);
            void* funcType = FindObj("/Script/CoreUObject.Function");
            void* klass = Read<void*>(event_receiver, profile::ObjectClass);
            void* derived = nullptr;
            for (int d = 0; d < 64 && klass && !IsBadPtr(klass) && funcType; ++d) {
                void* f = Read<void*>(klass, profile::StructChildProps);
                for (int i = 0; f && !IsBadPtr(f) && i < 1024; ++i) {
                    std::string n;
                    if (SafeFieldName(f, profile::FFieldName, n) && n == ev && IsA(f, funcType)) { derived = f; break; }
                    f = Read<void*>(f, profile::FFieldNext);
                }
                if (!derived) {
                    void* c = Read<void*>(klass, profile::StructChildren);
                    for (int i = 0; c && !IsBadPtr(c) && i < 1024; ++i) {
                        std::string n;
                        if (SafeFieldName(c, profile::ObjectName, n) && n == ev && IsA(c, funcType)) { derived = c; break; }
                        c = Read<void*>(c, profile::FieldNext);
                    }
                }
                if (derived) break;
                klass = Read<void*>(klass, profile::StructSuper);
            }
            if (derived) m_func = derived;
        }
        int total = Read<uint16_t>(m_func, profile::FunctionParamSize);
        if (total < 0 || total > (int)m_data.size()) return;
        void* p = Read<void*>(m_func, profile::StructChildren);
        for (int i = 0; p && !IsBadPtr(p) && i < 128; ++i) {
            add_param(p, Read<uint64_t>(p, profile::PropFlags), total, false);
            p = Read<void*>(p, profile::FieldNext);
        }
        void* fp = Read<void*>(m_func, profile::StructChildProps);
        for (int i = 0; fp && !IsBadPtr(fp) && i < 128; ++i) {
            add_param(fp, Read<uint64_t>(fp, profile::PropFlags), total, true);
            fp = Read<void*>(fp, profile::FFieldNext);
        }
        m_valid = (total == 0) || !m_params.empty();
    }
    bool ok() const { return m_valid; }
    void* func() const { return m_func; }
    void dump(const char* label) const {
        LOG("[Call %s] valid=%d func=%p params=%zu", label, (int)m_valid, m_func, m_params.size());
        for (auto& p : m_params)
            LOG("   param '%s' off=%d size=%d flags=%llx", p.name.c_str(), p.offset, p.size,
                (unsigned long long)p.flags);
    }
    template<class T> bool set(const char* key, const T& value) {
        for (auto& p : m_params)
            if (p.name == key && p.size == (int)sizeof(T)) {
                memcpy(m_data.data() + p.offset, &value, sizeof(T));
                return true;
            }
        return false;
    }
    // [F16] Try several reflection names (GetContext's "Class" is emitted as
    // "Class_0" by the dumper, and other renames exist across the SDK).
    template<class T> bool set_any(const T& value, const char* k1, const char* k2 = nullptr,
                                   const char* k3 = nullptr, const char* k4 = nullptr) {
        const char* keys[] = {k1, k2, k3, k4};
        for (int i = 0; i < 4; ++i)
            if (keys[i] && set(keys[i], value)) return true;
        return false;
    }
    // [F16] Positional binding for native functions with nameless params.
    template<class T> bool set_at(int index, const T& value) {
        int seen = 0;
        for (auto& p : m_params)
            if (!(p.flags & profile::CPF_ReturnParm) && p.size == (int)sizeof(T))
                if (seen++ == index) { memcpy(m_data.data() + p.offset, &value, sizeof(T)); return true; }
        return false;
    }
    bool set_bytes(const char* key, const void* src, int size) {
        for (auto& p : m_params)
            if (p.name == key && p.size == size && src) {
                memcpy(m_data.data() + p.offset, src, size);
                return true;
            }
        return false;
    }
    template<class T> T result() const {
        for (auto& p : m_params)
            if ((p.flags & profile::CPF_ReturnParm) && p.size == (int)sizeof(T))
                return Read<T>(m_data.data(), p.offset);
        return {};
    }
    void* result_data(int size) {
        for (auto& p : m_params)
            if ((p.flags & profile::CPF_ReturnParm) && p.size == size)
                return m_data.data() + p.offset;
        return nullptr;
    }
    template<class T> T get(const char* key) const {
        for (auto& p : m_params)
            if (p.name == key && p.size == (int)sizeof(T))
                return Read<T>(m_data.data(), p.offset);
        return {};
    }
    bool run(void* object) {
        if (!m_valid || !object || !orig_PE) return false;
        // [F16] Receiver validation — refuse cross-class execution.
        void* owner = Read<void*>(m_func, profile::ObjectOuter);
        if (owner && !IsBadPtr(owner) && !IsA(object, owner)) {
            LOG("[Call] rejected %s: receiver=%s owner=%s", ObjName(m_func).c_str(),
                ObjName(Read<void*>(object, profile::ObjectClass)).c_str(), ObjName(owner).c_str());
            return false;
        }
        Guard g;
        orig_PE(object, m_func, m_data.data());
        return true;
    }
};

// [F16] Extract a named incoming PE parameter by reflection metadata
// (ported from the Era core incoming()).
static void* incoming(void* function, void* data, const char* key, int expected_size) {
    if (!function || !data || IsBadPtr(function)) return nullptr;
    int total = Read<uint16_t>(function, profile::FunctionParamSize);
    void* p = Read<void*>(function, profile::StructChildren);
    for (int i = 0; p && !IsBadPtr(p) && i < 128; ++i, p = Read<void*>(p, profile::FieldNext)) {
        if (!(Read<uint64_t>(p, profile::PropFlags) & profile::CPF_Parm)) continue;
        std::string n;
        if (!SafeFieldName(p, profile::ObjectName, n) || n != key) continue;
        int offset = Read<int>(p, profile::PropOffset);
        int size   = Read<int>(p, profile::PropElementSize);
        if (offset < 0 || size < 1 || offset + size > total || (expected_size && size != expected_size))
            return nullptr;
        return (char*)data + offset;
    }
    p = Read<void*>(function, profile::StructChildProps);
    for (int i = 0; p && !IsBadPtr(p) && i < 128; ++i, p = Read<void*>(p, profile::FFieldNext)) {
        if (!(Read<uint64_t>(p, profile::PropFlags) & profile::CPF_Parm)) continue;
        std::string n;
        if (!SafeFieldName(p, profile::FFieldName, n) || n != key) continue;
        int offset = Read<int>(p, profile::PropOffset);
        int size   = Read<int>(p, profile::PropElementSize);
        if (offset < 0 || size < 1 || offset + size > total || (expected_size && size != expected_size))
            return nullptr;
        return (char*)data + offset;
    }
    return nullptr;
}

// ============================================================================
// ASSET LOADING / SPAWN
// ============================================================================
static void* LoadAsset(const std::string& path,
                       const char* type_path = "/Script/FortniteGame.FortItemDefinition") {
    if (void* r = FindObj(path)) return r;
    auto s = ToWide(path);
    void* type = FindObj(type_path);
    if (!type) return nullptr;
    return Fn<void*(*)(void*, void*, const char16_t*, const char16_t*, uint32_t, void*, bool, void*)>(
        profile::StaticLoadObject)(type, nullptr, s.c_str(), nullptr, 0, nullptr, true, nullptr);
}
static void* LoadClass(const char* path, const char* base_path = "/Script/Engine.Actor") {
    if (void* r = FindObj(path)) return r;
    auto s = ToWide(path);
    void* parent = FindObj(base_path);
    if (!parent) return nullptr;
    return Fn<void*(*)(void*, void*, const char16_t*, const char16_t*, uint32_t, void*)>(
        profile::StaticLoadClass)(parent, nullptr, s.c_str(), nullptr, 0, nullptr);
}
static void* GetPC(void* world, int index = 0) {
    if (!world) return nullptr;
    return Fn<void*(*)(void*, int)>(profile::GetPlayerController)(world, index);
}
static void* GetPawn(void* controller) {
    if (!controller) return nullptr;
    static void* pawnProp = nullptr;
    static bool resolved = false;
    if (!resolved) { pawnProp = GetProp("/Script/Engine.Controller.Pawn"); resolved = true; }
    if (!pawnProp) return nullptr;
    return Read<void*>(GetField(controller, pawnProp));
}
static void Possess(void* controller, void* pawn) {
    if (!controller || !pawn) return;
    Call c("/Script/Engine.Controller.Possess");
    if (!c.ok()) { LOG("Possess: binding failed"); return; }
    if (!c.set("InPawn", pawn)) { c.dump("Possess"); return; }
    c.run(controller);
}
static void* SpawnActor(void* world, void* klass, void* owner, const Transform& t) {
    if (!world || !klass) return nullptr;
    void* actor = Fn<void*(*)(void*, void*, const Transform*, uint8_t, void*)>(
        profile::BeginSpawn)(world, klass, &t, 1, owner);
    return actor ? Fn<void*(*)(void*, const Transform*)>(profile::FinishSpawn)(actor, &t) : nullptr;
}
static void* ResolveInterface(void* object, void* ifaceClass) {
    if (!object || !ifaceClass) return nullptr;
    using Fn_t = void*(*)(void*, void*);
    return ((Fn_t)(g_base + profile::GetInterfaceAddress))(object, ifaceClass);
}
// GUID generation through the native Kismet library (verified in 21.30).
static Guid MakeGuid() {
    Guid g{};
    void* klib = FindObj("/Script/Engine.Default__KismetGuidLibrary");
    if (klib) { Call c("/Script/Engine.KismetGuidLibrary.NewGuid"); if (c.ok() && c.run(klib)) g = c.result<Guid>(); }
    return g;
}

// ============================================================================
// SESSION
// ============================================================================
struct Session {
    void* world = nullptr;
    int   world_index = -1;
    void* controller = nullptr;
    void* mode = nullptr;
    void* state = nullptr;
    void* pawn = nullptr;
    void* inventory = nullptr;
    void* quickbars = nullptr;
    void* tactical_sprint = nullptr;
    bool  initialized = false;
    bool  inventory_ready = false;
    bool  aircraft_jumped = false;
    bool  attempted = false;
    bool  finalized = false;
    bool  cheats_ready = false;
    bool  safezone_spawned = false;
    bool  sprint_applied = false;
    bool  ready_called = false;
    bool  visibility_forced = false;
    bool  abilities_granted = false;
    bool  melee_granted = false;
    bool  stamina_initialized = false;
    bool  tactical_sprint_setup = false;
    bool  quickbars_initialized = false;
    bool  movement_enabled = false;
    bool  lobby_requested = false;      // [F24]
    int   last_phase = -1;              // [F24] GamePhase tracking
    // [F21] local build dedup + touch polling
    void*  held_build_class = nullptr;
    void*  last_build_class = nullptr;
    double last_build_time = 0;
    double held_build_time = 0;
    std::array<double,6> held_build_pose{};
    std::array<double,6> last_build_pose{};
    void*  locker_pickaxe = nullptr;      // [F22] resolved by SyncLockerLoadout
    double observed_at = 0;
    double retry_at = 0;
    double finalize_at = 0;
    double next_build_poll = 0;
    std::array<void*, 6> slots{};           // Primary quickbar (weapons)
    std::array<void*, 6> secondary_slots{}; // Secondary quickbar (build pieces) [F19]
    std::vector<void*> character_parts;
    std::vector<void*> granted_ability_sets;
    void* edit_tool = nullptr;
    void* wall = nullptr;
    void* floor = nullptr;
    void* stairs = nullptr;
    void* roof = nullptr;
} session;

static double Monotonic() {
    timespec ts{}; clock_gettime(CLOCK_MONOTONIC, &ts);
    return double(ts.tv_sec) + double(ts.tv_nsec) * 1e-9;
}
// [F15] Standalone gating — never touch an online session.
static bool WorldIsStandalone(void* world) {
    if (!g_base || !world) return false;
    using IsStandalone_t = bool(*)(void*);
    return Fn<IsStandalone_t>(profile::IsStandalone)(world);
}
static bool ActorLocation(void* actor, Vector& out) {
    Call c("/Script/Engine.Actor.K2_GetActorLocation");
    if (!c.ok() || !c.run(actor)) return false;
    out = c.result<Vector>();
    return std::isfinite(out.X) && std::isfinite(out.Y) && std::isfinite(out.Z);
}
static bool NearPlayer(const Vector& point, float limit) {
    Vector origin{};
    if (!session.pawn || !ActorLocation(session.pawn, origin)) return false;
    double dx = point.X - origin.X, dy = point.Y - origin.Y, dz = point.Z - origin.Z;
    return dx*dx + dy*dy + dz*dz <= double(limit) * double(limit);
}

// ============================================================================
// ATHENA NAMESPACE
// ============================================================================
namespace Athena {
    void SetupWorld(void* world);
    void Tick();
    bool TryInitializePlayer(void* world, void* mode, void* controller);
    void SetupCheats();
    void FinalizePlayer();
    void ShowSkin();
    void SyncLockerLoadout();          // [F22] MCP locker sync
    void LoadProfileCosmetics();
    void LoadDefaultCosmetics();
    void ForcePlayerVisible();
    void BeginPlayerSkydiving();
    void EquipSlot(int slot);
    void DestroyLods();
    void DropLoadingScreen();
    void FixBuildingFoundations();
    void FixBus();
    void InitializeInventory();
    bool GiveItem(void* def, int count, int slot, uint8_t bar, bool require_new); // [F17]
    void AddToInventory(void* def, int count, int slot);
    void InventoryUpdate();
    void GrantDefaultAbilities();
    void GrantAbility(void* klass);
    void GrantAllAbilitySets();
    void InitStaminaAttributes();
    void RefreshHUD();
    void ApplySprintSpeed();
    void SetupTacticalSprint();
    void SetTacticalSprinting(bool sprinting);
    void SpawnSafeZoneIndicator();
    void SpawnPickup(void* def, int count, Vector loc, uint8_t sourceFlags, uint8_t spawnSource, bool toss);
    void DropInventoryItem(Guid guid, int count);
    void OnServerExecuteInventoryItem(Guid guid);
    void OnServerExecuteInventoryWeapon(void* weapon);
    void HandleJumpFromHudStart();
    void SpawnBattleBus();
    void OnAircraftJump();
    void TeleportToSpawnIsland();
    void RemoveNetDebugUI();
    void SpawnBuildPreviews();
    bool LocalPickup(void* pickup);    // [F18]
    void Loot(void* actor);
    void HandleDoor(void* door);
    void OnServerCreateBuildingActor(void* params);
    bool LocalBuild();                 // [F21]
    void PollTouchBuild();             // [F21]
    void OnBeginEditActor(void* piece);
    void OnFinishEditActor(void* actor, void* newClass, int rotIter, bool mirrored);
    void PlayEmoteItem(void* item);
    void OnExitVehicle();
    void ReturnToLobby(void* world);   // [F24]
    void TeleportToReticle();          // [F24]
    void ApplyCheats(const std::string& cmd); // [F24] fly/walk/ghost/god
    void CheatScript(const char* script);

    // QuickBars [F19]
    void InitializeQuickBarsNative();
    void SetQuickBarReady();
    bool BindSlot(void* item, int slot, uint8_t bar);
    void ActivateQuickBarSlot(uint8_t bar, int slot);
    void ForceUpdateQuickBars();
    void BuildQuickBarsManually();

    // Loot [F20]
    void SpawnFloorLoot();
    bool SearchContainer(void* actor, const Vector& location);

    // [F18] pickups / [movement]
    void ServerHandlePickup(void* pickup);
    void EnableMovementFeatures();
}

// ============================================================================
// ABILITY SYSTEM
// ============================================================================
static void* GetASC(void* actor) {
    if (!actor) return nullptr;
    void* asc = GetObjField(actor, "/Script/FortniteGame.FortPawn.AbilitySystemComponent");
    if (asc) return asc;
    asc = GetObjField(actor, "/Script/FortniteGame.FortPlayerState.AbilitySystemComponent");
    if (asc) return asc;
    asc = GetObjField(actor, "/Script/GameplayAbilities.AbilitySystemComponent");
    return asc;
}

void Athena::GrantAbility(void* klass) {
    if (!klass || !session.pawn) return;
    void* asc = GetASC(session.pawn);
    if (!asc) { LOG("[Ability] no ASC"); return; }
    // [F13] TryActivateAbilityByClass is the only reflected activation entry
    // point in 21.30 (GiveAbility/K2_GiveAbility do not exist).
    Call c("/Script/GameplayAbilities.AbilitySystemComponent.TryActivateAbilityByClass");
    if (!c.ok()) return;
    bool bAllow = true;
    if (c.set("InAbilityToActivate", klass) && c.set("bAllowRemoteActivation", bAllow)) {
        c.run(asc);
        LOG("[Ability] Activated: %s", ObjName(klass).c_str());
    }
}

// FFortAbilitySetHandle (0x38): TWeakObjectPtr + GrantedAbilityHandles array
// + AppliedEffectHandles array + ItemGuidsForAdditionalItems array. The
// Android wrapper leaves the two handle arrays allocated for us — free them
// so the GAS bookkeeping does not leak (Era core behaviour).
struct FortAbilitySetHandle {
    void*   TargetAbilitySystemComponent;
    Array   GrantedAbilityHandles;
    Array   AppliedEffectHandles;
    Array   ItemGuidsForAdditionalItems;
};
static_assert(sizeof(FortAbilitySetHandle) == 0x38, "FFortAbilitySetHandle 21.30");

static bool ApplyAbilitySet(void* pawn, void* abilitySet) {
    if (!pawn || !abilitySet || IsBadPtr(abilitySet)) return false;
    void* klib = FindObj("/Script/FortniteGame.Default__FortKismetLibrary");
    if (!klib) { LOG("[Abilities] KismetLibrary missing"); return false; }
    Call c("/Script/FortniteGame.FortKismetLibrary.EquipFortAbilitySet");
    if (!c.ok()) { LOG("[Abilities] EquipFortAbilitySet binding FAILED"); c.dump("EquipFortAbilitySet"); return false; }
    void* ifaceType = FindObj("/Script/GameplayAbilities.AbilitySystemInterface");
    if (!ifaceType) { LOG("[Abilities] AbilitySystemInterface class missing"); return false; }
    void* ifaceAddr = ResolveInterface(pawn, ifaceType);
    if (!ifaceAddr) {
        void* state = GetObjField(pawn, "/Script/Engine.Controller.PlayerState");
        if (!state) {
            void* pc = GetObjField(pawn, "/Script/Engine.Pawn.Controller");
            if (pc) state = GetObjField(pc, "/Script/Engine.Controller.PlayerState");
        }
        if (state) {
            ifaceAddr = ResolveInterface(state, ifaceType);
            LOG("[Abilities] Resolved on PlayerState=%p -> %p", state, ifaceAddr);
        }
    }
    LOG("[Abilities] ResolveInterface(pawn=%p) -> iface=%p", pawn, ifaceAddr);
    TScriptInterface iface{pawn, ifaceAddr};
    bool s1 = c.set("AbilitySystemInterfaceActor", iface);
    bool s2 = c.set("AbilitySet", abilitySet);
    bool s3 = c.set("OverrideSourceObject", pawn);
    if (!s1 || !s2 || !s3) { LOG("[Abilities] param bind failed"); c.dump("EquipFortAbilitySet"); return false; }
    if (!c.run(klib)) return false;
    // Free returned handle arrays (Era core: the wrapper destroys them anyway,
    // freeing keeps the GAS bookkeeping consistent).
    FortAbilitySetHandle handle = c.result<FortAbilitySetHandle>();
    if (handle.GrantedAbilityHandles.Data) Fn<void(*)(void*)>(profile::Free)(handle.GrantedAbilityHandles.Data);
    if (handle.AppliedEffectHandles.Data) Fn<void(*)(void*)>(profile::Free)(handle.AppliedEffectHandles.Data);
    LOG("[Abilities] ✅ Applied set: %s (abilities=%d effects=%d)", ObjName(abilitySet).c_str(),
        handle.GrantedAbilityHandles.Count, handle.AppliedEffectHandles.Count);
    return true;
}

void Athena::InitStaminaAttributes() {
    if (!session.pawn) return;
    void* asc = GetASC(session.pawn);
    if (!asc) { LOG("[Stamina] no ASC"); return; }
    void* attrClass = FindObj("/Script/FortniteGame.FortPlayerAttributeSet");
    if (!attrClass) attrClass = FindObj("/Script/GameplayAbilities.AttributeSet");
    if (!attrClass) { LOG("[Stamina] AttributeSet missing"); return; }
    void* dataTable = FindObj("/Game/Athena/Items/Quests/AthenaObjectiveStatXPTable.AthenaObjectiveStatXPTable");
    Call c("/Script/GameplayAbilities.AbilitySystemComponent.K2_InitStats");
    if (!c.ok()) { LOG("[Stamina] K2_InitStats binding FAILED"); return; }
    if (!c.set("Attributes", attrClass)) { LOG("[Stamina] Attributes bind failed"); return; }
    if (dataTable) c.set("DataTable", dataTable);
    if (!c.run(asc)) return;
    session.stamina_initialized = true;
    LOG("[Stamina] Initialized FortPlayerAttributeSet");
}

void Athena::GrantAllAbilitySets() {
    if (!session.pawn || session.abilities_granted) return;
    LOG("[Abilities] Setting up...");
    void* asc = GetASC(session.pawn);
    if (!asc) { LOG("[Abilities] no ASC"); return; }
    if (!session.stamina_initialized) InitStaminaAttributes();

    void* coreSet = LoadAsset(
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GAS_AthenaPlayer.GAS_AthenaPlayer",
        "/Script/FortniteGame.FortAbilitySet");
    if (!coreSet) { LOG("[Abilities] GAS_AthenaPlayer missing"); session.abilities_granted = true; return; }

    // Add the mobile interaction/harvest/sprint abilities into the set before
    // EquipFortAbilitySet so the native delivery path initializes their
    // replicated specs (Era core behaviour; offsets verified: GameplayAbilities
    // @0x38 on UFortAbilitySet).
    void* abilities_prop = GetProp("/Script/FortniteGame.FortAbilitySet.GameplayAbilities");
    if (abilities_prop) {
        int off = Read<int>(abilities_prop, profile::PropOffset);
        LOG("[Abilities] GameplayAbilities offset = 0x%X (expect 0x38)", off);
        Array* abilities = reinterpret_cast<Array*>(GetField(coreSet, abilities_prop));
        if (abilities && abilities->Count >= 0 && abilities->Count < 256) {
            const char* extra[] = {
                "/Script/FortniteGame.FortGameplayAbility_Sprint",
                "/Script/FortniteGame.FortGameplayAbility_Jump",
                "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_InteractSearch.GA_DefaultPlayer_InteractSearch_C",
                "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_InteractUse.GA_DefaultPlayer_InteractUse_C",
                "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_Harvest.GA_DefaultPlayer_Harvest_C",
                nullptr
            };
            for (int i = 0; extra[i]; ++i) {
                void* klass = LoadClass(extra[i], "/Script/GameplayAbilities.GameplayAbility");
                if (!klass) continue;
                bool present = false;
                for (int j = 0; j < abilities->Count; ++j)
                    if (Read<void*>(abilities->Data, j * 8) == klass) { present = true; break; }
                if (!present) {
                    Fn<int(*)(void*,void*,const void*)>(profile::GenericArrayAdd)(abilities, abilities_prop, &klass);
                    LOG("[Abilities] ✅ Added: %s", extra[i]);
                }
            }
            LOG("[Abilities] Total abilities in set: %d", abilities->Count);
        }
    }

    if (ApplyAbilitySet(session.pawn, coreSet)) session.granted_ability_sets.push_back(coreSet);

    // Fallback direct activations (only works for already-granted abilities).
    const char* abilities[] = {
        "/Script/GameplayAbilities.GameplayAbility_CharacterJump",
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_InteractUse.GA_DefaultPlayer_InteractUse_C",
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_InteractSearch.GA_DefaultPlayer_InteractSearch_C",
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_DefaultPlayer_Death.GA_DefaultPlayer_Death_C",
        "/Game/Athena/Items/ForagedItems/Rift/GA_Rift_Athena_Skydive.GA_Rift_Athena_Skydive_C",
        nullptr
    };
    for (int i = 0; abilities[i]; ++i) {
        void* cls = FindObj(abilities[i]);
        if (!cls) cls = LoadClass(abilities[i], "/Script/GameplayAbilities.GameplayAbility");
        if (!cls) continue;
        GrantAbility(cls);
    }
    session.abilities_granted = true;
    session.melee_granted = true; // [F13] melee flows with the pickaxe weapon intrinsics; no separate grant API exists
    LOG("[Abilities] Done — sets=%zu", session.granted_ability_sets.size());
}

void Athena::GrantDefaultAbilities() { GrantAllAbilitySets(); }

void Athena::RefreshHUD() {
    if (!session.controller) return;
    void* asc = GetASC(session.pawn);
    if (asc) {
        Call c("/Script/GameplayAbilities.AbilitySystemComponent.OnRep_ActivateAbilities");
        if (c.ok()) c.run(asc);
    }
}

// ============================================================================
// QUICKBARS  — [F01/F02/F19] fixed layout + two-bar binding with read-back
// ============================================================================
void Athena::InitializeQuickBarsNative() {
    if (!session.quickbars || IsBadPtr(session.quickbars)) { LOG("[QB] No quickbar"); return; }
    const uint32_t expected[] = {0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd};
    if (memcmp(reinterpret_cast<void*>(g_base + profile::InitializeAthenaQuickBars), expected, sizeof(expected)) != 0)
        LOG("[QB] ⚠️ prologue mismatch at 0x%llX", (unsigned long long)(g_base + profile::InitializeAthenaQuickBars));
    void* vtable = Read<void*>(session.quickbars);
    if (!vtable || IsBadPtr(vtable)) { LOG("[QB] vtable invalid"); return; }
    uintptr_t init_slot = Read<uintptr_t>(vtable, profile::QuickBars_VtableSlot);
    uintptr_t expected_fn = g_base + profile::InitializeAthenaQuickBars;
    LOG("[QB] vtable=%p slot+0x%X=0x%llX expected=0x%llX", vtable, profile::QuickBars_VtableSlot, (unsigned long long)init_slot, (unsigned long long)expected_fn);
    if (init_slot != expected_fn) { LOG("[QB] ❌ vtable mismatch — SKIPPING native call"); return; }
    LOG("[QB] ✅ vtable match — calling native init");
    void* primary = (char*)session.quickbars + profile::QuickBars_PrimaryOffset;
    void* secondary = (char*)session.quickbars + profile::QuickBars_SecondaryOffset;
    Array p_before = Read<Array>(primary, profile::QuickBar_SlotsOffset);
    Array s_before = Read<Array>(secondary, profile::QuickBar_SlotsOffset);
    LOG("[QB] Before: Primary=%d Secondary=%d", p_before.Count, s_before.Count);
    { Guard g; reinterpret_cast<void(*)(void*)>(init_slot)(session.quickbars); }
    Array p_after = Read<Array>(primary, profile::QuickBar_SlotsOffset);
    Array s_after = Read<Array>(secondary, profile::QuickBar_SlotsOffset);
    LOG("[QB] After: Primary=%d Secondary=%d", p_after.Count, s_after.Count);
    if (p_after.Count >= 1 && s_after.Count >= 1) { LOG("[QB] ✅ Init succeeded"); session.quickbars_initialized = true; }
    else LOG("[QB] ⚠️ Init may have failed");
}

// [F01] Manual QuickBar construction — the enable byte is at slot+0x10
// (bEnabled bit0 | bIsDirty bit1 | bIsReserved bit2 | bIsOccupied bit3);
// the old code wrote slot+0x18 which is UsedByItemGuid.
void Athena::BuildQuickBarsManually() {
    if (!session.quickbars) { LOG("[QB] No quickbars"); return; }
    LOG("[QB] Building QuickBars manually...");

    struct SlotEntry { int idx; Guid guid; uint8_t bar; };
    std::vector<SlotEntry> entries;

    void* invProp = GetProp("/Script/FortniteGame.FortInventory.Inventory");
    void* instProp = GetProp("/Script/FortniteGame.FortItemList.ItemInstances");
    if (!invProp || !instProp) return;

    auto collect = [&](std::array<void*,6>& src, uint8_t bar) {
        for (int i = 0; i < 6; ++i) {
            void* inst = src[i];
            if (!inst) continue;
            Call g("/Script/FortniteGame.FortItem.GetItemGuid");
            if (g.ok() && g.run(inst)) entries.push_back({i, g.result<Guid>(), bar});
        }
    };
    collect(session.slots, 0);
    collect(session.secondary_slots, 1);

    LOG("[QB] Collected %zu item GUIDs", entries.size());

    void* barProp = GetProp("/Script/FortniteGame.FortQuickBars.PrimaryQuickBar");
    void* secProp = GetProp("/Script/FortniteGame.FortQuickBars.SecondaryQuickBar");
    void* slotsProp = GetProp("/Script/FortniteGame.QuickBar.Slots");   // [F02] struct name is QuickBar
    if (!barProp || !secProp || !slotsProp) { LOG("[QB] QuickBar reflection missing"); return; }

    auto ensure_slots = [&](void* bar, int want) -> Array {
        void* slotsField = (char*)bar + profile::QuickBar_SlotsOffset;
        Array arr = Read<Array>(slotsField);
        if (arr.Count < want) {
            void* newData = malloc(profile::QuickBarSlot_SlotSize * want);
            if (newData) {
                memset(newData, 0, profile::QuickBarSlot_SlotSize * want);
                Write<void*>(slotsField, 0, newData);
                Write<int32>(slotsField, 8, want);
                Write<int32>(slotsField, 12, want);
                arr = Read<Array>(slotsField);
                LOG("[QB] Allocated %d slots at %p", want, newData);
            }
        }
        return arr;
    };

    Array primArr = ensure_slots(GetField(session.quickbars, barProp), 6);
    Array secArr  = ensure_slots(GetField(session.quickbars, secProp), 5);

    for (auto& e : entries) {
        Array& arr = e.bar == 0 ? primArr : secArr;
        if (e.idx < 0 || e.idx >= arr.Count || !arr.Data) continue;
        void* slot = (char*)arr.Data + (size_t)e.idx * profile::QuickBarSlot_SlotSize;
        // Items array (TArray<FGuid>) at slot+0x00
        void* itemsField = (char*)slot + profile::QuickBarSlot_ItemsOffset;
        Guid* guidData = (Guid*)malloc(16);
        if (!guidData) continue;
        *guidData = e.guid;
        Write<void*>(itemsField, 0, guidData);
        Write<int32>(itemsField, 8, 1);
        Write<int32>(itemsField, 12, 1);
        // [F01] bEnabled bit0 + bIsOccupied bit3 on the flags byte at +0x10
        uint8_t* flags = (uint8_t*)((char*)slot + profile::QuickBarSlot_EnabledOffset);
        *flags |= profile::QuickBarSlot_EnabledBit | profile::QuickBarSlot_OccupiedBit;
        LOG("[QB] Bar%u slot %d -> GUID %08X:%08X", e.bar, e.idx, e.guid.A, e.guid.B);
    }

    session.quickbars_initialized = true;
    LOG("[QB] ✅ Manual initialization done");
}

void Athena::SetQuickBarReady() {
    if (!session.controller) return;
    void* qp = GetProp("/Script/FortniteGame.FortPlayerController.ClientQuickBars");
    if (!qp) { LOG("[QB] ClientQuickBars property not found"); return; }
    int propOff = Read<int>(qp, profile::PropOffset);
    LOG("[QB] ClientQuickBars offset = 0x%X (expect 0x%X)", propOff, profile::Controller_ClientQuickBars);
    if (propOff != (int)profile::Controller_ClientQuickBars) { LOG("[QB] ❌ offset mismatch — SKIPPING"); return; }
    if (!session.quickbars_initialized) { LOG("[QB] ❌ QuickBars not initialized — SKIPPING"); return; }
    uint8_t before = Read<uint8_t>(session.controller, profile::Controller_QuickBarReadyByte);
    if (before > 1) { LOG("[QB] Unexpected byte=%u", before); return; }
    Write<uint8_t>(session.controller, profile::Controller_QuickBarReadyByte, 1);
    LOG("[QB] ✅ Readiness byte: %u -> 1", before);
}

// [F19] Bind through the native Kismet helper, then verify by reading the
// slot back. EquipItemToQuickBarSlot(WorldContextObject, ItemDefinition,
// ItemVariantGuid, QuickBarType, SlotIndex) exists in 21.30.
bool Athena::BindSlot(void* item, int slot, uint8_t bar) {
    if (!item || !session.controller || bar > 1 || slot < 0) return false;
    void* definition = nullptr;
    { Call d("/Script/FortniteGame.FortItem.GetItemDefinitionBP"); if (d.ok() && d.run(item)) definition = d.result<void*>(); }
    if (!definition) return false;
    Guid guid{};
    { Call g("/Script/FortniteGame.FortItem.GetItemGuid"); if (g.ok() && g.run(item)) guid = g.result<Guid>(); }
    if (!(guid.A | guid.B | guid.C | guid.D)) return false;

    Call bind("/Script/FortniteGame.FortKismetLibrary.EquipItemToQuickBarSlot");
    if (bind.ok() && bind.set("WorldContextObject", session.world) &&
        bind.set("ItemDefinition", definition) && bind.set("ItemVariantGuid", guid) &&
        bind.set("QuickBarType", bar) && bind.set("SlotIndex", slot))
        bind.run(FindObj("/Script/FortniteGame.Default__FortKismetLibrary"));

    // Verify through reflection layout checks before trusting anything.
    // (21.30: FQuickBarSlot stride 0x28, Items is TArray<FGuid> — the
    // definitive verification is the GetItemInQuickbarSlot read-back below.)
    void* barProp = GetProp(bar == 0 ? "/Script/FortniteGame.FortQuickBars.PrimaryQuickBar"
                                     : "/Script/FortniteGame.FortQuickBars.SecondaryQuickBar");
    void* slotsProp = GetProp("/Script/FortniteGame.QuickBar.Slots");       // [F02]
    void* itemsProp = GetProp("/Script/FortniteGame.QuickBarSlot.Items");   // [F02]
    if (!barProp || !slotsProp || !itemsProp) return false;
    void* quickbar = GetObjField(session.controller, "/Script/FortniteGame.FortPlayerController.ClientQuickBars");
    if (!quickbar) return false;
    void* barField = GetField(quickbar, barProp);
    Array slots = Read<Array>(GetField(barField, slotsProp));
    if (!slots.Data || slots.Count < 1 || slot >= slots.Count) return false;
    void* target = (char*)slots.Data + (size_t)slot * profile::QuickBarSlot_SlotSize;
    Array values = Read<Array>(GetField(target, itemsProp));
    bool matched = false;
    for (int i = 0; i < values.Count; ++i)
        if (!memcmp((char*)values.Data + (size_t)i * sizeof(Guid), &guid, sizeof(Guid))) matched = true;
    if (!matched && values.Count == 1) {
        Write<Guid>(values.Data, 0, guid);   // stale temporary GUID must not survive reconciliation
        matched = true;
    }
    if (!matched && values.Count == 0) {
        void* itemsField = GetField(target, itemsProp);
        Fn<int(*)(void*,void*,const void*)>(profile::GenericArrayAdd)(itemsField, itemsProp, &guid);
        values = Read<Array>(itemsField);
        matched = values.Count >= 1 && values.Data && !memcmp(values.Data, &guid, sizeof(Guid));
        if (matched) {
            uint8_t* flags = (uint8_t*)((char*)target + profile::QuickBarSlot_EnabledOffset);
            *flags |= profile::QuickBarSlot_EnabledBit | profile::QuickBarSlot_OccupiedBit;
        }
        LOG("[QB] Standalone slot repair bar=%u slot=%d guid_verified=%d", bar, slot, (int)matched);
    }
    // [F19] Read back through the controller API for a definitive answer.
    Call query("/Script/FortniteGame.FortPlayerController.GetItemInQuickbarSlot");
    void* actual = nullptr;
    if (query.ok() && query.set("QuickBarType", bar) && query.set("SlotIndex", slot) && query.run(session.controller))
        actual = query.result<void*>();
    if (actual) matched = matched || (actual == item);
    LOG("[QB] Slot bar=%u slot=%d verified=%d", bar, slot, (int)matched);
    return matched;
}

void Athena::ActivateQuickBarSlot(uint8_t bar, int slot) {
    if (!session.controller) return;
    Call act("/Script/FortniteGame.FortPlayerController.ClientActivateSlot");
    if (!act.ok()) { LOG("[QB] ClientActivateSlot binding failed"); return; }
    float delay = 0.f; bool bUpdatePrev = true; bool bForce = true;
    if (!act.set("InQuickBar", bar) || !act.set("Slot", slot) || !act.set("ActivateDelay", delay) ||
        !act.set("bUpdatePreviousFocusedSlot", bUpdatePrev) || !act.set("bForceExecution", bForce)) {
        LOG("[QB] ClientActivateSlot param binding failed"); return;
    }
    if (act.run(session.controller)) LOG("[QB] ✅ ClientActivateSlot bar=%u slot=%d", bar, slot);
}

void Athena::ForceUpdateQuickBars() {
    if (!session.controller) return;
    for (uint8_t bar : {(uint8_t)0, (uint8_t)1}) {
        Call c("/Script/FortniteGame.FortPlayerController.ClientForceUpdateQuickbar");
        if (c.ok() && c.set("QuickbarToRefresh", bar)) c.run(session.controller);
    }
}

// ============================================================================
// MOVEMENT FEATURES
// ============================================================================
void Athena::EnableMovementFeatures() {
    if (!session.pawn) return;
    LOG("[Movement] Enabling slide & swim...");

    Call canSlide("/Script/FortniteGame.FortPlayerPawn.SetCanSlide");
    if (canSlide.ok()) {
        bool bTrue = true;
        if (canSlide.set("bCanSlide", bTrue)) {
            if (canSlide.run(session.pawn)) LOG("[Movement] ✅ SetCanSlide(true)");
            else LOG("[Movement] SetCanSlide run failed");
        }
    } else LOG("[Movement] SetCanSlide not found");

    SetBool(session.pawn, "/Script/FortniteGame.FortPawn.bWantsToSlide", true);

    void* movement = GetObjField(session.pawn, "/Script/Engine.Character.CharacterMovement");
    if (movement && !IsBadPtr(movement)) {
        const char* swimPaths[] = {
            "/Script/Engine.NavMovementComponent.bCanSwim",
            "/Script/Engine.MovementComponent.bCanSwim",
            "/Script/Engine.CharacterMovementComponent.bCanSwim",
            nullptr
        };
        for (int i = 0; swimPaths[i]; ++i) {
            void* p = GetProp(swimPaths[i]);
            if (p) { SetBool(movement, swimPaths[i], true); LOG("[Movement] ✅ Set %s", swimPaths[i]); break; }
        }
    }

    // [F13] Slide ability rides in the ability set delivery path — activate
    // it directly if the class is available (K2_GiveAbility is gone in 21.30).
    void* slideAbility = LoadClass(
        "/Game/Abilities/Player/Generic/Traits/DefaultPlayer/GA_Athena_Player_Slide.GA_Athena_Player_Slide_C",
        "/Script/GameplayAbilities.GameplayAbility");
    if (slideAbility) {
        LOG("[Movement] Found slide ability — activating through ASC");
        GrantAbility(slideAbility);
    } else {
        LOG("[Movement] Slide ability class not found");
    }

    session.movement_enabled = true;
    LOG("[Movement] Done");
}

// ============================================================================
// TACTICAL SPRINT
// ============================================================================
void Athena::SetupTacticalSprint() {
    if (!session.controller) return;
    if (session.tactical_sprint_setup && session.tactical_sprint) return;
    LOG("[TacticalSprint] Setting up component...");
    void* compClass = FindObj("/Script/TacticalSprintRuntime.FortPlayerControllerComponent_TacticalSprint");
    if (!compClass) { LOG("[TacticalSprint] Component class not found"); session.tactical_sprint_setup = true; return; }
    void* comp = nullptr;
    Call get("/Script/Engine.Actor.GetComponentByClass");
    if (get.ok() && get.set("ComponentClass", compClass) && get.run(session.controller)) comp = get.result<void*>();
    if (!comp || IsBadPtr(comp)) {
        LOG("[TacticalSprint] Adding component...");
        Call add("/Script/Engine.Actor.AddComponentByClass");
        if (add.ok()) {
            bool bManual = false; Transform t{}; bool bDeferred = false;
            add.set("Class_0", compClass) || add.set("Class", compClass);
            add.set("bManualAttachment", bManual);
            add.set("RelativeTransform", t); add.set("bDeferredFinish", bDeferred);
            if (add.run(session.controller)) { comp = add.result<void*>(); LOG("[TacticalSprint] Component added: %p", comp); }
        }
    }
    if (!comp || IsBadPtr(comp)) { LOG("[TacticalSprint] Failed to create component"); session.tactical_sprint_setup = true; return; }
    session.tactical_sprint = comp;
    uint8_t* flags = (uint8_t*)comp + profile::TacticalSprint_Flags;
    *flags |= profile::TacticalSprint_Bit_Enabled;
    LOG("[TacticalSprint] Enabled, flags=0x%02X", *flags);
    Call set("/Script/TacticalSprintRuntime.FortPlayerControllerComponent_TacticalSprint.SetIsSprinting");
    if (set.ok()) {
        bool bSprint = true;
        if (set.set("bNewIsSprinting", bSprint)) set.run(comp);
    }
    session.tactical_sprint_setup = true;
    LOG("[TacticalSprint] ✅ Setup complete");
}
void Athena::SetTacticalSprinting(bool sprinting) {
    if (!session.tactical_sprint) return;
    Call set("/Script/TacticalSprintRuntime.FortPlayerControllerComponent_TacticalSprint.SetIsSprinting");
    if (set.ok() && set.set("bNewIsSprinting", sprinting)) set.run(session.tactical_sprint);
}

void Athena::ApplySprintSpeed() {
    if (!session.pawn || IsBadPtr(session.pawn)) return;
    void* pawnCls = FindObj("/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C");
    if (pawnCls && IsA(session.pawn, pawnCls)) {
        float* sprintSpeed = (float*)((char*)session.pawn + profile::PlayerPawn_Athena_Speed_Sprint);
        if (!IsBadPtr(sprintSpeed)) {
            float old = *sprintSpeed;
            if (std::isfinite(old) && old > 0.0f && old < 5000.0f) {
                if (fabsf(old - 1300.0f) > 1.0f) { *sprintSpeed = 1300.0f; LOG("[Sprint] Speed_Sprint: %.0f -> 1300", old); }
            }
        }
    }
    void* cmc = GetObjField(session.pawn, "/Script/Engine.Character.CharacterMovement");
    if (cmc && !IsBadPtr(cmc)) {
        void* prop = GetProp("/Script/Engine.CharacterMovementComponent.MaxWalkSpeed");
        if (prop) { float* val = (float*)GetField(cmc, prop); if (val && !IsBadPtr(val)) { if (fabsf(*val - 600.f) > 1.0f) *val = 600.f; } }
        void* flyProp = GetProp("/Script/Engine.CharacterMovementComponent.MaxFlySpeed");
        if (flyProp) { float* val = (float*)GetField(cmc, flyProp); if (val && !IsBadPtr(val)) { if (fabsf(*val - 1200.f) > 1.0f) *val = 1200.f; } }
    }
    session.sprint_applied = true;
}

// ============================================================================
// BATTLE BUS / SKYDIVING — [F04] aircraft jump via the 21.30 component
// ============================================================================
static void* GetAircraftComponent(void* controller) {
    if (!controller) return nullptr;
    void* cls = FindObj("/Script/FortniteGame.FortControllerComponent_Aircraft");
    if (!cls) return nullptr;
    Call get("/Script/Engine.Actor.GetComponentByClass");
    if (!get.ok() || !get.set("ComponentClass", cls)) return nullptr;
    if (!get.run(controller)) return nullptr;
    return get.result<void*>();
}

void Athena::SpawnBattleBus() {
    if (!session.mode || !session.state) return;
    LOG("[Bus] Attempting bus...");
    void* gp = GetProp("/Script/FortniteGame.FortGameStateAthena.GamePhase");
    if (gp) {
        uint8_t phase = 3;
        Write<uint8_t>(GetField(session.state, gp), 0, phase);
        Call n("/Script/FortniteGame.FortGameStateAthena.OnRep_GamePhase");
        if (n.ok() && n.set("OldGamePhase", (uint8_t)2)) n.run(session.state);
    }
    if (session.pawn) {
        Vector loc{0.0, 0.0, 50000.0}; Rotator rot{0.0, 0.0, 0.0};
        Call sl("/Script/Engine.Actor.K2_SetActorLocationAndRotation");
        if (sl.ok()) {
            bool bSweep = false, bTeleport = true;
            sl.set("NewLocation", loc); sl.set("NewRotation", rot);
            sl.set("bSweep", bSweep); sl.set("bTeleport", bTeleport);
            sl.run(session.pawn);
        }
        BeginPlayerSkydiving();
    }
    LOG("[Bus] Aircraft phase + skydive dispatched");
}

// [F05] The touch jump button raises UAthenaHUDContext.JumpFromHudStart; the
// standalone build has no authoritative server for the aircraft RPC, so run
// the local skydive fallback and restore the pawn as the view target (Era
// core behaviour, ported with the 21.30 component path added).
void Athena::HandleJumpFromHudStart() {
    if (!session.controller || !session.pawn) return;
    if (session.aircraft_jumped) return;
    Guard g;
    LOG("[Bus] JumpFromHudStart intercepted");
    Vector rot{0, 0, 0};
    void* rotProp = GetProp("/Script/Engine.Controller.ControlRotation");
    if (rotProp && Read<int>(rotProp, profile::PropElementSize) == 24) {
        void* addr = GetField(session.controller, rotProp);
        if (addr) rot = Read<Vector>(addr);
    }
    // [F04] ServerAttemptAircraftJump lives on UFortControllerComponent_Aircraft.
    void* aircraft = GetAircraftComponent(session.controller);
    if (aircraft) {
        Call ac("/Script/FortniteGame.FortControllerComponent_Aircraft.ServerAttemptAircraftJump");
        if (ac.ok()) {
            Rotator r{rot.X, rot.Y, rot.Z};
            if (ac.set("ClientRotation", r)) ac.run(aircraft);
        }
    } else {
        LOG("[Bus] Aircraft component unavailable — local fallback only");
    }
    Call sky("/Script/FortniteGame.FortPlayerPawn.BeginSkydiving");
    if (sky.ok()) { bool bFromBus = true; if (sky.set("bFromBus", bFromBus)) sky.run(session.pawn); }
    Write<uint8_t>(session.pawn, profile::FortPlayerPawn_bIsSkydiving, 1);
    Call cam("/Script/Engine.PlayerController.SetViewTargetWithBlend");
    if (cam.ok()) { cam.set("NewViewTarget", session.pawn); float blend = 0.0f; cam.set("BlendTime", blend); cam.run(session.controller); }
    Call rep("/Script/FortniteGame.FortPlayerPawn.OnRep_IsSkydiving");
    if (rep.ok()) rep.run(session.pawn);
    session.aircraft_jumped = true;
    LOG("[Bus] Skydiving started");
}
void Athena::OnAircraftJump() { HandleJumpFromHudStart(); }
void Athena::BeginPlayerSkydiving() {
    if (!session.pawn || IsBadPtr(session.pawn)) return;
    Call bsd("/Script/FortniteGame.FortPlayerPawn.BeginSkydiving");
    if (bsd.ok()) { bool bFromBus = true; if (bsd.set("bFromBus", bFromBus)) { bsd.run(session.pawn); LOG("[Skydive] BeginSkydiving(true)"); } }
    Write<uint8_t>(session.pawn, profile::FortPlayerPawn_bIsSkydiving, 1);
}

void Athena::SetupCheats() {
    void* pc = session.controller; if (!pc) return;
    void* p = GetProp("/Script/Engine.PlayerController.CheatManager");
    void* klass = FindObj("/Script/FortniteGame.FortCheatManager");
    if (!klass) klass = FindObj("/Script/Engine.CheatManager");
    if (!p || !klass || Read<int>(p, profile::PropElementSize) != 8) return;
    void* cheat = Read<void*>(GetField(pc, p));
    if (!cheat) {
        cheat = Fn<void*(*)(void*, void*)>(profile::SpawnObject)(klass, pc);
        if (cheat) Write<void*>(GetField(pc, p), 0, cheat);
    }
    if (cheat) {
        Call init("/Script/Engine.CheatManager.ReceiveInitCheatManager");
        if (init.ok()) init.run(cheat);
        session.cheats_ready = true;
    }
}

// ============================================================================
// DOORS — 21.30 bDoorOpen is a plain bool byte (0/1)
// ============================================================================
void Athena::HandleDoor(void* door) {
    if (!door || IsBadPtr(door)) return;
    static void* wallCls = nullptr;
    if (!wallCls) wallCls = FindObj("/Script/FortniteGame.BuildingWall");
    if (!wallCls || !IsA(door, wallCls)) return;

    uint8_t* byte = (uint8_t*)door + profile::BuildingWall_bDoorOpen_Byte;
    bool wasOpen = *byte != 0;
    *byte = wasOpen ? 0 : 1;

    Call r("/Script/FortniteGame.BuildingWall.OnRep_bDoorOpen");
    if (r.ok()) r.run(door);

    Call netup("/Script/Engine.Actor.ForceNetUpdate");
    if (netup.ok()) netup.run(door);

    // [F16] Blueprint event — resolve the receiver's override.
    Call interact("/Script/FortniteGame.BuildingActor.BlueprintOnInteract", door);
    if (interact.ok() && session.pawn) {
        if (interact.set("InteractingPawn", session.pawn)) interact.run(door);
    }

    LOG("[Door] Toggled: %s -> %s", wasOpen ? "OPEN" : "CLOSED", wasOpen ? "CLOSED" : "OPEN");
}

// ============================================================================
// COSMETICS — [F22] locker sync through the MCP context, with the menu-hero
// parts fallback from the original hook (Era core setup_cosmetics ported).
// ============================================================================
static std::vector<void*> SoftAssetArray(void* owner, const char* path) {
    std::vector<void*> result;
    void* p = GetProp(path);
    if (!p || !owner || IsBadPtr(owner)) return result;
    // UE5-safe: the element stride is the property's own ElementSize
    // (TSoftObjectPtr in 21.30 is 24 bytes); no 0x80 inner probing needed.
    int stride = Read<int>(p, profile::PropElementSize);
    Array values = Read<Array>(GetField(owner, p));
    if (stride < 8 || stride > 128 || values.Count < 0 || values.Count > 64 || (values.Count && !values.Data)) return result;
    for (int i = 0; i < values.Count; ++i) {
        Call load("/Script/Engine.KismetSystemLibrary.LoadAsset_Blocking");
        if (load.ok() && load.set_bytes("Asset", (char*)values.Data + (size_t)i * stride, stride) &&
            load.run(FindObj("/Script/Engine.Default__KismetSystemLibrary")))
            if (void* value = load.result<void*>()) result.push_back(value);
    }
    return result;
}

// FFortAthenaLoadout shadow (21.30 size 0x110 — verified: last member
// CachedProfileSource @0x108 + 8).
struct FFortAthenaLoadoutShadow { uint8_t Bytes[0x110]; };

void Athena::SyncLockerLoadout() {
    if (!session.controller || !session.pawn) return;
    // [F22] AFortPlayerController.CosmeticLoadoutPC @0x1E70 (FFortAthenaLoadout).
    void* loadoutProp = GetProp("/Script/FortniteGame.FortPlayerController.CosmeticLoadoutPC");
    if (!loadoutProp) { LOG("[Locker] CosmeticLoadoutPC not present"); return; }
    void* loadout = GetField(session.controller, loadoutProp);

    void* character = GetObjField(loadout, "/Script/FortniteGame.FortAthenaLoadout.Character");
    void* pickaxe   = GetObjField(loadout, "/Script/FortniteGame.FortAthenaLoadout.Pickaxe");
    void* backpack  = GetObjField(loadout, "/Script/FortniteGame.FortAthenaLoadout.Backpack");

    // MCP route: GetContext(FortMcpContext).GetLoadoutForPlayer(PlayerID)
    void* info = GetObjField(session.controller, "/Script/FortniteGame.FortPlayerController.MyPlayerInfo");
    void* idProp = GetProp("/Script/FortniteGame.FortRegisteredPlayerInfo.PlayerID");
    if (info && idProp && !character) {
        void* ctxType = FindObj("/Script/FortniteGame.FortMcpContext");
        Call context("/Script/BlueprintContext.BlueprintContextLibrary.GetContext");
        if (ctxType && context.ok() &&
            context.set("ContextObject", session.controller) &&
            (context.set("Class", ctxType) || context.set("Class_0", ctxType)) &&
            context.run(FindObj("/Script/BlueprintContext.Default__BlueprintContextLibrary"))) {
            void* mcp = context.result<void*>();
            int idSize = Read<int>(idProp, profile::PropElementSize);
            Call get("/Script/FortniteGame.FortMcpContext.GetLoadoutForPlayer");
            if (mcp && get.ok() && get.set_bytes("MemberUniqueId", GetField(info, idProp), idSize) && get.run(mcp)) {
                void* value = get.result_data(Read<int>(loadoutProp, profile::PropElementSize));
                if (value) {
                    int size = Read<int>(loadoutProp, profile::PropElementSize);
                    memcpy(loadout, value, (size_t)std::min(size, (int)sizeof(FFortAthenaLoadoutShadow)));
                    character = GetObjField(loadout, "/Script/FortniteGame.FortAthenaLoadout.Character");
                    pickaxe   = GetObjField(loadout, "/Script/FortniteGame.FortAthenaLoadout.Pickaxe");
                    LOG("[Locker] loadout resolved through MCP");
                }
            }
        }
    }
    LOG("[Locker] character=%s pickaxe=%s", ObjName(character).c_str(), ObjName(pickaxe).c_str());
    session.locker_pickaxe = pickaxe;

    // Hero definition → hero type on the player state.
    void* state = GetObjField(session.controller, "/Script/Engine.Controller.PlayerState");
    void* heroTypeProp = GetProp("/Script/FortniteGame.FortPlayerState.HeroType");
    void* heroType = nullptr;
    if (character) {
        void* heroDef = GetObjField(character, "/Script/FortniteGame.AthenaCharacterItemDefinition.HeroDefinition");
        if (heroDef) {
            Call ght("/Script/FortniteGame.FortHero.GetHeroTypeBP");
            if (ght.ok() && ght.run(heroDef)) heroType = ght.result<void*>();
        }
    }
    if (heroType && state && heroTypeProp && Read<int>(heroTypeProp, profile::PropElementSize) == 8) {
        Write<void*>(GetField(state, heroTypeProp), 0, heroType);
        Call r1("/Script/FortniteGame.FortPlayerState.OnRep_HeroType");
        if (r1.ok()) r1.run(state);
    }

    // Mirror the loadout onto the pawn (CosmeticLoadout @0x2A08).
    void* pawnLoadoutProp = GetProp("/Script/FortniteGame.FortPlayerPawn.CosmeticLoadout");
    if (pawnLoadoutProp && Read<int>(pawnLoadoutProp, profile::PropElementSize) == Read<int>(loadoutProp, profile::PropElementSize)) {
        int size = Read<int>(pawnLoadoutProp, profile::PropElementSize);
        memcpy(GetField(session.pawn, pawnLoadoutProp), loadout, (size_t)size);
        LOG("[Locker] pawn CosmeticLoadout mirrored (%d bytes)", size);
    }

    // Character parts: menu hero first, then hero specializations, then backpack.
    std::vector<void*> equipped_parts;
    void* menu = info ? Read<void*>(info, profile::RegisteredPlayerInfo_HeroInstance) : nullptr;
    if (menu && !IsBadPtr(menu)) {
        Array parts = Read<Array>(menu, profile::FortHero_CharacterParts);
        if (parts.Data && parts.Count > 0 && parts.Count <= 16)
            for (int i = 0; i < parts.Count; ++i) equipped_parts.push_back(Read<void*>(parts.Data, (size_t)i * 8));
    }
    void* heroDef = character ? GetObjField(character, "/Script/FortniteGame.AthenaCharacterItemDefinition.HeroDefinition") : nullptr;
    if (heroDef && !IsBadPtr(heroDef)) {
        for (void* spec : SoftAssetArray(heroDef, "/Script/FortniteGame.FortHeroType.Specializations"))
            for (void* part : SoftAssetArray(spec, "/Script/FortniteGame.FortHeroSpecialization.CharacterParts"))
                equipped_parts.push_back(part);
    }
    if (backpack && !IsBadPtr(backpack)) {
        void* bp = GetProp("/Script/FortniteGame.AthenaCharacterPartItemDefinition.CharacterParts");
        if (bp) {
            Array back_parts = Read<Array>(GetField(backpack, bp));
            if (back_parts.Data && back_parts.Count >= 0 && back_parts.Count <= 16)
                for (int i = 0; i < back_parts.Count; ++i) equipped_parts.push_back(Read<void*>(back_parts.Data, (size_t)i * 8));
        }
    }
    for (void* part : equipped_parts) {
        uint8_t type = 255;
        void* partCls = FindObj("/Script/FortniteGame.CustomCharacterPart");
        if (!part || !partCls || !IsA(part, partCls)) continue;
        if (!GetFieldValue<uint8_t>(part, "/Script/FortniteGame.CustomCharacterPart.CharacterPartType", type) || type > 6) continue;
        Call choose("/Script/FortniteGame.FortPlayerPawn.ServerChoosePart");
        if (choose.ok() && choose.set("Part", type) && choose.set("ChosenCharacterPart", part)) {
            choose.run(session.pawn);
            if (std::find(session.character_parts.begin(), session.character_parts.end(), part) == session.character_parts.end())
                session.character_parts.push_back(part);
        }
    }
    if (!equipped_parts.empty() && state) {
        Array parts{session.character_parts.data(), (int32)session.character_parts.size(), (int32)session.character_parts.size()};
        Call apply("/Script/FortniteGame.FortKismetLibrary.ApplyCharacterCosmetics");
        if (apply.ok() && apply.set("WorldContextObject", session.world) && apply.set("CharacterParts", parts) && apply.set("PlayerState", state))
            apply.run(FindObj("/Script/FortniteGame.Default__FortKismetLibrary"));
    }
    if (state) {
        Call r3("/Script/FortniteGame.FortPlayerState.OnRep_CharacterData"); if (r3.ok()) r3.run(state);
        void* klib = FindObj("/Script/FortniteGame.Default__FortKismetLibrary");
        if (klib) {
            Call refresh("/Script/FortniteGame.FortKismetLibrary.UpdatePlayerCustomCharacterPartsVisualization");
            if (refresh.ok() && refresh.set("PlayerState", state)) refresh.run(klib);
        }
    }
}

void Athena::LoadProfileCosmetics() {
    if (!session.pawn || !session.controller) return;
    LOG("[Profile] Loading cosmetics...");
    SyncLockerLoadout();
    if (!session.character_parts.empty()) { ShowSkin(); return; }
    LoadDefaultCosmetics();
}

void Athena::LoadDefaultCosmetics() {
    if (!session.pawn || !session.controller) return;
    LOG("[Cosmetics] Loading hardcoded defaults...");
    const char* partPaths[] = {
        "/Game/Athena/Heroes/Meshes/Heads/F_Med_Head1_ATH.F_Med_Head1_ATH",
        "/Game/Athena/Heroes/Meshes/Bodies/CP_001_Athena_Body.CP_001_Athena_Body",
    };
    void* loaded[2]{};
    for (int i = 0; i < 2; ++i) {
        void* asset = LoadAsset(partPaths[i], "/Script/FortniteGame.CustomCharacterPart");
        if (!asset) continue;
        void* partCls = FindObj("/Script/FortniteGame.CustomCharacterPart");
        if (!partCls || !IsA(asset, partCls)) continue;
        loaded[i] = asset;
        Call choose("/Script/FortniteGame.FortPlayerPawn.ServerChoosePart");
        if (choose.ok() && choose.set("Part", (uint8_t)i) && choose.set("ChosenCharacterPart", asset)) {
            choose.run(session.pawn);
            if (std::find(session.character_parts.begin(), session.character_parts.end(), asset) == session.character_parts.end())
                session.character_parts.push_back(asset);
        }
    }
    void* state = GetObjField(session.controller, "/Script/Engine.Controller.PlayerState");
    if (!state || IsBadPtr(state)) return;
    if (loaded[0] && loaded[1]) {
        Array partsArr{loaded, 2, 2};
        Call apply("/Script/FortniteGame.FortKismetLibrary.ApplyCharacterCosmetics");
        if (apply.ok() && apply.set("WorldContextObject", session.world) && apply.set("CharacterParts", partsArr) && apply.set("PlayerState", state)) {
            void* klib = FindObj("/Script/FortniteGame.Default__FortKismetLibrary");
            if (klib) apply.run(klib);
        }
        void* hero = LoadAsset("/Game/Athena/Heroes/HID_001_Athena_Commando_F.HID_001_Athena_Commando_F", "/Script/FortniteGame.FortHeroType");
        if (!hero) hero = LoadAsset("/Game/Athena/Heroes/HID_001_Athena_Commando_M.HID_001_Athena_Commando_M", "/Script/FortniteGame.FortHeroType");
        void* heroTypeProp = GetProp("/Script/FortniteGame.FortPlayerState.HeroType");
        if (hero && heroTypeProp) Write<void*>(GetField(state, heroTypeProp), 0, hero);
    }
    Call r1("/Script/FortniteGame.FortPlayerState.OnRep_HeroType"); if (r1.ok()) r1.run(state);
    Call r3("/Script/FortniteGame.FortPlayerState.OnRep_CharacterData"); if (r3.ok()) r3.run(state);
    void* klib = FindObj("/Script/FortniteGame.Default__FortKismetLibrary");
    if (klib) {
        Call upd("/Script/FortniteGame.FortKismetLibrary.UpdatePlayerCustomCharacterPartsVisualization");
        if (upd.ok() && upd.set("PlayerState", state)) upd.run(klib);
    }
}
void Athena::ShowSkin() { LoadDefaultCosmetics(); }

void Athena::ForcePlayerVisible() {
    if (!session.pawn || IsBadPtr(session.pawn)) return;
    LOG("[Visibility] Forcing player body visible...");
    SetBool(session.pawn, "/Script/FortniteGame.FortPlayerPawn.bIsSkydiving", false);
    SetBool(session.pawn, "/Script/FortniteGame.FortPlayerPawn.bIsParachuteOpen", false);
    SetBool(session.pawn, "/Script/FortniteGame.FortPlayerPawn.bIsParachuteAvailable", true);
    Call sd("/Script/FortniteGame.FortPlayerPawn.OnRep_IsSkydiving"); if (sd.ok()) sd.run(session.pawn);
    Call po("/Script/FortniteGame.FortPlayerPawn.OnRep_IsParachuteOpen"); if (po.ok()) po.run(session.pawn);
    Call body("/Script/FortniteGame.FortPlayerPawnAthena.SetCharacterBodyVisibilityForPossession");
    if (body.ok()) { bool bShow = true; if (body.set("bVisible", bShow)) { body.run(session.pawn); bool ret = body.result<bool>(); LOG("[Visibility] SetCharacterBodyVisibilityForPossession -> %d", (int)ret); } }
    Call hid("/Script/Engine.Actor.SetActorHiddenInGame");
    if (hid.ok()) { bool bHidden = false; if (hid.set("bNewHidden", bHidden)) hid.run(session.pawn); }
    SetBool(session.pawn, "/Script/Engine.Actor.bHidden", false);
    LoadProfileCosmetics();
    session.visibility_forced = true;
}

// ============================================================================
// PLAYER INITIALIZATION / FINALIZATION
// ============================================================================
static void SetPawnHealth(float value) {
    if (!session.pawn) return;
    // [F23] SetMaxHealth/SetHealth take a single nameless float — positional
    // binding handles them.
    for (const char* method : {"/Script/FortniteGame.FortPawn.SetMaxHealth", "/Script/FortniteGame.FortPawn.SetHealth"}) {
        Call c(method);
        if (c.ok() && c.set_at(0, value)) c.run(session.pawn);
    }
}

bool Athena::TryInitializePlayer(void* world, void* mode, void* controller) {
    // [F15] Standalone gate.
    if (!WorldIsStandalone(world)) { LOG("[Init] World is not standalone — stopping setup"); session.attempted = true; return false; }
    void* pawnCls = LoadClass("/Game/Athena/PlayerPawn_Athena.PlayerPawn_Athena_C", "/Script/Engine.Pawn");
    if (!pawnCls) { LOG("[Init] PlayerPawn_Athena_C missing"); return false; }
    void* existing = GetPawn(controller);
    if (existing && !IsBadPtr(existing) && IsA(existing, pawnCls)) { session.pawn = existing; LOG("[Init] Using existing pawn"); return true; }
    Transform t{};
    t.Location[0] = 0.0; t.Location[1] = 0.0; t.Location[2] = 500.0;
    bool haveSpawn = false;
    Call choose("/Script/Engine.GameModeBase.ChoosePlayerStart");
    if (choose.ok() && choose.set("Player", controller) && choose.run(mode)) {
        void* start = choose.result<void*>();
        if (start && !IsBadPtr(start)) {
            Call loc("/Script/Engine.Actor.K2_GetActorLocation");
            if (loc.ok() && loc.run(start)) {
                Vector v = loc.result<Vector>();
                if (std::isfinite(v.X) && std::isfinite(v.Y) && std::isfinite(v.Z)) {
                    t.Location[0] = v.X; t.Location[1] = v.Y; t.Location[2] = v.Z + 100.0;
                    haveSpawn = true;
                    LOG("[Init] PlayerStart at (%.0f,%.0f,%.0f)", v.X, v.Y, v.Z);
                }
            }
        }
    }
    if (!haveSpawn) { t.Location[0] = 0.0; t.Location[1] = 0.0; t.Location[2] = 3100.0; }
    session.pawn = SpawnActor(world, pawnCls, controller, t);
    if (!session.pawn) { LOG("[Init] Spawn failed"); return false; }
    LOG("[Init] Spawned: %p", session.pawn);
    SetBool(session.pawn, "/Script/FortniteGame.FortPlayerPawn.bIsSkydiving", false);
    SetBool(session.pawn, "/Script/FortniteGame.FortPlayerPawn.bIsParachuteOpen", false);
    Possess(controller, session.pawn);
    if (GetPawn(controller) != session.pawn) { LOG("[Init] Possession did not stick"); return false; }
    LOG("[Init] Possessed OK");
    SetupCheats();
    // [F23] Full health on spawn.
    SetPawnHealth(100.0f);
    Call sd("/Script/FortniteGame.FortPlayerPawn.OnRep_IsSkydiving"); if (sd.ok()) sd.run(session.pawn);
    Call po("/Script/FortniteGame.FortPlayerPawn.OnRep_IsParachuteOpen"); if (po.ok()) po.run(session.pawn);
    Call restart("/Script/Engine.PlayerController.ClientRestart");
    if (restart.ok() && restart.set("NewPawn", session.pawn)) restart.run(controller);
    ForcePlayerVisible();
    return true;
}

void Athena::SetupWorld(void* world) {
    session.world = world;
    session.controller = GetPC(world, 0);
    if (!session.controller) { LOG("SetupWorld: no controller"); return; }
    void* baseCtrl = FindObj("/Script/FortniteGame.FortPlayerController");
    if (!baseCtrl || !IsA(session.controller, baseCtrl)) { LOG("SetupWorld: not FortPlayerController"); return; }
    void* modeProp  = GetProp("/Script/Engine.World.AuthorityGameMode");
    void* stateProp = GetProp("/Script/Engine.World.GameState");
    session.mode  = modeProp  ? Read<void*>(GetField(world, modeProp))  : nullptr;
    session.state = stateProp ? Read<void*>(GetField(world, stateProp)) : nullptr;
    if (!session.mode || !session.state) { LOG("SetupWorld: mode/state missing"); return; }
    LOG("SetupWorld: PC=%s GM=%s GS=%s", ObjName(session.controller).c_str(), ObjName(session.mode).c_str(), ObjName(session.state).c_str());
    if (!TryInitializePlayer(world, session.mode, session.controller)) { LOG("SetupWorld: prerequisites not ready"); return; }
    LOG("SetupWorld: GrantAllAbilitySets"); GrantAllAbilitySets();
    LOG("SetupWorld: SetupTacticalSprint"); SetupTacticalSprint();
    LOG("SetupWorld: EnableMovementFeatures"); EnableMovementFeatures();
    LOG("SetupWorld: ShowSkin"); ShowSkin();
    LOG("SetupWorld: DestroyLods"); DestroyLods();
    LOG("SetupWorld: InitializeInventory"); InitializeInventory();
    LOG("SetupWorld: SpawnFloorLoot"); SpawnFloorLoot();
    LOG("SetupWorld: ApplySprintSpeed"); ApplySprintSpeed();
    LOG("SetupWorld: SpawnSafeZoneIndicator"); SpawnSafeZoneIndicator();
    LOG("SetupWorld: SpawnBuildPreviews"); SpawnBuildPreviews();
    LOG("SetupWorld: DropLoadingScreen"); DropLoadingScreen();
    LOG("SetupWorld: SpawnBattleBus"); SpawnBattleBus();
    LOG("SetupWorld: RemoveNetDebugUI"); RemoveNetDebugUI();
    // [F23] Free building + direct readiness bit (standalone-safe fallbacks).
    SetBool(session.controller, "/Script/FortniteGame.FortPlayerController.bBuildFree", true);
    { uint8_t* ready = (uint8_t*)session.controller + profile::FortPlayerController_ReadyByte;
      *ready |= (uint8_t)(1u << profile::FortPlayerController_ReadyBit); }
    RefreshHUD();
    session.finalize_at = Monotonic() + 2.0;
    session.attempted   = true;
    session.initialized = true;
    LOG("SetupWorld: initial pass done");
}

void Athena::FinalizePlayer() {
    if (session.finalized) return;
    session.finalized = true;
    LOG("=== FinalizePlayer ===");
    void* pc = GetPC(session.world, 0);
    if (pc != session.controller) { LOG("Finalize: controller changed"); return; }
    void* pawnCls = FindObj("/Script/FortniteGame.FortPlayerPawnAthena");
    void* pawn = GetPawn(pc);
    if (!pawn || !pawnCls || !IsA(pawn, pawnCls)) { LOG("Finalize: pawn is not FortPlayerPawnAthena"); return; }
    session.pawn = pawn;
    // Readiness can change possession — refresh cached pawn-dependent state.
    Call ps("/Script/Engine.Pawn.OnRep_PlayerState"); if (ps.ok()) ps.run(pawn);
    if (!session.abilities_granted) GrantAllAbilitySets();
    if (!session.tactical_sprint_setup) SetupTacticalSprint();
    if (!session.movement_enabled) EnableMovementFeatures();
    if (!session.quickbars_initialized) {
        InitializeQuickBarsNative();
        if (!session.quickbars_initialized) BuildQuickBarsManually();
        SetQuickBarReady();
        ForceUpdateQuickBars();
        ActivateQuickBarSlot(0, 0);
    }
    if (!session.ready_called) {
        Call srt("/Script/FortniteGame.FortPlayerController.ServerReadyToStartMatch"); if (srt.ok()) srt.run(pc);
        Call cps("/Script/FortniteGame.FortPlayerControllerZone.ClientOnPawnSpawned"); if (cps.ok()) cps.run(pc);
        session.ready_called = true;
    }
    Call restart("/Script/Engine.PlayerController.ClientRestart");
    if (restart.ok() && restart.set("NewPawn", pawn)) restart.run(pc);
    Call cam("/Script/Engine.PlayerController.SetViewTargetWithBlend");
    if (cam.ok() && cam.set("NewViewTarget", pawn)) cam.run(pc);
    session.visibility_forced = false;
    ForcePlayerVisible();
    SetPawnHealth(100.0f);
    RefreshHUD();
    LOG("Finalize: complete");
}

void Athena::Tick() {
    if (!session.world) return;
    double now = Monotonic();
    if (!session.attempted) {
        if (now - session.retry_at < 1.0) return;
        session.retry_at = now;
        SetupWorld(session.world);
        return;
    }
    if (session.finalize_at > 0 && !session.finalized && now >= session.finalize_at) { FinalizePlayer(); return; }
    static double lastVisibility = 0;
    static double lastSpeedReapply = 0;
    static double lastAbilityCheck = 0;
    static double lastSprintCheck = 0;
    static double lastQBCheck = 0;
    static double lastMovementCheck = 0;
    static int qbRetries = 0;
    if (session.finalized && now - lastVisibility > 8.0) { lastVisibility = now; if (!session.visibility_forced) ForcePlayerVisible(); }
    if (session.finalized && session.pawn && now - lastSpeedReapply > 2.0) { lastSpeedReapply = now; ApplySprintSpeed(); }
    if (session.finalized && session.pawn && now - lastAbilityCheck > 10.0) {
        lastAbilityCheck = now;
        if (!session.abilities_granted) { GrantAllAbilitySets(); RefreshHUD(); }
    }
    if (session.finalized && session.controller && now - lastSprintCheck > 5.0) {
        lastSprintCheck = now;
        if (!session.tactical_sprint_setup || !session.tactical_sprint) { session.tactical_sprint_setup = false; SetupTacticalSprint(); }
    }
    if (session.finalized && session.controller && now - lastQBCheck > 6.0) {
        lastQBCheck = now;
        if (!session.quickbars_initialized && qbRetries++ < 5) {
            LOG("Tick: QuickBars retry #%d", qbRetries);
            InitializeQuickBarsNative();
            if (!session.quickbars_initialized) BuildQuickBarsManually();
            if (session.quickbars_initialized) SetQuickBarReady();
        }
    }
    if (session.finalized && session.pawn && now - lastMovementCheck > 8.0) {
        lastMovementCheck = now;
        if (!session.movement_enabled) EnableMovementFeatures();
    }
    if (session.finalized) PollTouchBuild();
}

void Athena::DestroyLods() {
    void* cm = GetObjField(session.controller, "/Script/Engine.PlayerController.CheatManager");
    if (!cm) { SetupCheats(); cm = GetObjField(session.controller, "/Script/Engine.PlayerController.CheatManager"); }
    if (!cm) return;
    void* cls = FindObj("/Script/FortniteGame.FortHLODSMActor");
    if (!cls) return;
    Call c("/Script/Engine.CheatManager.DestroyAll");
    if (c.ok() && c.set("aClass", cls)) c.run(cm);
    LOG("DestroyLods: done");
}

void Athena::DropLoadingScreen() {
    if (!session.state || !session.controller) return;
    void* gp = GetProp("/Script/FortniteGame.FortGameStateAthena.GamePhase");
    if (gp && Read<int>(gp, profile::PropElementSize) == 1) {
        uint8_t old = Read<uint8_t>(GetField(session.state, gp));
        if (old < 2) {
            uint8_t phase = 2;
            Write<uint8_t>(GetField(session.state, gp), 0, phase);
            Call n("/Script/FortniteGame.FortGameStateAthena.OnRep_GamePhase");
            if (n.ok() && n.set("OldGamePhase", old)) n.run(session.state);
        }
    }
    SetBool(session.controller, "/Script/FortniteGame.FortPlayerController.bHasServerFinishedLoading", true);
    SetBool(session.controller, "/Script/FortniteGame.FortPlayerController.bHasClientFinishedLoading", true);
    void* playlist = FindObj("/Game/Athena/Playlists/Playlist_DefaultSolo.Playlist_DefaultSolo");
    if (!playlist) playlist = FindObj("/Game/Athena/Playlists/Playground/Playlist_Playground.Playlist_Playground");
    if (playlist) {
        void* infoProp = GetProp("/Script/FortniteGame.FortGameStateAthena.CurrentPlaylistInfo");
        void* bp = GetProp("/Script/FortniteGame.PlaylistPropertyArray.BasePlaylist");
        void* op = GetProp("/Script/FortniteGame.PlaylistPropertyArray.OverridePlaylist");
        if (infoProp && bp) Write<void*>(GetField(GetField(session.state, infoProp), bp), 0, playlist);
        if (infoProp && op) Write<void*>(GetField(GetField(session.state, infoProp), op), 0, playlist);
        Call a("/Script/FortniteGame.FortGameStateAthena.OnRep_CurrentPlaylistInfo"); if (a.ok()) a.run(session.state);
    }
    if (!session.ready_called) {
        Call srt("/Script/FortniteGame.FortPlayerController.ServerReadyToStartMatch"); if (srt.ok()) srt.run(session.controller);
        Call cps("/Script/FortniteGame.FortPlayerControllerZone.ClientOnPawnSpawned"); if (cps.ok()) cps.run(session.controller);
        session.ready_called = true;
    }
    LOG("Loading screen dropped");
}

void Athena::InventoryUpdate() {
    if (session.inventory) { Call a("/Script/FortniteGame.FortInventory.HandleInventoryLocalUpdate"); if (a.ok()) a.run(session.inventory); }
    { Call b("/Script/FortniteGame.FortPlayerController.HandleWorldInventoryLocalUpdate"); if (b.ok()) b.run(session.controller); }
    { Call f("/Script/FortniteGame.FortPlayerController.ClientForceWorldInventoryUpdate"); if (f.ok()) f.run(session.controller); }
    // [F12] FortQuickBars OnRep_* and PC.OnRep_QuickBar do not exist in 21.30;
    // the ClientForceUpdateQuickbar calls below are the supported refresh path.
    for (uint8_t bar : {uint8_t(0), uint8_t(1)}) {
        Call c("/Script/FortniteGame.FortPlayerController.ClientForceUpdateQuickbar");
        if (c.ok() && c.set("QuickbarToRefresh", bar)) c.run(session.controller);
    }
}

// ============================================================================
// INVENTORY — [F17/F18/F19] native GUID + reconciliation + stacking + 2 bars
// ============================================================================
static void* ItemDefinitionOf(void* item) {
    Call c("/Script/FortniteGame.FortItem.GetItemDefinitionBP");
    return c.ok() && item && c.run(item) ? c.result<void*>() : nullptr;
}
static Guid ItemGuidOf(void* item) {
    Call c("/Script/FortniteGame.FortItem.GetItemGuid");
    return c.ok() && item && c.run(item) ? c.result<Guid>() : Guid{};
}

// [F17] Era-core give(): create the temporary instance, assign a native GUID
// BEFORE the replicated-entry copy, insert both arrays, then verify the
// surviving instance by GUID after a local inventory refresh.
bool Athena::GiveItem(void* def, int count, int slot, uint8_t bar, bool require_new) {
    if (!def || !session.inventory || count < 1 || count > 9999) return false;
    if (!IsA(def, FindObj("/Script/FortniteGame.FortItemDefinition"))) return false;
    void* invProp   = GetProp("/Script/FortniteGame.FortInventory.Inventory");
    void* instProp  = GetProp("/Script/FortniteGame.FortItemList.ItemInstances");
    void* entryProp = GetProp("/Script/FortniteGame.FortItemList.ReplicatedEntries");
    void* itemEntryProp = GetProp("/Script/FortniteGame.FortWorldItem.ItemEntry");
    if (!invProp || !instProp || !entryProp || !itemEntryProp) return false;
    void* list = GetField(session.inventory, invProp);
    void* instArr  = GetField(list, instProp);
    void* entryArr = GetField(list, entryProp);
    Array current = Read<Array>(instArr);
    Array replicated = Read<Array>(entryArr);
    if (current.Count < 0 || current.Count > 512 || current.Count > current.Max ||
        replicated.Count < 0 || replicated.Count > 512 || replicated.Count > replicated.Max) return false;

    // Duplicate protection — callers can give a different item instead.
    for (int i = 0; i < current.Count; ++i) {
        void* existing = Read<void*>(current.Data, (size_t)i * 8);
        if (existing && ItemDefinitionOf(existing) == def) {
            if (require_new) return false;
            if (slot >= 0 && slot < 6) (bar == 0 ? session.slots : session.secondary_slots)[(size_t)slot] = existing;
            LOG("Already present: %s", ObjName(def).c_str());
            return true;
        }
    }

    // CreateTemporaryItemInstanceBP(Count, Level) — single call, capture result.
    void* inst = nullptr;
    {
        Call c("/Script/FortniteGame.FortItemDefinition.CreateTemporaryItemInstanceBP");
        int lvl = 1;
        if (c.ok() && c.set("Count", count) && c.set("Level", lvl) && c.run(def)) inst = c.result<void*>();
    }
    if (!inst || !IsA(inst, FindObj("/Script/FortniteGame.FortWorldItem"))) return false;
    { Call c("/Script/FortniteGame.FortItem.SetOwningControllerForTemporaryItem");
      if (!c.ok() || !c.set("InController", session.controller) || !c.run(inst)) return false; }

    void* entrySrc = GetField(inst, itemEntryProp);
    if (!entrySrc) return false;
    void* guidProp = GetProp("/Script/FortniteGame.FortItemEntry.ItemGuid");
    if (!guidProp || Read<int>(guidProp, profile::PropElementSize) != sizeof(Guid)) return false;

    // Assign the native GUID before the deep copy so the replicated entry
    // and the instance share identity (Era core ordering).
    Guid guid = MakeGuid();
    if (!(guid.A | guid.B | guid.C | guid.D)) return false;
    Write<Guid>(GetField(entrySrc, guidProp), 0, guid);
    Guid reported = ItemGuidOf(inst);
    if (memcmp(&reported, &guid, sizeof(guid)) != 0) {
        LOG("Temporary item GUID accessor mismatch; insertion refused");
        return false;
    }

    Write<float>(entrySrc, profile::ItemEntry_Durability, 1.0f);
    if (IsA(def, FindObj("/Script/FortniteGame.FortWeaponItemDefinition"))) {
        Write<int32>(entrySrc, profile::ItemEntry_LoadedAmmo, 30);
        if (GetBool(def, "/Script/FortniteGame.FortWeaponItemDefinition.bUsesPhantomReserveAmmo"))
            Write<int32>(entrySrc, profile::ItemEntry_PhantomAmmo, 300);
    }

    auto add = Fn<int(*)(void*,void*,const void*)>(profile::GenericArrayAdd);
    int idx = add(instArr, instProp, &inst);
    if (idx < 0) return false;
    int entryIdx = add(entryArr, entryProp, entrySrc);
    if (entryIdx < 0) { LOG("Entry copy failed after instance insertion; stop granting items"); return false; }

    InventoryUpdate();

    // Reconciliation: the refresh may rebuild cached state; resolve the
    // canonical instance by GUID and refuse silent loss (Era core).
    void* canonical = nullptr;
    Array after = Read<Array>(instArr);
    if (after.Count >= 0 && after.Count <= 512 && after.Data)
        for (int i = 0; i < after.Count; ++i) {
            void* candidate = Read<void*>(after.Data, (size_t)i * 8);
            if (candidate && !IsBadPtr(candidate)) {
                Guid id = ItemGuidOf(candidate);
                if (!memcmp(&id, &guid, sizeof(guid))) { canonical = candidate; break; }
            }
        }
    if (!canonical || ItemDefinitionOf(canonical) != def) {
        LOG("Inventory reconciliation lost %s GUID=%08x:%08x:%08x:%08x",
            ObjName(def).c_str(), guid.A, guid.B, guid.C, guid.D);
        return false;
    }
    inst = canonical;
    LOG("Inventory identity verified %s GUID=%08x:%08x:%08x:%08x",
        ObjName(def).c_str(), guid.A, guid.B, guid.C, guid.D);

    if (slot >= 0 && slot < 6) {
        (bar == 0 ? session.slots : session.secondary_slots)[(size_t)slot] = inst;
        BindSlot(inst, slot, bar);
    }
    LOG("Inventory inserted %s count=%d slot=%d bar=%u indices=%d/%d",
        ObjName(def).c_str(), count, slot, bar, idx, entryIdx);
    return true;
}

// Legacy direct-add kept for ammo/resources (no quickbar binding).
void Athena::AddToInventory(void* def, int count, int slot) {
    if (IsBadPtr(def) || !session.inventory) return;
    GiveItem(def, count, (slot >= 0 && slot < 6) ? slot : -1, 0, false);
}

// [F19] Two-bar starter loadout: pickaxe + weapons on Primary(0), building
// pieces on Secondary(1), ammo/resources unbound. Pickaxe comes from the
// locker when available (AthenaPickaxeItemDefinition.WeaponDefinition).
static void* ResolveWeaponDefForEquip(void* def) {
    if (!def) return nullptr;
    void* pickCls = FindObj("/Script/FortniteGame.AthenaPickaxeItemDefinition");
    if (pickCls && IsA(def, pickCls)) {
        void* weapon = GetObjField(def, "/Script/FortniteGame.AthenaPickaxeItemDefinition.WeaponDefinition");
        if (weapon) return weapon;
    }
    return def;
}

void Athena::InitializeInventory() {
    if (!session.controller) return;
    void* invProp = GetProp("/Script/FortniteGame.FortPlayerController.WorldInventory");
    if (!invProp) return;
    session.inventory = Read<void*>(GetField(session.controller, invProp));
    if (!session.inventory) {
        void* klass = FindObj("/Script/FortniteGame.FortInventory");
        Transform t{};
        session.inventory = SpawnActor(session.world, klass, session.controller, t);
        if (session.inventory) Write<void*>(GetField(session.controller, invProp), 0, session.inventory);
    }
    if (!session.inventory) { LOG("[Inv] No inventory"); return; }
    { Call so("/Script/FortniteGame.FortInventory.SetOwner"); if (so.ok()) so.run(session.inventory); }

    void* qbProp = GetProp("/Script/FortniteGame.FortPlayerController.ClientQuickBars");
    if (qbProp) {
        session.quickbars = Read<void*>(GetField(session.controller, qbProp));
        if (!session.quickbars) {
            LOG("[QB] Spawning FortQuickBarsAthena...");
            void* klass = FindObj("/Script/FortniteGame.FortQuickBarsAthena");
            if (!klass) { LOG("[QB] FortQuickBarsAthena not found, fallback"); klass = FindObj("/Script/FortniteGame.FortQuickBars"); }
            Transform t{};
            session.quickbars = SpawnActor(session.world, klass, session.controller, t);
            if (session.quickbars) {
                Write<void*>(GetField(session.controller, qbProp), 0, session.quickbars);
                LOG("[QB] Spawned: %p class=%s", session.quickbars, ObjName(Read<void*>(session.quickbars, profile::ObjectClass)).c_str());
            }
        }
        if (session.quickbars) {
            Write<void*>((char*)session.quickbars + 0x138, 0, session.controller);
            Call soQB("/Script/FortniteGame.FortQuickBars.SetOwner");
            if (soQB.ok()) soQB.run(session.quickbars);
            LOG("[QB] Running native initialize...");
            InitializeQuickBarsNative();
        }
    }

    LOG("Inventory ready: inv=%p qb=%p", session.inventory, session.quickbars);

    // Pickaxe: locker first, hardcoded fallback second.
    void* pickaxeDef = session.locker_pickaxe;
    if (!pickaxeDef) pickaxeDef = LoadAsset("/Game/Athena/Items/Weapons/WID_Harvest_Pickaxe_Athena_C_T01.WID_Harvest_Pickaxe_Athena_C_T01");
    if (!pickaxeDef) pickaxeDef = ByShortName("WID_Harvest_Pickaxe_Athena_C_T01");
    if (pickaxeDef) GiveItem(pickaxeDef, 1, 0, 0, false);

    // Starter weapon (slot 1 keeps 2..5 free for pickups).
    { void* def = ByShortName("WID_Assault_Auto_Athena_C_Ore_T03");
      if (!def) def = LoadAsset("/Game/Athena/Items/Weapons/WID_Assault_Auto_Athena_C_Ore_T03.WID_Assault_Auto_Athena_C_Ore_T03");
      if (def) GiveItem(def, 1, 1, 0, false); }

    // [F19] Build pieces live on the Secondary quickbar (bar 1).
    struct ToolSlot { const char* path; const char* shortName; int slot; };
    ToolSlot tools[] = {
        {"/Game/Items/Weapons/BuildingTools/BuildingItemData_Wall.BuildingItemData_Wall",        "BuildingItemData_Wall",     0},
        {"/Game/Items/Weapons/BuildingTools/BuildingItemData_Floor.BuildingItemData_Floor",      "BuildingItemData_Floor",    1},
        {"/Game/Items/Weapons/BuildingTools/BuildingItemData_Stair_W.BuildingItemData_Stair_W",  "BuildingItemData_Stair_W",  2},
        {"/Game/Items/Weapons/BuildingTools/BuildingItemData_RoofS.BuildingItemData_RoofS",      "BuildingItemData_RoofS",    3},
    };
    for (auto& t : tools) {
        void* def = LoadAsset(t.path);
        if (!def) def = ByShortName(t.shortName);
        if (def) GiveItem(def, 1, t.slot, 1, false);
    }
    session.edit_tool = LoadAsset("/Game/Items/Weapons/BuildingTools/EditTool.EditTool");
    if (session.edit_tool) GiveItem(session.edit_tool, 1, 4, 1, false);

    const char* ammo[] = {
        "/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsLight.AthenaAmmoDataBulletsLight",
        "/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsMedium.AthenaAmmoDataBulletsMedium",
        "/Game/Athena/Items/Ammo/AthenaAmmoDataBulletsHeavy.AthenaAmmoDataBulletsHeavy",
        "/Game/Athena/Items/Ammo/AthenaAmmoDataShells.AthenaAmmoDataShells",
        "/Game/Athena/Items/Ammo/AmmoDataRockets.AmmoDataRockets", nullptr};
    for (int i = 0; ammo[i]; ++i) { void* def = LoadAsset(ammo[i]); if (def) AddToInventory(def, 999, -1); }
    const char* resources[] = {
        "/Game/Items/ResourcePickups/WoodItemData.WoodItemData",
        "/Game/Items/ResourcePickups/StoneItemData.StoneItemData",
        "/Game/Items/ResourcePickups/MetalItemData.MetalItemData", nullptr};
    for (int i = 0; resources[i]; ++i) { void* def = LoadAsset(resources[i]); if (def) AddToInventory(def, 500, -1); }

    InventoryUpdate();
    ForceUpdateQuickBars();
    ActivateQuickBarSlot(0, 0);
    session.inventory_ready = true;
    EquipSlot(0);
}

// [F19] EquipSlot resolves pickaxe WeaponDefinition and equips by GUID.
void Athena::EquipSlot(int slot) {
    if (slot < 0 || slot >= 6 || !session.slots[(size_t)slot]) return;
    void* item = session.slots[(size_t)slot];
    void* def = ItemDefinitionOf(item);
    if (!def) return;
    Guid guid = ItemGuidOf(item);
    if (!(guid.A | guid.B | guid.C | guid.D)) return;
    void* weaponDef = ResolveWeaponDefForEquip(def);
    if (!weaponDef) return;

    void* itemEntryProp = GetProp("/Script/FortniteGame.FortWorldItem.ItemEntry");
    Guid trackerGuid{};
    if (itemEntryProp) {
        void* entrySrc = GetField(item, itemEntryProp);
        void* trackerProp = GetProp("/Script/FortniteGame.FortItemEntry.TrackerGuid");
        if (entrySrc && trackerProp) trackerGuid = Read<Guid>(GetField(entrySrc, trackerProp));
    }
    Call eq("/Script/FortniteGame.FortPawn.EquipWeaponDefinition");
    if (!eq.ok()) return;
    bool bDisableAnim = false;
    eq.set("WeaponData", weaponDef); eq.set("ItemEntryGuid", guid);
    eq.set("TrackerGuid", trackerGuid); eq.set("bDisableEquipAnimation", bDisableAnim);
    if (eq.run(session.pawn)) {
        void* weapon = eq.result<void*>();
        LOG("Equip %s -> %p", ObjName(weaponDef).c_str(), weapon);
    }
}

void Athena::OnServerExecuteInventoryItem(Guid guid) {
    if (!session.inventory || !session.pawn) return;
    void* invProp = GetProp("/Script/FortniteGame.FortInventory.Inventory");
    void* instProp = GetProp("/Script/FortniteGame.FortItemList.ItemInstances");
    if (!instProp || !invProp) return;
    void* list = GetField(session.inventory, invProp);
    Array arr = Read<Array>(GetField(list, instProp));
    for (int i = 0; i < arr.Count; ++i) {
        void* inst = Read<void*>(arr.Data, (size_t)i * 8);
        if (!inst) continue;
        Guid cand = ItemGuidOf(inst);
        if (!memcmp(&cand, &guid, sizeof(Guid))) {
            for (int s = 0; s < 6; ++s) if (session.slots[(size_t)s] == inst) { EquipSlot(s); return; }
            // Not bound to a slot — still allow equipping weapons.
            void* def = ItemDefinitionOf(inst);
            void* weaponDef = ResolveWeaponDefForEquip(def);
            if (weaponDef) {
                Guid tracker{};
                Call eq("/Script/FortniteGame.FortPawn.EquipWeaponDefinition");
                bool bDisableAnim = false;
                if (eq.ok() && eq.set("WeaponData", weaponDef) && eq.set("ItemEntryGuid", cand) &&
                    eq.set("TrackerGuid", tracker) && eq.set("bDisableEquipAnimation", bDisableAnim))
                    eq.run(session.pawn);
            }
            return;
        }
    }
}
void Athena::OnServerExecuteInventoryWeapon(void* weapon) {
    if (!weapon || !session.pawn) return;
    void* p = GetProp("/Script/FortniteGame.FortWeapon.ItemEntryGuid");
    if (!p) return;
    Guid guid = Read<Guid>(GetField(weapon, p));
    OnServerExecuteInventoryItem(guid);
}

// ============================================================================
// PICKUPS — [F06/F18] entry-based access + stacking (Era core local_pickup)
// ============================================================================
static void* PickupEntry(void* pickup) {
    void* entryProp = GetProp("/Script/FortniteGame.FortPickup.PrimaryPickupItemEntry");
    return pickup && entryProp ? GetField(pickup, entryProp) : nullptr;
}
static void* EntryDefinition(void* entry) {
    void* p = entry ? GetProp("/Script/FortniteGame.FortItemEntry.ItemDefinition") : nullptr;
    return p ? Read<void*>(GetField(entry, p)) : nullptr;
}
static int EntryCount(void* entry) {
    void* p = entry ? GetProp("/Script/FortniteGame.FortItemEntry.Count") : nullptr;
    return p ? Read<int>(GetField(entry, p)) : 0;
}

// [F18] Local pickup with stack transfer and weapon slot assignment.
bool Athena::LocalPickup(void* pickup) {
    if (!pickup || !IsA(pickup, FindObj("/Script/FortniteGame.FortPickup"))) return false;
    Vector location{};
    if (!ActorLocation(pickup, location) || !NearPlayer(location, 600.0f)) return false;
    void* entry = PickupEntry(pickup);
    void* def = EntryDefinition(entry);
    int count = EntryCount(entry);
    if (!def || count < 1 || count > 9999) return true;

    // Stackable: transfer into existing stacks first.
    int maximum = 0;
    void* maxProp = GetProp("/Script/FortniteGame.FortItemDefinition.MaxStackSize");
    if (maxProp && Read<int>(maxProp, profile::PropElementSize) == 8) {
        double v = Read<double>(GetField(def, maxProp));
        if (v > 1.0 && v <= 9999.0) maximum = (int)v;
    }
    if (maximum > 1) {
        void* invProp   = GetProp("/Script/FortniteGame.FortInventory.Inventory");
        void* instProp  = GetProp("/Script/FortniteGame.FortItemList.ItemInstances");
        void* entryProp = GetProp("/Script/FortniteGame.FortItemList.ReplicatedEntries");
        void* itemEntryProp = GetProp("/Script/FortniteGame.FortWorldItem.ItemEntry");
        void* countProp = GetProp("/Script/FortniteGame.FortItemEntry.Count");
        void* guidProp  = GetProp("/Script/FortniteGame.FortItemEntry.ItemGuid");
        if (invProp && instProp && entryProp && itemEntryProp && countProp && guidProp) {
            void* list = GetField(session.inventory, invProp);
            void* instArr = GetField(list, instProp);
            void* entryArr = GetField(list, entryProp);
            Array instances = Read<Array>(instArr);
            Array replicated = Read<Array>(entryArr);
            int stride = Read<int>(itemEntryProp, profile::PropElementSize);
            for (int i = 0; i < instances.Count && count > 0; ++i) {
                void* item = Read<void*>(instances.Data, (size_t)i * 8);
                if (!item || ItemDefinitionOf(item) != def) continue;
                void* itemEntry = GetField(item, itemEntryProp);
                int have = Read<int>(GetField(itemEntry, countProp));
                int amount = std::min(count, std::max(0, maximum - have));
                Guid id = Read<Guid>(GetField(itemEntry, guidProp));
                void* matching = nullptr;
                for (int j = 0; j < replicated.Count; ++j) {
                    void* candidate = (char*)replicated.Data + (size_t)j * stride;
                    Guid other = Read<Guid>(GetField(candidate, guidProp));
                    if (!memcmp(&id, &other, sizeof(id))) { matching = candidate; break; }
                }
                if (!matching || amount < 1) break;
                int updated = have + amount;
                Write<int>(GetField(itemEntry, countProp), 0, updated);
                Write<int>(GetField(matching, countProp), 0, updated);
                count -= amount;
            }
            if (count < EntryCount(entry)) {
                if (count > 0) { Write<int>(GetField(entry, countProp), 0, count); }
                else { Call d("/Script/Engine.Actor.K2_DestroyActor"); if (d.ok()) d.run(pickup); }
                InventoryUpdate();
                LOG("Pickup stacked %s remainder=%d", ObjName(def).c_str(), count);
                return true;
            }
            if (count == 0) return true;
        }
    }

    // Weapon/item pickup: first free primary slot (0 is the pickaxe).
    int slot = -1;
    if (IsA(def, FindObj("/Script/FortniteGame.FortWeaponItemDefinition"))) {
        for (int i = 1; i < 6; ++i)
            if (!session.slots[(size_t)i]) { slot = i; break; }
        if (slot < 0) { LOG("Pickup retained: primary quickbar full"); return true; }
    }
    if (!GiveItem(def, count, slot, 0, true)) {
        LOG("Pickup retained: duplicate, full stack, or insertion failed (%s)", ObjName(def).c_str());
        return true;
    }
    Call d("/Script/Engine.Actor.K2_DestroyActor");
    if (d.ok()) d.run(pickup);
    InventoryUpdate();
    LOG("Local pickup transferred %s count=%d slot=%d", ObjName(def).c_str(), count, slot);
    return true;
}

void Athena::ServerHandlePickup(void* pickup) { LocalPickup(pickup); }

// [F06] SpawnPickup writes the FFortPickup entry (ItemDefinition/Count no
// longer exist on AFortPickup in 21.30) and tags source flags so the pickup
// widget/despawn behave like native loot.
void Athena::SpawnPickup(void* def, int count, Vector loc, uint8_t sourceFlags, uint8_t spawnSource, bool toss) {
    if (!def || !session.world) return;
    void* klass = FindObj("/Script/FortniteGame.FortPickupAthena");
    if (!klass) return;
    Transform t{};
    t.Location[0] = loc.X; t.Location[1] = loc.Y; t.Location[2] = loc.Z;
    void* pickup = SpawnActor(session.world, klass, session.pawn, t);
    if (!pickup) return;
    void* entry = PickupEntry(pickup);
    void* defProp = GetProp("/Script/FortniteGame.FortItemEntry.ItemDefinition");
    void* cntProp = GetProp("/Script/FortniteGame.FortItemEntry.Count");
    if (entry && defProp) Write<void*>(GetField(entry, defProp), 0, def);
    if (entry && cntProp) Write<int>(GetField(entry, cntProp), 0, count);
    Write<uint8_t>(pickup, profile::AFortPickup_SourceFlags, sourceFlags);
    Write<uint8_t>(pickup, profile::AFortPickup_SpawnSource, spawnSource);
    Call rep("/Script/FortniteGame.FortPickup.OnRep_PrimaryPickupItemEntry"); if (rep.ok()) rep.run(pickup);
    if (toss) {
        Call c("/Script/FortniteGame.FortPickup.TossPickup");
        if (c.ok()) {
            c.set("FinalLocation", loc);
            c.set("ItemOwner", session.pawn);
            int overrideMax = -1; c.set("OverrideMaxStackCount", overrideMax);
            bool bToss = true; c.set("bToss", bToss);
            bool combine = true; c.set("bShouldCombinePickupsWhenTossCompletes", combine);
            // EEFortPickupSourceTypeFlag (uint8) / EEFortPickupSpawnSource (uint8)
            c.set_any((uint8_t)sourceFlags, "InPickupSourceTypeFlags");
            c.set_any((uint8_t)spawnSource, "InPickupSpawnSource");
            c.run(pickup);
        }
    }
}

void Athena::DropInventoryItem(Guid guid, int count) {
    if (!session.inventory) return;
    void* invProp = GetProp("/Script/FortniteGame.FortInventory.Inventory");
    void* instProp = GetProp("/Script/FortniteGame.FortItemList.ItemInstances");
    void* entryProp = GetProp("/Script/FortniteGame.FortItemList.ReplicatedEntries");
    if (!invProp || !instProp || !entryProp) return;
    void* list = GetField(session.inventory, invProp);
    void* instArr = GetField(list, instProp);
    void* entryArr = GetField(list, entryProp);
    Array arr = Read<Array>(instArr);
    for (int i = 0; i < arr.Count; ++i) {
        void* inst = Read<void*>(arr.Data, (size_t)i * 8);
        if (!inst) continue;
        Guid cand = ItemGuidOf(inst);
        if (!memcmp(&cand, &guid, sizeof(Guid))) {
            // Spawn the visual pickup before removing the item.
            void* def = ItemDefinitionOf(inst);
            Vector loc{};
            if (session.pawn) ActorLocation(session.pawn, loc);
            loc.Z += 60.0;
            if (def) {
                int dropped = count > 0 ? std::min(count, 9999) : 1;
                SpawnPickup(def, dropped, loc, 16 /*Tossed*/, 10 /*TossedByPlayer*/, true);
            }
            Fn<void(*)(void*,void*,int)>(profile::GenericArrayRemove)(instArr, instProp, i);
            Array entries = Read<Array>(entryArr);
            if (i < entries.Count)
                Fn<void(*)(void*,void*,int)>(profile::GenericArrayRemove)(entryArr, entryProp, i);
            for (int s = 0; s < 6; ++s) if (session.slots[(size_t)s] == inst) session.slots[(size_t)s] = nullptr;
            for (int s = 0; s < 6; ++s) if (session.secondary_slots[(size_t)s] == inst) session.secondary_slots[(size_t)s] = nullptr;
            InventoryUpdate();
            return;
        }
    }
}

// ============================================================================
// LOOT — [F20] local rolling (Era core search_container ported to 21.30)
// ============================================================================
namespace local_loot {
    struct Row  { const char* name; float weight; int count; };
    struct Tier { const char* group; int weaponsMin, weaponsMax, ammoMin, ammoMax, resources; float rarityBoost; };

    // Weapon short names — every row is resolved through ByShortName and
    // silently skipped when the asset is absent, so the table is safe to
    // extend for other seasons.
    static const Row weapons[] = {
        {"WID_Assault_Auto_Athena_C_Ore_T03",        30, 1},
        {"WID_Shotgun_Standard_Athena_C_Ore_T03",    25, 1},
        {"WID_Shotgun_SemiAuto_Athena_C_Ore_T03",    20, 1},
        {"WID_Sniper_Standard_Athena_C_Ore_T03",     12, 1},
        {"WID_Assault_Semi_Athena_C_Ore_T03",        12, 1},
        {"WID_SMG_Tactical_Athena_C_Ore_T03",        12, 1},
        {"WID_Launcher_Rocket_Athena_C_Ore_T03",      4, 1},
        {"WID_Minigun_Athena_C_Ore_T03",              3, 1},
    };
    static const Row ammo[] = {
        {"AthenaAmmoDataBulletsLight",   30, 60},
        {"AthenaAmmoDataBulletsMedium",  34, 60},
        {"AthenaAmmoDataBulletsHeavy",   14, 30},
        {"AthenaAmmoDataShells",         18, 24},
        {"AmmoDataRockets",               4, 4},
    };
    static const Row resources[] = {
        {"WoodItemData",  40, 50},
        {"StoneItemData", 34, 50},
        {"MetalItemData", 26, 50},
    };
    // Loot-tier-group → package mapping (21.30 SearchLootTierGroup names).
    static const Tier tiers[] = {
        {"Loot_AthenaTreasure",       2, 3, 2, 3, 1, 1.6f},
        {"Loot_Treasure",             2, 3, 2, 3, 1, 1.6f},   // legacy alias
        {"Loot_AthenaAmmoLarge",      0, 0, 3, 4, 0, 0.0f},
        {"Loot_AthenaAmmoSmall",      0, 0, 2, 3, 0, 0.0f},
        {"Loot_AthenaFloorLoot",      1, 1, 1, 2, 1, 0.0f},
    };

    static float random01() {
        static uint32_t state = 0;
        if (!state) state = (uint32_t)(Monotonic() * 1000000.0) | 1;
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return float(state >> 8) / 16777216.0f;
    }
    static void* resolve(const Row& row) {
        std::string shortName(row.name);
        void* def = ByShortName(shortName);
        if (def) return def;
        // Try the classic content paths before giving up.
        const char* prefixes[] = {
            "/Game/Athena/Items/Weapons/",
            "/Game/Athena/Items/Ammo/",
            "/Game/Items/ResourcePickups/",
            nullptr
        };
        for (int i = 0; prefixes[i]; ++i) {
            std::string path = std::string(prefixes[i]) + shortName + "." + shortName;
            def = LoadAsset(path);
            if (def) return def;
        }
        return nullptr;
    }
    static void* pickWeapon(float boost) {
        float total = 0;
        for (auto& w : weapons) total += w.weight * (1.0f + boost * (w.weight < 10.0f ? 2.0f : 0.0f));
        float pick = random01() * total;
        for (auto& w : weapons) {
            float weight = w.weight * (1.0f + boost * (w.weight < 10.0f ? 2.0f : 0.0f));
            pick -= weight;
            if (pick < 0) return resolve(w);
        }
        return nullptr;
    }
    static void* pickRow(const Row* table, size_t n) {
        float total = 0;
        for (size_t i = 0; i < n; ++i) total += table[i].weight;
        float pick = random01() * total;
        for (size_t i = 0; i < n; ++i) {
            pick -= table[i].weight;
            if (pick < 0) return resolve(table[i]);
        }
        return nullptr;
    }
    static int rollBetween(int lo, int hi) { return lo + (int)(random01() * float(hi - lo + 1)); }
}

bool Athena::SearchContainer(void* actor, const Vector& location) {
    void* searchedProp = GetProp("/Script/FortniteGame.BuildingContainer.bAlreadySearched");
    if (!actor || !searchedProp) return false;
    if (GetBool(actor, "/Script/FortniteGame.BuildingContainer.bAlreadySearched")) return true;

    // Read the container's loot tier group (FName).
    std::string group;
    void* groupProp = GetProp("/Script/FortniteGame.BuildingContainer.SearchLootTierGroup");
    if (groupProp) {
        FStringNative text{};
        Fn<void(*)(const void*, FStringNative*)>(profile::NameAppendString)(GetField(actor, groupProp), &text);
        group = Narrow(text.Data, 128);
        if (text.Data) Fn<void(*)(void*)>(profile::Free)(text.Data);
    }
    if (group == "Loot_Treasure") group = "Loot_AthenaTreasure";
    if (group == "Loot_Ammo") group = "Loot_AthenaAmmoLarge";

    const local_loot::Tier* tier = nullptr;
    for (auto& t : local_loot::tiers)
        if (group == t.group) { tier = &t; break; }
    if (!tier) { LOG("[Loot] group not ported: %s", group.c_str()); return false; } // let caller try native

    struct Drop { void* def; int count; };
    std::vector<Drop> drops;
    int weapons = tier->weaponsMax > tier->weaponsMin ? local_loot::rollBetween(tier->weaponsMin, tier->weaponsMax) : tier->weaponsMin;
    int ammo = tier->ammoMax > tier->ammoMin ? local_loot::rollBetween(tier->ammoMin, tier->ammoMax) : tier->ammoMin;
    for (int i = 0; i < weapons; ++i)
        if (void* def = local_loot::pickWeapon(tier->rarityBoost)) drops.push_back({def, 1});
    for (int i = 0; i < ammo; ++i)
        if (void* def = local_loot::pickRow(local_loot::ammo, sizeof(local_loot::ammo) / sizeof(local_loot::Row))) {
            int cnt = 30 + (int)(local_loot::random01() * 60.0f);
            drops.push_back({def, cnt});
        }
    for (int i = 0; i < tier->resources; ++i)
        if (void* def = local_loot::pickRow(local_loot::resources, sizeof(local_loot::resources) / sizeof(local_loot::Row)))
            drops.push_back({def, 50});
    if (drops.empty()) return false;

    // Resolve/spawn every pickup first; roll back partial spawns so a retry
    // cannot duplicate loot (Era core guarantee).
    std::vector<void*> spawned;
    for (size_t i = 0; i < drops.size(); ++i) {
        Vector loc{location.X + 60.0 + double(i % 3) * 35.0,
                   location.Y + double(i / 3) * 35.0,
                   location.Z + 50.0};
        // sourceFlags: Container(4) | Player(1); spawnSource: Chest(2)/AmmoBox(4)/FloorLoot? use LootDrop(9) for generic
        uint8_t spawnSource = 9; // LootDrop
        if (group == "Loot_AthenaTreasure") spawnSource = 2; // Chest
        if (group.find("Ammo") != std::string::npos) spawnSource = 4; // AmmoBox
        void* pickup = nullptr;
        {
            void* klass = FindObj("/Script/FortniteGame.FortPickupAthena");
            Transform t{};
            t.Location[0] = loc.X; t.Location[1] = loc.Y; t.Location[2] = loc.Z;
            pickup = klass ? SpawnActor(session.world, klass, nullptr, t) : nullptr;
            if (pickup) {
                void* entry = PickupEntry(pickup);
                void* defProp = GetProp("/Script/FortniteGame.FortItemEntry.ItemDefinition");
                void* cntProp = GetProp("/Script/FortniteGame.FortItemEntry.Count");
                if (entry && defProp) Write<void*>(GetField(entry, defProp), 0, drops[i].def);
                if (entry && cntProp) Write<int>(GetField(entry, cntProp), 0, drops[i].count);
                Write<uint8_t>(pickup, profile::AFortPickup_SourceFlags, 4);
                Write<uint8_t>(pickup, profile::AFortPickup_SpawnSource, spawnSource);
                Call rep("/Script/FortniteGame.FortPickup.OnRep_PrimaryPickupItemEntry");
                if (rep.ok()) rep.run(pickup);
            }
        }
        if (!pickup) {
            for (void* previous : spawned) { Call d("/Script/Engine.Actor.K2_DestroyActor"); if (d.ok()) d.run(previous); }
            LOG("Container pickup spawn failed; search not consumed");
            return true;
        }
        spawned.push_back(pickup);
    }
    SetBool(actor, "/Script/FortniteGame.BuildingContainer.bAlreadySearched", true);
    Call r1("/Script/FortniteGame.BuildingContainer.OnRep_bAlreadySearched"); if (r1.ok()) r1.run(actor);
    Call r2("/Script/FortniteGame.BuildingContainer.BounceContainer"); if (r2.ok()) r2.run(actor);
    LOG("[Loot] ✅ Container search %s spawned=%zu", group.c_str(), spawned.size());
    return true;
}

void Athena::Loot(void* actor) {
    if (!actor || !session.pawn) return;
    LOG("[Loot] Opening %s", ObjName(Read<void*>(actor, profile::ObjectClass)).c_str());
    Vector location{};
    bool haveLoc = ActorLocation(actor, location);

    // Local roll first (offline-deterministic); fall back to the native
    // BP_SpawnLoot when the group is not ported.
    if (haveLoc && SearchContainer(actor, location)) return;
    Call spawn("/Script/FortniteGame.BuildingContainer.BP_SpawnLoot");
    if (spawn.ok()) {
        if (spawn.set("PlayerPawn", session.pawn)) {
            if (spawn.run(actor)) {
                SetBool(actor, "/Script/FortniteGame.BuildingContainer.bAlreadySearched", true);
                Call r1("/Script/FortniteGame.BuildingContainer.OnRep_bAlreadySearched"); if (r1.ok()) r1.run(actor);
                Call r2("/Script/FortniteGame.BuildingContainer.BounceContainer"); if (r2.ok()) r2.run(actor);
                LOG("[Loot] ✅ native BP_SpawnLoot path");
                return;
            }
        }
    }
    LOG("[Loot] ❌ container search failed (local+native)");
}

void Athena::SpawnFloorLoot() {
    LOG("[Loot] Spawning floor loot...");
    void* wb = FindObj("/Script/Engine.Default__GameplayStatics");
    if (!wb) { LOG("[Loot] GameplayStatics missing"); return; }
    const char* containers[] = {
        "/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_01.Tiered_Athena_FloorLoot_01_C",
        "/Game/Athena/Environments/Blueprints/Tiered_Athena_FloorLoot_Warmup.Tiered_Athena_FloorLoot_Warmup_C",
        nullptr
    };
    int total = 0;
    for (int i = 0; containers[i]; ++i) {
        void* cls = FindObj(containers[i]);
        if (!cls) cls = LoadClass(containers[i], "/Script/Engine.Actor");
        if (!cls) continue;
        Call gac("/Script/Engine.GameplayStatics.GetAllActorsOfClass");
        if (!gac.ok()) break;
        gac.set("WorldContextObject", session.world);
        gac.set("ActorClass", cls);
        if (!gac.run(wb)) continue;
        Array actors = gac.result<Array>();
        LOG("[Loot] Type %d: %d actors", i, actors.Count);
        for (int j = 0; j < actors.Count; ++j) {
            void* c = Read<void*>(actors.Data, (size_t)j * 8);
            if (!c || IsBadPtr(c)) continue;
            // Native BP path first (it rolls the real cooked tables).
            Call bp("/Script/FortniteGame.BuildingContainer.BP_SpawnLoot");
            bool ok = false;
            if (bp.ok() && bp.set("PlayerPawn", session.pawn)) ok = bp.run(c);
            if (!ok && session.pawn) {
                // Fallback: local roll beside the container.
                Vector loc{};
                if (ActorLocation(c, loc)) ok = SearchContainer(c, loc);
            }
            if (ok) ++total;
        }
    }
    LOG("[Loot] ✅ Total %d containers looted", total);
}

// ============================================================================
// BUILDING — [F07/F21] local preview build (Era core) + fixed RPC parse
// ============================================================================
// Unreal FRotator (Pitch/Yaw/Roll degrees) → FQuat for the 21.30 scalar
// double-precision FTransform (Era core conversion, in doubles).
static void RotatorToQuat(const Rotator& r, double out[4]) {
    const double h = 0.00872664626; // pi / 360
    double sp = std::sin(r.Pitch * h), cp = std::cos(r.Pitch * h);
    double sy = std::sin(r.Yaw * h),   cy = std::cos(r.Yaw * h);
    double sr = std::sin(r.Roll * h),  cr = std::cos(r.Roll * h);
    out[0] = cr * sp * sy - sr * cp * cy;
    out[1] = -cr * sp * cy - sr * cp * sy;
    out[2] = cr * cp * sy - sr * sp * cy;
    out[3] = cr * cp * cy + sr * sp * sy;
}

// [F21] Build from the controller's local preview state — the mobile client
// keeps LastBuildPreviewGridSnapLoc/Rot + CurrentBuildableClass updated while
// the build marker is visible, so no server payload is required.
bool Athena::LocalBuild() {
    if (!session.controller || !session.world) return false;
    void* klass = nullptr;
    Vector loc{};
    Rotator rot{};
    if (!GetFieldValue<Vector>(session.controller, "/Script/FortniteGame.FortPlayerController.LastBuildPreviewGridSnapLoc", loc) ||
        !GetFieldValue<Rotator>(session.controller, "/Script/FortniteGame.FortPlayerController.LastBuildPreviewGridSnapRot", rot)) {
        LOG("[Build] local preview properties unavailable");
        return true;
    }
    void* classProp = GetProp("/Script/FortniteGame.FortPlayerController.CurrentBuildableClass");
    if (classProp) klass = Read<void*>(GetField(session.controller, classProp));
    void* smCls = FindObj("/Script/FortniteGame.BuildingSMActor");
    if (!klass || !smCls || !SubclassOfFast(klass, smCls) || !NearPlayer(loc, 2000.0f) ||
        !std::isfinite(rot.Pitch) || !std::isfinite(rot.Yaw) || !std::isfinite(rot.Roll)) {
        LOG("[Build] invalid selected class or preview transform");
        return true;
    }
    // Mirroring from the controller state (bBuildPreviewMirrored @0x1859).
    bool mirrored = false;
    { uint8_t flag = Read<uint8_t>(session.controller, profile::Controller_BuildPreviewMirrored);
      mirrored = flag != 0; }

    // Dedup: identical class+pose within 0.25s (touch repeat protection).
    double pose[6] = {loc.X, loc.Y, loc.Z, rot.Pitch, rot.Yaw, rot.Roll};
    double now = Monotonic();
    if (klass == session.last_build_class && now - session.last_build_time < 0.25) {
        bool same = true;
        for (int i = 0; i < 6; ++i) if (session.last_build_pose[(size_t)i] != pose[i]) { same = false; break; }
        if (same) return true;
    }
    LOG("[Build] preview %s at %.0f %.0f %.0f", ObjName(klass).c_str(), loc.X, loc.Y, loc.Z);

    Transform t{};
    t.Location[0] = loc.X; t.Location[1] = loc.Y; t.Location[2] = loc.Z;
    RotatorToQuat(rot, t.Rotation);
    void* actor = SpawnActor(session.world, klass, session.controller, t);
    if (!actor) { LOG("[Build] spawn returned null"); return true; }
    session.last_build_class = klass;
    session.last_build_time = now;
    for (int i = 0; i < 6; ++i) session.last_build_pose[(size_t)i] = pose[i];

    SetBool(actor, "/Script/FortniteGame.BuildingActor.bPlayerPlaced", true);
    // [F03] SetMirrored lives on BuildingSMActor in 21.30.
    Call mirror("/Script/FortniteGame.BuildingSMActor.SetMirrored");
    if (mirror.ok()) { bool m = mirrored; if (mirror.set("bIsMirrored", m)) mirror.run(actor); }
    Call init("/Script/FortniteGame.BuildingActor.InitializeKismetSpawnedBuildingActor");
    bool animations = true;
    if (init.ok() && init.set("BuildingOwner", actor) && init.set("SpawningController", session.controller) &&
        init.set("bUsePlayerBuildAnimations", animations) && init.run(actor)) {
        LOG("[Build] ✅ local build initialized %s", ObjName(klass).c_str());
    } else {
        LOG("[Build] init failed — destroying piece");
        Call d("/Script/Engine.Actor.K2_DestroyActor");
        if (d.ok()) d.run(actor);
    }
    return true;
}

// [F21] Mobile touch placement: poll the touch-primary-fire hold while in
// build mode and place when the preview pose changes (Era core behaviour).
void Athena::PollTouchBuild() {
    if (!session.controller || !session.world) return;
    double now = Monotonic();
    if (now < session.next_build_poll) return;
    session.next_build_poll = now + 0.05;
    bool held = false;
    if (!GetFieldValue<bool>(session.controller, "/Script/FortniteGame.FortPlayerController.bHoldingPrimaryFireFromTouch", held)) return;
    if (!held) return;
    Call mode("/Script/FortniteGame.FortPlayerController.IsInBuildMode");
    if (!mode.ok() || !mode.run(session.controller) || !mode.result<bool>()) return;
    Vector loc{};
    Rotator rot{};
    if (!GetFieldValue<Vector>(session.controller, "/Script/FortniteGame.FortPlayerController.LastBuildPreviewGridSnapLoc", loc) ||
        !GetFieldValue<Rotator>(session.controller, "/Script/FortniteGame.FortPlayerController.LastBuildPreviewGridSnapRot", rot)) return;
    void* classProp = GetProp("/Script/FortniteGame.FortPlayerController.CurrentBuildableClass");
    void* klass = classProp ? Read<void*>(GetField(session.controller, classProp)) : nullptr;
    double pose[6] = {loc.X, loc.Y, loc.Z, rot.Pitch, rot.Yaw, rot.Roll};
    if (session.held_build_class == klass && now - session.held_build_time < 0.25) {
        bool same = true;
        for (int i = 0; i < 6; ++i) if (session.held_build_pose[(size_t)i] != pose[i]) { same = false; break; }
        if (same) return;
    }
    session.held_build_class = klass;
    session.held_build_time = now;
    for (int i = 0; i < 6; ++i) session.held_build_pose[(size_t)i] = pose[i];
    LOG("[Build] touch placement confirm observed");
    LocalBuild();
}

// [F07] ServerCreateBuildingActor parse — FCreateBuildingActorData (0x50):
//   BuildingClassHandle u32 @0x00, BuildLoc FVector @0x08, BuildRot FRotator
//   @0x20, bMirrored @0x38, SyncKey f32 @0x3C, BuildingClassData @0x40
//   (BuildingClass UClass* @0x40).
void Athena::OnServerCreateBuildingActor(void* params) {
    if (!params) return;
    void* fn = FindObj("/Script/FortniteGame.FortPlayerController.ServerCreateBuildingActor");
    uint16_t total = fn ? Read<uint16_t>(fn, profile::FunctionParamSize) : 0;
    if (total == 0x50) {
        void* cls = Read<void*>(params, 0x40);
        Vector loc = Read<Vector>(params, 0x08);
        Rotator rot = Read<Rotator>(params, 0x20);
        bool mirrored = Read<uint8_t>(params, 0x38) != 0;
        if (!cls) { LOG("[Build] RPC class handle-only placement — using local preview"); LocalBuild(); return; }
        Transform t{};
        t.Location[0] = loc.X; t.Location[1] = loc.Y; t.Location[2] = loc.Z;
        RotatorToQuat(rot, t.Rotation);
        void* actor = SpawnActor(session.world, cls, session.controller, t);
        if (!actor) return;
        SetBool(actor, "/Script/FortniteGame.BuildingActor.bPlayerPlaced", true);
        Call mirror("/Script/FortniteGame.BuildingSMActor.SetMirrored");
        if (mirror.ok()) { bool m = mirrored; if (mirror.set("bIsMirrored", m)) mirror.run(actor); }
        Call init("/Script/FortniteGame.BuildingActor.InitializeKismetSpawnedBuildingActor");
        bool bUse = true;
        if (init.ok()) {
            init.set("BuildingOwner", actor); init.set("SpawningController", session.controller);
            init.set("bUsePlayerBuildAnimations", bUse); init.run(actor);
        }
    } else {
        LOG("[Build] unexpected ServerCreateBuildingActor size %u — using local preview", total);
        LocalBuild();
    }
}

// [F10] Edit tool — EditActor and OnRep_EditActor live on FortWeap_EditingTool.
void Athena::OnBeginEditActor(void* piece) {
    if (!session.edit_tool || !session.pawn || !piece) return;
    void* inst = nullptr;
    {
        Call c("/Script/FortniteGame.FortItemDefinition.CreateTemporaryItemInstanceBP");
        int cnt = 1, lvl = 1;
        if (c.ok() && c.set("Count", cnt) && c.set("Level", lvl) && c.run(session.edit_tool)) inst = c.result<void*>();
    }
    if (!inst) return;
    Guid guid{};
    { Call g("/Script/FortniteGame.FortItem.GetItemGuid"); if (g.ok() && g.run(inst)) guid = g.result<Guid>(); }
    Call eq("/Script/FortniteGame.FortPawn.EquipWeaponDefinition");
    if (!eq.ok()) return;
    bool bd = false; Guid tg{};
    eq.set("WeaponData", session.edit_tool); eq.set("ItemEntryGuid", guid);
    eq.set("TrackerGuid", tg); eq.set("bDisableEquipAnimation", bd);
    if (!eq.run(session.pawn)) return;
    void* editActor = eq.result<void*>();
    if (!editActor) return;
    void* p = GetProp("/Script/FortniteGame.FortWeap_EditingTool.EditActor");   // [F10]
    if (p) Write<void*>(GetField(editActor, p), 0, piece);
    else Write<void*>(editActor, profile::EditingTool_EditActor, piece);
    Call r("/Script/FortniteGame.FortWeap_EditingTool.OnRep_EditActor");        // [F10]
    if (r.ok()) r.run(editActor);
    LOG("[Edit] editing tool bound to %s", ObjName(piece).c_str());
}

void Athena::OnFinishEditActor(void* actor, void* newClass, int rotIter, bool mirrored) {
    if (!actor) return;
    SetBool(actor, "/Script/FortniteGame.BuildingActor.bPlayDestructionEffects", false);
    Vector zero{0.0, 0.0, 0.0};
    Call s("/Script/Engine.Actor.SetActorScale3D");
    if (s.ok() && s.set("NewScale3D", zero)) s.run(actor);
    Call d("/Script/Engine.Actor.K2_DestroyActor");
    if (d.ok()) d.run(actor);
    if (!newClass) return;
    Transform t{};
    void* edited = SpawnActor(session.world, newClass, session.controller, t);
    if (!edited) return;
    Call m("/Script/FortniteGame.BuildingSMActor.SetMirrored");                 // [F03]
    if (m.ok()) { bool mm = mirrored != 0; if (m.set("bIsMirrored", mm)) m.run(edited); }
    Call init("/Script/FortniteGame.BuildingActor.InitializeKismetSpawnedBuildingActor");
    bool bUse = true;
    if (init.ok()) {
        init.set("BuildingOwner", edited); init.set("SpawningController", session.controller);
        init.set("bUsePlayerBuildAnimations", bUse); init.run(edited);
    }
    LOG("[Edit] replaced with %s", ObjName(newClass).c_str());
}

void Athena::SpawnBuildPreviews() {
    void* previewCls = FindObj("BuildingPlayerPrimitivePreview");
    if (!previewCls || !session.controller) return;
    auto build = [&](const char* meshPath) -> void* {
        Transform t{};
        void* actor = SpawnActor(session.world, previewCls, session.controller, t);
        if (!actor) return nullptr;
        void* mesh = LoadAsset(meshPath, nullptr);
        void* mc = nullptr;
        Call g("/Script/FortniteGame.BuildingActor.GetBuildingMeshComponent");
        if (g.ok() && g.run(actor)) mc = g.result<void*>();
        if (mc && mesh) { Call s("/Script/Engine.StaticMeshComponent.SetStaticMesh"); if (s.ok() && s.set("NewMesh", mesh)) s.run(mc); }
        Call hide("/Script/Engine.Actor.SetActorHiddenInGame");
        bool h = true;
        if (hide.ok() && hide.set("bNewHidden", h)) hide.run(actor);
        return actor;
    };
    session.wall   = build("/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Solid.PBWA_W1_Solid");
    session.floor  = build("/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_Floor.PBWA_W1_Floor");
    session.stairs = build("/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_StairW.PBWA_W1_StairW");
    session.roof   = build("/Game/Building/ActorBlueprints/Player/Wood/L1/PBWA_W1_RoofC.PBWA_W1_RoofC");
}

// ============================================================================
// SAFE ZONE / MISC
// ============================================================================
void Athena::SpawnSafeZoneIndicator() {
    if (session.safezone_spawned || !session.state || !session.world) return;
    void* existing = GetObjField(session.state, "/Script/FortniteGame.FortGameStateAthena.SafeZoneIndicator");
    if (existing && !IsBadPtr(existing)) { session.safezone_spawned = true; return; }
    void* cls = FindObj("/Script/FortniteGame.FortSafeZoneIndicator");
    if (!cls) return;
    Transform t{};
    void* indicator = SpawnActor(session.world, cls, session.state, t);
    if (!indicator) return;
    void* prop = GetProp("/Script/FortniteGame.FortGameStateAthena.SafeZoneIndicator");
    if (prop) Write<void*>(GetField(session.state, prop), 0, indicator);
    Call n("/Script/FortniteGame.FortGameStateAthena.OnRep_SafeZoneIndicator");
    if (n.ok()) n.run(session.state);
    session.safezone_spawned = true;
}

void Athena::FixBuildingFoundations() {
    void* cls = FindObj("/Script/FortniteGame.BuildingFoundation");
    if (!cls || !session.world) return;
    GObjects_t* go = GObjectArray();
    if (!go) return;
    void* p = GetProp("/Script/FortniteGame.BuildingFoundation.DynamicFoundationType");
    if (!p) return;
    int propOff = Read<int>(p, profile::PropOffset);
    if (propOff < 0 || propOff > 0x20000) return;
    int fixed = 0;
    for (int32 i = 0; i < go->NumElements; ++i) {
        void* o = go->GetById((uint32)i);
        if (!o || IsBadPtr(o)) continue;
        if (Read<void*>(o, profile::ObjectClass) != cls) continue;
        Write<char>((char*)o + propOff, 0, 0);
        ++fixed;
    }
    LOG("[Foundations] Static type applied to %d foundations", fixed);
}

void Athena::TeleportToSpawnIsland() {
    if (!session.pawn) return;
    void* cls = FindObj("/Script/FortniteGame.FortPlayerStartWarmup");
    if (!cls) return;
    GObjects_t* go = GObjectArray();
    if (!go) return;
    std::vector<void*> starts;
    for (int32 i = 0; i < go->NumElements; ++i) {
        void* o = go->GetById((uint32)i);
        if (o && !IsBadPtr(o)) {
            void* k = Read<void*>(o, profile::ObjectClass);
            if (k == cls) starts.push_back(o);
        }
    }
    if (starts.empty()) return;
    void* start = starts[rand() % starts.size()];
    Call c("/Script/Engine.Actor.K2_GetActorLocation");
    Vector loc{0.0, 0.0, 1000.0};
    if (c.ok() && c.run(start)) loc = c.result<Vector>();
    Rotator rot{0.0, 0.0, 0.0};
    Call sl("/Script/Engine.Actor.K2_SetActorLocationAndRotation");
    if (sl.ok()) { bool bSweep = false, bTeleport = true; sl.set("NewLocation", loc); sl.set("NewRotation", rot); sl.set("bSweep", bSweep); sl.set("bTeleport", bTeleport); sl.run(session.pawn); }
}

void Athena::RemoveNetDebugUI() {
    void* wb = FindObj("/Script/UMG.Default__WidgetBlueprintLibrary");
    if (!wb) return;
    void* cls = FindObj("/Game/Athena/HUD/NetDebugUI.NetDebugUI_C");
    if (!cls) return;
    Call c("/Script/UMG.WidgetBlueprintLibrary.GetAllWidgetsOfClass");
    if (!c.ok() || !c.set("WorldContextObject", session.world) || !c.set("WidgetClass", cls)) return;
    if (!c.run(wb)) return;
    auto arr = c.result<Array>();
    for (int i = 0; i < arr.Count; ++i) {
        void* w = Read<void*>(arr.Data, (size_t)i * 8);
        if (w) { Call r("/Script/UMG.UserWidget.RemoveFromViewport"); if (r.ok()) r.run(w); }
    }
}

// ============================================================================
// EMOTES — [F09] 21.30 signature: BodyType/Gender/PawnContext
// ============================================================================
void Athena::PlayEmoteItem(void* item) {
    if (!item || !session.pawn) return;
    void* mesh = GetObjField(session.pawn, "/Script/Engine.Character.Mesh");
    if (!mesh) return;
    Call anim("/Script/Engine.SkeletalMeshComponent.GetAnimInstance");
    if (!anim.ok() || !anim.run(mesh)) return;
    void* ai = anim.result<void*>();
    if (!ai) return;
    Call ref("/Script/FortniteGame.FortMontageItemDefinitionBase.GetAnimationHardReference");
    void* montage = nullptr;
    if (ref.ok()) {
        // EEFortCustomBodyType / EEFortCustomGender are uint8 enums; try the
        // female/male combinations and fall back gracefully.
        struct Combo { uint8_t body; uint8_t gender; };
        const Combo combos[] = {{1, 1}, {0, 0}, {1, 0}, {0, 1}};
        for (auto& cb : combos) {
            if (ref.set("BodyType", cb.body) && ref.set("Gender", cb.gender) &&
                ref.set("PawnContext", session.pawn) && ref.run(item)) {
                montage = ref.result<void*>();
                if (montage) break;
            }
        }
    }
    if (!montage) { LOG("[Emote] no montage hard reference resolved"); return; }
    Call play("/Script/Engine.AnimInstance.Montage_Play");
    if (play.ok()) {
        play.set("MontageToPlay", montage);
        float rate = 1.0f; play.set("InPlayRate", rate);
        float start = 0.0f; play.set("InTimeToStartMontageAt", start);
        bool bStop = true; play.set("bStopAllMontages", bStop);
        play.run(ai);
        LOG("[Emote] playing %s", ObjName(montage).c_str());
    }
}

// [F11] ExitForceBehavior is EETryExitVehicleBehavior (uint8).
void Athena::OnExitVehicle() {
    if (!session.pawn) return;
    Call v("/Script/FortniteGame.FortPlayerPawn.ServerOnExitVehicle");
    if (v.ok()) {
        uint8_t ExitForce = 0; bool bDestroy = false;
        if (v.set("ExitForceBehavior", ExitForce) && v.set("bDestroyVehicleWhenForced", bDestroy)) v.run(session.pawn);
    }
}

void Athena::CheatScript(const char* script) {
    if (!script || !session.pawn) return;
    std::string s = script;
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    LOG("CheatScript: %s", s.c_str());
}

// ============================================================================
// LOBBY / CHEAT COMMANDS — [F24]
// ============================================================================
static void* g_depart_world = nullptr;

void Athena::ReturnToLobby(void* world) {
    // Travel after WorldTick, never from inside a controller return-menu RPC
    // (Era core return_to_lobby ported).
    void* controller = session.controller;
    g_depart_world = world;
    session = Session{};
    auto text = ToWide("open Frontend");
    FStringNative command{(TCHAR*)text.data(), (int32)text.size() + 1, (int32)text.size() + 1};
    Call travel("/Script/Engine.KismetSystemLibrary.ExecuteConsoleCommand");
    bool dispatched = travel.ok() && travel.set("WorldContextObject", world) &&
                      travel.set("Command", command) && travel.set("SpecificPlayer", controller) &&
                      travel.run(FindObj("/Script/Engine.Default__KismetSystemLibrary"));
    LOG("Lobby travel dispatched=%d; old session cleared", (int)dispatched);
    if (!dispatched) { g_depart_world = nullptr; }
}

void Athena::TeleportToReticle() {
    if (!session.controller) return;
    Vector target{};
    if (!GetFieldValue<Vector>(session.controller, "/Script/FortniteGame.FortPlayerController.LocationUnderReticle", target)) {
        LOG("Teleport: no reticle location"); return;
    }
    Call teleport("/Script/FortniteGame.FortPlayerController.ServerTeleportToReticle");
    if (teleport.ok() && teleport.set("TeleportLocation", target) && teleport.run(session.controller))
        LOG("Teleport-to-reticle dispatched to %.0f %.0f %.0f", target.X, target.Y, target.Z);
}

// fly/walk/ghost/god — Era core exec handlers ported.
void Athena::ApplyCheats(const std::string& cmd) {
    if (!session.pawn || !session.controller) return;
    if (cmd == "god") {
        bool current = GetBool(session.pawn, "/Script/Engine.Actor.bCanBeDamaged");
        SetBool(session.pawn, "/Script/Engine.Actor.bCanBeDamaged", !current);
        LOG("God mode=%d", (int)current);
        return;
    }
    void* movement = GetObjField(session.pawn, "/Script/Engine.Character.CharacterMovement");
    Call mode("/Script/Engine.CharacterMovementComponent.SetMovementMode");
    uint8_t movement_mode = cmd == "walk" ? 1 : 5, custom = 0;
    if (movement && mode.ok() && mode.set("NewMovementMode", movement_mode) && mode.set("NewCustomMode", custom))
        mode.run(movement);
    Call collision("/Script/Engine.Actor.SetActorEnableCollision");
    bool enabled = cmd != "ghost";
    if (collision.ok() && collision.set("bNewActorEnableCollision", enabled)) collision.run(session.pawn);
    LOG("Local movement cheat %s", cmd.c_str());
}

// ============================================================================
// HOOKS
// ============================================================================
static void* g_interactionClass = nullptr;
static void* g_hudContextClass = nullptr;
static void* g_aircraftClass = nullptr;

// [F25] Prologue verification before hooking (Era core pattern) — refuses to
// patch when the bytes at an RVA do not match the profiled build (protects
// against wrong offsets or another mod's detour).
static bool PrologueMatch(const char* label, uintptr_t rva, const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        uint32_t actual = Read<uint32_t>((const void*)(g_base + rva), 4 * i);
        if (actual != words[i]) {
            LOG("PROLOGUE MISMATCH %s RVA=%llx word=%zu expected=%08x actual=%08x",
                label, (unsigned long long)rva, i, words[i], actual);
            return false;
        }
    }
    LOG("Prologue verified %s RVA=%llx", label, (unsigned long long)rva);
    return true;
}

// [F26] ProcessEvent prologue varies across builds in its first instruction:
//   libUE4.so  (CL-8936006, reference): STR X28,[SP,#-0x60]!  = 0xf81a0ffc
//   libUnreal.so (CL-21088273 / 21.30): STP X28,X27,[SP,#-0x60]! = 0xa9ba6ffc
// Words 1..3 are identical in both builds (STP X26,X25 / X24,X23 / X22,X21).
// Accept either save-X28 form, then verify the shared tail.
static bool PrologueMatchPE(const char* label, uintptr_t rva) {
    static const uint32_t word0_variants[] = {0xa9ba6ffc, 0xf81a0ffc};
    static const uint32_t tail[]           = {0xa90167fa, 0xa9025ff8, 0xa90357f6};
    uint32_t actual0 = Read<uint32_t>((const void*)(g_base + rva), 0);
    bool ok0 = false;
    for (size_t i = 0; i < sizeof(word0_variants) / sizeof(word0_variants[0]); ++i)
        if (actual0 == word0_variants[i]) { ok0 = true; break; }
    if (!ok0) {
        LOG("PROLOGUE MISMATCH %s RVA=%llx word=0 expected=a9ba6ffc|f81a0ffc actual=%08x",
            label, (unsigned long long)rva, actual0);
        return false;
    }
    for (size_t i = 0; i < sizeof(tail) / sizeof(tail[0]); ++i) {
        uint32_t actual = Read<uint32_t>((const void*)(g_base + rva), 4 * (i + 1));
        if (actual != tail[i]) {
            LOG("PROLOGUE MISMATCH %s RVA=%llx word=%zu expected=%08x actual=%08x",
                label, (unsigned long long)rva, i + 1, tail[i], actual);
            return false;
        }
    }
    LOG("Prologue verified %s RVA=%llx word0=%08x", label, (unsigned long long)rva, actual0);
    return true;
}

static void pe_hook(void* obj, void* func, void* params) {
    if (!orig_PE) return;
    if (!g_internal && func && g_gameTid.load() == (int)gettid()) {
        if (session.lobby_requested) { orig_PE(obj, func, params); return; }

        std::string fn = ObjName(func);
        bool isPC   = (obj == session.controller);
        bool isPawn = (obj == session.pawn);
        void* currentWeapon = nullptr;
        if (session.pawn) currentWeapon = Read<void*>(session.pawn, profile::FortPawn_CurrentWeapon);
        bool isWeapon = (obj == currentWeapon && obj);

        // [F05] JumpFromHudStart comes through UAthenaHUDContext (touch HUD).
        if (!g_hudContextClass) g_hudContextClass = FindObj("/Script/FortniteGame.AthenaHUDContext");
        if (g_hudContextClass && obj && fn == "JumpFromHudStart") {
            void* cls = Read<void*>(obj, profile::ObjectClass);
            if (SubclassOfFast(cls, g_hudContextClass)) { Athena::HandleJumpFromHudStart(); return orig_PE(obj, func, params); }
        }
        // Direct receiver match (original behaviour retained for safety).
        if (isPC && fn == "JumpFromHudStart") { Athena::HandleJumpFromHudStart(); return orig_PE(obj, func, params); }

        // [F24] Lobby return interception (standalone: travel locally).
        if (isPC && (fn == "ReturnToMainMenu" || fn == "ServerReturnToMainMenu" ||
                     fn == "ClientReturnToMainMenu" || fn == "ClientReturnToMainMenuWithTextReason")) {
            session.lobby_requested = true;
            LOG("Lobby return queued: %s", fn.c_str());
            return orig_PE(obj, func, params);
        }

        // [F04] Aircraft jump RPC arrives on the aircraft component.
        if (!g_aircraftClass) g_aircraftClass = FindObj("/Script/FortniteGame.FortControllerComponent_Aircraft");
        if (g_aircraftClass && obj && fn == "ServerAttemptAircraftJump") {
            void* cls = Read<void*>(obj, profile::ObjectClass);
            if (SubclassOfFast(cls, g_aircraftClass)) { Athena::HandleJumpFromHudStart(); return orig_PE(obj, func, params); }
        }

        if (!g_interactionClass) g_interactionClass = FindObj("/Script/FortniteGame.FortControllerComponent_Interaction");
        bool isInteraction = false;
        if (g_interactionClass && obj) {
            void* cls = Read<void*>(obj, profile::ObjectClass);
            isInteraction = SubclassOfFast(cls, g_interactionClass);
        }
        if (isPC || isPawn || isWeapon || isInteraction) {
            Guard g;
            if (fn == "ServerExecuteInventoryItem") {
                if (Read<uint16_t>(func, profile::FunctionParamSize) >= (int)sizeof(Guid))
                    Athena::OnServerExecuteInventoryItem(Read<Guid>(params));
            }
            else if (fn == "ServerExecuteInventoryWeapon") Athena::OnServerExecuteInventoryWeapon(Read<void*>(params));
            // [F06] ServerHandlePickup(PickUp, InFlyTime, InStartDirection,
            // bPlayPickupSound) and ServerHandlePickupInfo(PickUp, Info) —
            // the pickup pointer is the first parameter in both.
            else if ((fn == "ServerHandlePickupInfo" || fn == "ServerHandlePickup") && isPawn) {
                void* arg = incoming(func, params, "PickUp", 8);
                if (!arg) arg = incoming(func, params, "Pickup", 8);
                if (!arg) arg = params;   // PickUp is the first parameter
                if (arg) Athena::ServerHandlePickup(Read<void*>(arg));
            }
            else if (fn == "ServerAttemptInteract" && params && isInteraction) {
                void* target = Read<void*>(params, 0);
                if (target && !IsBadPtr(target)) {
                    void* bcCls   = FindObj("/Script/FortniteGame.BuildingContainer");
                    void* wallCls = FindObj("/Script/FortniteGame.BuildingWall");
                    void* actorCls = FindObj("/Script/FortniteGame.BuildingActor");
                    void* targetCls = Read<void*>(target, profile::ObjectClass);
                    LOG("[Interact] target=%p class=%s", target, ObjName(targetCls).c_str());
                    if (bcCls && SubclassOfFast(targetCls, bcCls)) Athena::Loot(target);
                    else if (wallCls && SubclassOfFast(targetCls, wallCls)) Athena::HandleDoor(target);
                    else if (actorCls && SubclassOfFast(targetCls, actorCls)) {
                        // [F16] generic building interaction via BP override.
                        Call interact("/Script/FortniteGame.BuildingActor.BlueprintOnInteract", target);
                        if (interact.ok() && interact.set("InteractingPawn", session.pawn)) interact.run(target);
                    }
                }
            }
            else if (fn == "ServerAttemptAircraftJump" && !session.aircraft_jumped) Athena::HandleJumpFromHudStart();
            else if (fn == "ServerCreateBuildingActor" && params && isPC) Athena::OnServerCreateBuildingActor(params);
            // [F21] Local preview build for any ServerCreateBuilding variant.
            else if (fn.rfind("ServerCreateBuilding", 0) == 0 && params && isPC) { if (!Athena::LocalBuild()) Athena::OnServerCreateBuildingActor(params); }
            else if (fn == "ServerBeginEditingBuildingActor" && params) Athena::OnBeginEditActor(Read<void*>(params, 0));
            else if (fn == "ServerEditBuildingActor" && params) {
                // [F08] Named extraction: BuildingActorToEdit(ptr),
                // NewBuildingClass(ptr), RotationIterations(u8), bMirrored(bool).
                void* ba = incoming(func, params, "BuildingActorToEdit", 8);
                void* nc = incoming(func, params, "NewBuildingClass", 8);
                void* ri = incoming(func, params, "RotationIterations", 1);
                void* mi = incoming(func, params, "bMirrored", 1);
                if (ba && nc && ri && mi)
                    Athena::OnFinishEditActor(Read<void*>(ba), Read<void*>(nc), Read<uint8_t>(ri), Read<uint8_t>(mi) != 0);
                else {
                    struct P { void* BA; void* NC; uint8_t RI; uint8_t M; } *p = (P*)params;
                    Athena::OnFinishEditActor(p->BA, p->NC, p->RI, p->M != 0);
                }
            }
            else if (fn == "ServerPlayEmoteItem" && isPC) Athena::PlayEmoteItem(Read<void*>(params));
            else if (fn == "ServerAttemptExitVehicle") Athena::OnExitVehicle();
            // [F24] ServerAttemptInventoryDrop(ItemGuid, Count, bTrash).
            else if (fn == "ServerAttemptInventoryDrop" && params) {
                Guid gg = Read<Guid>(params);
                int cnt = Read<int>(params, sizeof(Guid));
                Athena::DropInventoryItem(gg, cnt);
            }
        }
    }
    orig_PE(obj, func, params);
}

static const char* PhaseName(int phase) {
    switch (phase) {
        case 0: return "None"; case 1: return "Setup"; case 2: return "Warmup";
        case 3: return "Aircraft"; case 4: return "SafeZones"; case 5: return "EndGame";
        case 6: return "Count"; default: return "Unknown";
    }
}

static void tick_hook(void* world, int type, float delta) {
    if (orig_Tick) orig_Tick(world, type, delta);
    if (g_internal || !world) return;
    int expected = 0;
    if (g_gameTid.compare_exchange_strong(expected, (int)gettid())) LOG("WorldTick first: tid=%d", (int)gettid());
    if (g_gameTid.load() != (int)gettid()) return;
    // [F27] The old [F14] guard read libUnreal.so CODE bytes at
    // profile::IsSavingPackage (0x0760A59C is an EXEC RVA in 21.30 — it was
    // defined-but-never-used in the original hook.cpp). First byte there is
    // 0xFD, so the check was always true and tick_hook returned before ANY
    // world detection ran (no controller binding, no spawn). game.cpp's
    // reference used a real .bss bool there, but this build has no known data
    // address for it — both probes are dropped (the original hook.cpp never
    // used them either). Re-entrancy is still guarded below.
    Guard g;

    // [F27] One-shot diagnostics: proves world pointer + name resolution work
    // on-device (appears once, right after the first game-thread tick).
    static bool diag_logged = false;
    if (!diag_logged) {
        diag_logged = true;
        void* wcls = Read<void*>(world, profile::ObjectClass);
        LOG("DIAG world=%p class=%p ('%s') name='%s'",
            world, wcls, ObjName(wcls).c_str(), ObjName(world).c_str());
    }

    // [F24] Lobby travel was dispatched inside ReturnToLobby; while it is in
    // flight, ignore the departing world so the session does not resurrect.
    if (g_depart_world) {
        if (world == g_depart_world) return;
        g_depart_world = nullptr;
    }
    if (session.lobby_requested && session.world == world) { Athena::ReturnToLobby(world); return; }

    std::string wn = ObjName(world);
    static std::string lastWorld;
    if (wn != lastWorld) { LOG(">>> WorldTick world: '%s'", wn.c_str()); lastWorld = wn; }
    if (wn.find("Frontend") != std::string::npos) {
        if (session.attempted && !session.finalized && session.finalize_at > 0) {
            double now = Monotonic();
            if (now >= session.finalize_at) Athena::FinalizePlayer();
        }
        return;
    }
    if (wn.find("Artemis_Terrain") == std::string::npos && wn.find("Athena_Terrain") == std::string::npos) return;
    int index = Read<int>(world, 0xC);
    if (world != session.world || index != session.world_index) {
        session = Session{};
        session.world = world; session.world_index = index;
        session.observed_at = Monotonic();
        LOG(">>> Terrain detected: %s", wn.c_str());
    }
    double now = Monotonic();
    // [F24] GamePhase tracking for diagnostics (reflection cached).
    if (session.state) {
        static void* phaseProp = nullptr;
        static bool phaseResolved = false;
        if (!phaseResolved) { phaseProp = GetProp("/Script/FortniteGame.FortGameStateAthena.GamePhase"); phaseResolved = true; }
        if (phaseProp && Read<int>(phaseProp, profile::PropElementSize) == 1) {
            uint8_t phase = Read<uint8_t>(GetField(session.state, phaseProp));
            if ((int)phase != session.last_phase) {
                LOG("GamePhase observed=%u (%s) elapsed=%.1f", phase, PhaseName(phase), now - session.observed_at);
                session.last_phase = (int)phase;
            }
        }
    }
    if (now - session.observed_at < 5.0) return;
    Athena::Tick();
}

static bool exec_hook(void* engine, void* world, const char16_t* command, void* output) {
    if (!orig_Exec) return false;
    std::string cmd = Narrow(command, 256);
    std::string normalized = cmd;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    while (!normalized.empty() && (normalized.back() == ' ' || normalized.back() == '\t')) normalized.pop_back();
    if (!cmd.empty()) LOG("Exec: %s", cmd.c_str());
    bool onGameThread = (g_gameTid.load() == (int)gettid());

    if (normalized == "open athena_terrain" || normalized.rfind("open athena_terrain", 0) == 0) {
        LOG("Intercepting Athena travel");
        session = Session{};
        session.attempted = false;
        return orig_Exec(engine, world, u"open Athena_Terrain", output);
    }
    if (onGameThread && session.world && (normalized == "era.lobby" || normalized == "athena.lobby")) {
        session.lobby_requested = true;
        return true;
    }
    if (onGameThread && session.world && normalized == "teleport") {
        if (session.finalized) Athena::TeleportToReticle();
        return true;
    }
    // [F24] Movement cheats (Era core parity).
    if (onGameThread && session.world && session.finalized &&
        (normalized == "fly" || normalized == "walk" || normalized == "ghost" || normalized == "god")) {
        Guard g;
        Athena::ApplyCheats(normalized);
        return true;
    }
    // [F24] Diagnostics/cheat console (era.* mirror of the Era core athena.*).
    if (onGameThread && normalized.rfind("era.", 0) == 0) {
        Guard g;
        if (normalized == "era.status") {
            LOG("STATUS world=%p controller=%p pawn=%p inv=%p qb=%p ready=%d phase=%d",
                session.world, session.controller, session.pawn, session.inventory,
                session.quickbars, (int)session.finalized, session.last_phase);
        } else if (normalized == "era.inventory" && session.inventory) {
            for (size_t i = 0; i < session.slots.size(); ++i)
                LOG("SLOT %zu %s", i, ObjName(ItemDefinitionOf(session.slots[i])).c_str());
            for (size_t i = 0; i < session.secondary_slots.size(); ++i)
                LOG("BUILD %zu %s", i, ObjName(ItemDefinitionOf(session.secondary_slots[i])).c_str());
        } else if (normalized.rfind("era.equip ", 0) == 0) {
            int slot = -1;
            if (sscanf(normalized.c_str(), "era.equip %d", &slot) == 1 && slot >= 0 && slot < 6)
                Athena::EquipSlot(slot);
        } else if (normalized.rfind("era.give ", 0) == 0 && session.inventory_ready) {
            char asset[512]{}; int count = 1, slot = -1;
            if (sscanf(cmd.c_str(), "%*s %511s %d %d", asset, &count, &slot) >= 1) {
                void* def = asset[0] == '/' ? LoadAsset(asset) : ByShortName(asset);
                if (!def) LOG("Give: asset not found: %s", asset);
                else LOG("Give result=%d", (int)Athena::GiveItem(def, count, slot, slot >= 0 && slot < 6 ? 0 : (uint8_t)-1, false));
            }
        } else LOG("Commands: era.status | era.inventory | era.equip 0..5 | era.give Name [count] [slot] | fly | walk | ghost | god | teleport | era.lobby");
        return true;
    }
    return orig_Exec(engine, world, command, output);
}

static void InstallHooks() {
    LOG("=== InstallHooks ===");
    // [F25/F26] Verify every hooked RVA belongs to this build before patching.
    // Each hook installs independently — one prologue mismatch must never
    // disable the remaining hooks (the old all-or-nothing return left the
    // game with zero hooks: no controller binding, no respawn).
    static const uint32_t tick_words[]  = {0xd10503ff, 0x6d0d23e9, 0xa90e6ffc, 0xa90f67fa};
    static const uint32_t exec_words[]  = {0xd10183ff, 0xa9025ff8, 0xa90357f6, 0xa9044ff4};
    static const uint32_t qb_words[]    = {0xa9bd57f6, 0xa9014ff4, 0xa9027bfd, 0x910083fd};
    bool pe_valid   = PrologueMatchPE("ProcessEvent",    profile::ProcessEvent);
    bool tick_valid = PrologueMatch("WorldTick",       profile::WorldTick,       tick_words, 4);
    bool exec_valid = PrologueMatch("EngineExec",      profile::EngineExec,      exec_words, 4);
    bool qb_valid   = PrologueMatch("AthenaQuickBars", profile::InitializeAthenaQuickBars, qb_words, 4);

    int installed = 0, failed = 0;
    if (pe_valid) {
        if (DobbyHook((void*)(g_base + profile::ProcessEvent), (void*)pe_hook, (void**)&orig_PE) == 0 && orig_PE) { LOG("ProcessEvent hooked"); ++installed; }
        else { LOG("ProcessEvent FAILED"); ++failed; }
    } else { LOG("ProcessEvent skipped (prologue)"); ++failed; }
    if (tick_valid) {
        if (DobbyHook((void*)(g_base + profile::WorldTick), (void*)tick_hook, (void**)&orig_Tick) == 0 && orig_Tick) { LOG("WorldTick hooked"); ++installed; }
        else { LOG("WorldTick FAILED"); ++failed; }
    } else { LOG("WorldTick skipped (prologue)"); ++failed; }
    if (exec_valid) {
        if (DobbyHook((void*)(g_base + profile::EngineExec), (void*)exec_hook, (void**)&orig_Exec) == 0 && orig_Exec) { LOG("EngineExec hooked"); ++installed; }
        else { LOG("EngineExec FAILED"); ++failed; }
    } else { LOG("EngineExec skipped (prologue)"); ++failed; }
    if (!qb_valid) LOG("[QB] initializer prologue mismatch — native init disabled, manual path only");
    LOG("InstallHooks done: installed=%d failed=%d", installed, failed);
}

static int InspectCb(dl_phdr_info* info, size_t, void*) {
    const char* name = strrchr(info->dlpi_name, '/');
    name = name ? name + 1 : info->dlpi_name;
    if (strcmp(name, "libUnreal.so")) return 0;
    g_base = info->dlpi_addr;
    g_status.store(1);
    LOG("libUnreal.so base=%p", (void*)g_base);
    return 1;
}

static void* Worker(void*) {
    LOG("=== EraCH3 (fixed) starting — target CL-21088273 build 5b371871c152651ff2b038c01c9504c1a82de1f8 ===");
    LOG("sizeof(Vector)=%zu sizeof(Rotator)=%zu sizeof(Transform)=%zu", sizeof(Vector), sizeof(Rotator), sizeof(Transform));
    for (int i = 0; i < 600 && g_status.load() == 0; ++i) {
        dl_iterate_phdr(InspectCb, nullptr);
        if (g_status.load() == 0) usleep(100000);
    }
    if (g_status.load() != 1) { LOG("Timeout"); return nullptr; }
    usleep(300000);
    InstallHooks();
    LOG("=== Ready ===");
    return nullptr;
}

extern "C" __attribute__((constructor))
void LibMain() {
    InitLog();
    pthread_t t;
    if (pthread_create(&t, nullptr, Worker, nullptr) == 0) pthread_detach(t);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM*, void*) { return JNI_VERSION_1_6; }
