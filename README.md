# ⚡ مركز عمليات الوكلاء — Agent Ops Center v2

مركز تحكم للوكلاء على **GitHub Actions + Cloudflared**، مرتبط مباشرة بـ **abdallh-rgx/idk**.

## القواعد (معروضة أعلى الموقع أيضاً)
- **الكل يتكلم عربي** في المحادثات
- كل رسالة / Commit / أمر يحتاج **موافقة الجميع**
- كل Commit يظهر في تبويب **Commits**
- ضع **GH_PAT** في ⚙️ الإعدادات → تحكم كامل من الموقع (بدل كتابته في الشات)

## التشغيل
Actions → **Host Ops Center** → Run workflow → بعد ~3 دقائق:
1. URL في **ملخص الـ run**
2. كومنت تلقائي في issue "🌐 Tunnel URL" بمستودع idk
3. Artifact `tunnel-url`

## الأقسام
💬 محادثة (قنوات: general/interaction/quickbars/loot/vehicles/building/inventory/crash-debug/re-tools/builds) • ✅ موافقات صارمة • 📜 كل الـ Commits • 🖥️ طرفية على الـ runner • 📁 ملفات • 🔀 Git+Actions للـ idk • ⬇️ MediaFire • 👥 وكلاء

## أدوات RE (أهم شيء — إيجاد الدوال)
```bash
# إيجاد دوال native عبر مراجع السلاسل (يعمل بدون IDA):
python3 scripts/find_native.py ~/fortnite-src/libUnreal.so SpawnLoot SetPickupItems ServerOnAttemptInteract
# اكتشاف أسماء (Klombo = ButterCake):
python3 scripts/find_native.py ~/fortnite-src/libUnreal.so --strings ButterCake
# فك دالة:
python3 scripts/disasm.py ~/fortnite-src/libUnreal.so 0x05955e38 0x80
```
مراجع تحقق: ProcessEvent=0x7611b5c • SetIsDoorOpen=0x05955e38 • QuickBars=0x56607ec

## API للوكلاء
كل الطلبات بالهيدر `x-code`. أمثلة في تبويب 🔌 API بالموقع.

## الأهداف الناقصة (من تحليل التغطية)
Looting 33% (SpawnLoot/SetPickupItems/FinishedTargetSpline) • Vehicles 14% (EquipVehicleWeapon/OnPawnEnterSeat/ServerMove/SetFuel/Towhook/AttachedInfo) • Interaction 80% (ServerOnAttemptInteract/OnDamageServer) • Building 93% (OnDamageServer/OnAuthorityRandomUpgradeApplied) • Storm (SetIsInsideSafeZone/UpdateOutsideSafeZone)
