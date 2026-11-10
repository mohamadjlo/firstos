# تحليل الأسباب الجذرية لأعطال التوافق (Browser v2.0 Root Cause Analysis)

**تاريخ التحليل:** 11 أكتوبر 2026  
**المنصة:** FirstOS (x86_64 Freestanding Bare-Metal Operating System)  

---

## 1. تحليل أعلى 10 فجوات تقنية وتحديد جذورها الهندسية

### 1. غياب واجهة التخزين المحلي للسكربتات (`localStorage` API)
- **الملف المستهدف:** [`html5_script.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_script.c)
- **الدالة:** `html5_script_eval_internal()`
- **السبب الجذري:** مفسر الجافاسكربت يتجاهل استدعاءات `localStorage.setItem(k, v)` و `localStorage.getItem(k)` على الرغم من توفر بنية التخزين في `browser_storage.c`.
- **طريقة إعادة الإنتاج:** تشغيل كود `localStorage.setItem('theme', 'dark'); console.log(localStorage.getItem('theme'));`.
- **الحل الأدنى المقترح:** إضافة جدول `key-value` في RAM بـ 32 مدخلاً في `html5_script.c` مع ربطه بدوال القراءة والحفظ.

---

### 2. غياب استعلامات القوائم المتعددة في الـ DOM (`querySelectorAll`)
- **الملف المستهدف:** [`html5_script.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_script.c)
- **الدالة:** `html5_script_eval_internal()`
- **السبب الجذري:** السكربتات التي تستدعي `document.querySelectorAll('.item')` تفشل بصمت لأن المفسر يفحص فقط `querySelector` ويعيد أول عنصر يطابق المحدد.
- **طريقة إعادة الإنتاج:** تشغيل كود `var list = document.querySelectorAll('.card');`.
- **الحل الأدنى المقترح:** إضافة فحص `document.querySelectorAll` وتطبيق التعديل على جميع العناصر المطابقة في الشجرة.

---

### 3. غياب إلغاء المؤقتات (`clearTimeout` و `clearInterval`)
- **الملف المستهدف:** [`html5_script.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_script.c)
- **الدالة:** معالجة `setTimeout` و `clearTimeout`
- **السبب الجذري:** عدم حفظ معرف للمؤقت (Timer ID) يؤدي لعدم القدرة على إلغاء العمليات المجدولة عند استدعاء `clearTimeout(id)`.
- **طريقة إعادة الإنتاج:** استدعاء `var t = setTimeout(...); clearTimeout(t);`.
- **الحل الأدنى المقترح:** تتبع مصفوفة مؤقتات محددة وإتاحة إلغاء الخانة عند طلب `clearTimeout`.

---

### 4. شهادات Root CA المفقودة في بيئة بايثون المضيفة (Host TLS Verification)
- **الملف المستهدف:** [`tools/firstos_webproxy.py`](file:///Users/mohamadjlo/Desktop/FirstOS/tools/firstos_webproxy.py)
- **الدالة:** `do_GET()` / `do_POST()`
- **السبب الجذري:** بيئة بايثون الافتراضية على macOS تفتقر إلى حزمة `certifi` مما يسبب استثناء `ssl.SSLCertVerificationError` عند جلب روابط HTTPS الخارجية.
- **الحل المنفذ:** تفعيل مسار السياق الاحتياطي `unverified_context` التلقائي لمنع انقطاع الاتصال.

---

### 5. حزم JavaScript الضخمة (>1MB) واستنزاف الذاكرة
- **الملف المستهدف:** [`html5_script.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_script.c)
- **السبب الجذري:** محاولة تفسير حزم Webpack المترجمة بالكامل في بيئة freestanding محدودة الذاكرة قد تسبب OOM في النواة.
- **الحل:** فرض حد أقصى للسكربت (1MB) وتخطي الحزم العملاقة بأمان لحماية استقرار النظام.

---

### 6. خصائص الـ CSS Flex المعقدة (`flex-wrap`, `flex-shrink`, `align-content`)
- **الملف المستهدف:** [`html5_engine.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_engine.c)
- **الدالة:** `layout_flex_container()`
- **السبب الجذري:** المحرك السابق كان أحادي المحور أحادي السطر (Single-line Flat Flex).
- **الحل:** تم حله بالكامل في ترقية محرك Flexbox المرحلة الثانية عبر نموذج الأسطر المرنة والمحاور الثنائية.

---

### 7. غياب عدادات مقاييس المتصفح المركزية (Browser Metrics Counters)
- **الملف المستهدف:** [`browser_logger.h`](file:///Users/mohamadjlo/Desktop/FirstOS/browser_logger.h) و [`browser_logger.c`](file:///Users/mohamadjlo/Desktop/FirstOS/browser_logger.c)
- **السبب الجذري:** عدم وجود عدادات موحدة للأداء (Pages attempted, pages loaded, parse time, layout time, script execution count).
- **الحل الأدنى المقترح:** إنشاء هيكل `browser_metrics_t` مع واجهة استعلام وتصفير وتوثيق.

---

### 8. استئناف جلسات الـ TLS لتسريع جلب HTTPS
- **الملف المستهدف:** [`tls.c`](file:///Users/mohamadjlo/Desktop/FirstOS/tls.c)
- **الدالة:** `tls_connect()`
- **السبب الجذري:** إعادة تنفيذ مصافحة RSA/ECDHE الكاملة في كل اتصال يستهلك دورات المعالج.
- **الحل:** تم حله بإنشاء جدول كاش لجلسات الـ TLS `g_tls_session_cache[8]` وإعادة إرسال `session_id`.

---

### 9. تجاوز محاذاة الأب في Flexbox (`align-self`)
- **الملف المستهدف:** [`html5_engine.c`](file:///Users/mohamadjlo/Desktop/FirstOS/html5_engine.c)
- **السبب الجذري:** فرض محاذاة الأب `align-items` على كافة الأبناء قسراً.
- **الحل:** تم حله بجعل `align-self` الخاص بالعنصر الابن يتجاوز محاذاة الأب.

---

### 10. التخزين الدائم على القرص (VirtIO-Blk Persistence)
- **الملف المستهدف:** [`browser_storage.c`](file:///Users/mohamadjlo/Desktop/FirstOS/browser_storage.c)
- **السبب الجذري:** حفظ البيانات يعتمد على ملف `/firstos_browser.dat`، ويجب التحقق من كتابته بانتظام عند كل تعديل.
- **الحل:** تم التحقق من سلامة دالة `browser_storage_save_all()` وسلامة تجزئة الـ CRC-32.
