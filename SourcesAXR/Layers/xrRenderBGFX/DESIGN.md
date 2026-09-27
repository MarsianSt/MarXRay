# Split-HDR (low/high) + 校准亮度 — 设计文档

Только анализ/дизайн. Кода нет. Ссылки на строки — по `MarXRay` @ `48cbfe9d9` (master) и
`game_unpacked/shaders/r3/*` (распакованный AXR).

---

## 0. TL;DR (выводы, которые всё определяют)

1. **AXR r3/R4 — это двухтаргетная 8-битная кодировка, а не второй HDR-буфер.**
   `rt_Generic_0` = low (A8R8G8B8, tonemapped LDR), `rt_Generic_1` = high (A8R8G8B8, `rgb/def_hdr`,
   `def_hdr = 9.0`). Оба — `D3DFMT_A8R8G8B8` (`r4_rendertarget.cpp:483,490`), то есть high
   существует ради **9× запаса по точности/динамике в 8 битах**, а не ради «настоящего» HDR.
2. **high пишут 3 шейдера, читает ровно один — bloom bright-pass.**
   Пишут: `sky2.ps:19,49-49,60,66` (небо+облака), `combine_1.ps:73,213-215` (свет+туман),
   `combine_volumetric.ps:33`. Читает: `blender_bloom_build.cpp:18` → `s_image = r2_RT_generic1`.
   Больше **никто** high не читает: grep по `game_unpacked/shaders/r3` даёт только 4 писателя
   `SV_Target1` и ни одного `Texture2DArray`/второго slice.
3. **После combine_1 `rt_Generic_1` переиспользуется как distortion-mask** (очищается и
   заполняется distort-объектами, `r4_rendertarget_phase_combine.cpp:419-435`). Значит
   «жизненный цикл high» = от combine_1 до начала `phase_bloom` (`:406`). Всё, что после
   bloom, high уже не имеет.
4. **Наш текущий tm_scale занижен в ~3.6× относительно AXR** — это реальный, измеримый
   дефект калибровки, а не «нет high-канала». Подробности в §3.2. Split-HDR сам по себе
   картинку не починит; чинить надо `hdrLuminance`.
5. **AXR-паритет high-канала визуально почти ничего не даёт** (см. §4.1) — цель T2 «структурная
   (pipeline совпадает)», а не «картинка ярче».

---

## 1. Полный путь high-канала в AXR (только существующие файлы)

### 1.1 Формат и таргеты

| Что | AXR | Файл:строка |
|---|---|---|
| `def_hdr` | `float(9.h)`, `def_hdr_clip = 0.75` | `r3/common_defines.h:11-12` |
| low RT | `rt_Generic_0`, `D3DFMT_A8R8G8B8` | `r4_rendertarget.cpp:483` |
| high RT | `rt_Generic_1`, `D3DFMT_A8R8G8B8` | `r4_rendertarget.cpp:490` |
| MSAA-версии | `rt_Generic_0_r` / `_1_r`, тот же формат, resolve → `DXGI_FORMAT_R8G8B8A8_UNORM` | `r4_rendertarget.cpp:504-505`, `r4_rendertarget_phase_combine.cpp:401-402` |
| Привязка | `u_setrt(rt_Generic_0, rt_Generic_1, 0, pBaseZB)` = RTV0 + **RTV1** (не MRT-массив) | `r4_rendertarget_phase_combine.cpp:145`, реализация `r4_rendertarget.cpp:51-87` (`set_RT(_1,0)`, `set_RT(_2,1)`) |
| Очистка | оба в `(0,0,0,0)`, комментарий `// low/hi RTs` | `r4_rendertarget_phase_combine.cpp:139-146` |
| Кто пишет только RTV0 | вода/`RenderLast` (`:358`), forward (`:378`) — `set_ColorWriteEnable()` RGB | `r4_rendertarget_phase_combine.cpp:358,378` |
| Кто переиспользует RTV1 | distort-mask, очистка `(127,127,0,127)` | `r4_rendertarget_phase_combine.cpp:419-435` |
| Bloom-таргеты | `rt_Bloom_1` / `rt_Bloom_2`, `D3DFMT_A8R8G8B8`, 256×256 | `r4_rendertarget.cpp:788-792` |
| LUM-таргеты | `rt_LUM_64` (64²), `rt_LUM_8` (8²), `rt_LUM_pool[i*2+0/1]` (1×1, ping-pong) | `r4_rendertarget_phase_luminance.cpp:33,63,95` |

### 1.2 Формула записи (общая для неба и геометрии)

`r3/common_functions.h:23-33`:
```
rgb  = SRGBToLinear(rgb);      // от undo гаммы, сделанной в combine_1.ps:188
rgb *= scale;                  // tm_scale из luminance-цепочки
rgb  = LinearTosRGB(rgb);      // round-trip по сути элиминируется
low  = tonemap_sRGB(rgb, fWhiteIntensity=11.2);   // contrast 0.7 → boost 1.42857 → reinhard → /(11.2/12.2)
high = rgb / def_hdr;                            // ← 8x/9x dynamic range
```
Небо, ветка без `ENCHANTED_SHADERS_ENABLED` (`r3/sky2.ps:52-61`): `o.low = sky*0.33`,
`o.high = o.low/def_hdr`; aurora добавляется в оба канала (`:65-66`).

### 1.3 Порядок в кадре (AXR `phase_combine`)

```
:143-145  clear generic0 + generic1, bind RTV0=generic0, RTV1=generic1
:166      RenderSky()      -> sky2.ps пишет low+high
:170      RenderClouds()   -> clouds.ps
:299-316  combine_1        -> читает G-buffer, считает свет, пишет low(RTV0)+high(RTV1)
:344      CopyResource generic0 -> generic_temp
:358      вода / RenderLast    -> только RTV0
:372      CopyResource generic0 -> generic0_temp
:378      forward rendering    -> только RTV0
:393      phase_combine_volumetric -> low+high
:401-402  MSAA resolve обоих
:406      phase_bloom()      <-- читает generic1 (=HIGH) в bright-pass
          :74    rt_Bloom_1 <- bright pass из generic1
          :131   phase_luminance()  (LUM-цепочка, вход = rt_Bloom_1)
          :136+  H-фильтр / V-фильтр по rt_Bloom_1/2
:419-435  rt_Generic_1 очищается и становится distortion-mask   (high больше не нужен)
:683      phase_flares()
:684      present
```
Отдельно: LUM-цепочка в AXR **задержана на 1 кадр** — `combine_1` биндит `r2_RT_luminance_cur`
(`blender_combine.cpp:36`), а `t_LUM_src`/`t_LUM_dest` свапаются только в конце `phase_combine`
(`r4_rendertarget_phase_combine.cpp:678`), т.е. `combine_1` читает результат прошлого кадра.

### 1.4 Bloom high-путь (то, что надо воспроизвести)

`r3/bloom_build.ps:41-46` (не-enchanted ветка, её и портировали):
```
avg = (s0+s1+s2+s3)/2
hi  = dot(avg,1) - b_params.x        // b_params.x = ps_r2_ls_bloom_threshold = 0.00001
out  = float4(avg, hi)               // rgb = high-домен (≈ low/9), a = luma(high) - thr
```
`blender_bloom_build.cpp:15-20` → `s_image = r2_RT_generic1` (HIGH!), blend `SRCALPHA/INVSRCALPHA`
на неочищенный `rt_Bloom_1` (`r4_rendertarget_phase_bloom.cpp:77`).

Потребление bloom в combine: `r3/combine_2_naa.ps:136` → `blend_soft(img, bloom.xyz*bloom.w)`
(= `combine_bloom`, `r3/common_functions.h:77-82`: `high.rgb *= high.a; blend_soft(low, high.rgb)`).
**Умножения на `def_hdr` в основном пути нет.** Оно есть только в distort-ветке:
`r3/combine_2_naa.ps:162-164` → `blurred = bloom*def_hdr; img = lerp(img, blurred, distort.z)`.
Т.е. автор в одном месте помнил про 9×-масштаб, в другом — нет.

### 1.5 Luminance high-путь

`r3/bloom_luminance_1.ps:6-10`:
```
luminance(tc) = dot(s_image.Sample(tc), LUMINANCE_VECTOR * def_hdr)   // LUM = (0.3,0.38,0.22)
```
`blender_luminance.cpp:16-19` → `s_image = r2_RT_bloom1` (256², **результат bright-pass, т.е. high-домен**).
Т.е. `*def_hdr` здесь — это обратное преобразование high→low-домен:
`result = 9·luma(high) = luma(sRGB_encode(linear·scale))`, т.е. **в sRGB-домене, а не в линейном**.

Дальше: `bloom_luminance_2.ps` 64²→8², `bloom_luminance_3.ps:16-79` 8²→1×1 + адаптация:
```
scale        = MiddleGray.x / (result*MiddleGray.y + MiddleGray.z)
scale_prev   = s_tonemap[...]                       // предыдущий кадр
rvalue       = lerp(scale_prev, scale, MiddleGray.w)
clamp(rvalue, 1/128, 20)                            // :77 — баг, результат clamp'а не присваивается (строка 79 возвращает rvalue)
```

---

## 2. Что требуется изменить у нас (проект, без кода)

### 2.1 Таргеты/форматы

| Наш ресурс | Сейчас | Нужно для split-HDR | Комментарий |
|---|---|---|---|
| `s_hdrColor` (attach 0) | `RGBA16F` full-res, хранит **albedo** до T1 | после T1: **low** (tonemapped LDR, 0..1) | low по смыслу = AXR `rt_Generic_0`; 16F вместо 8 бит — сознательное улучшение, к high-парителю не относится |
| `s_hdrHigh` (новый attach) | — | `RGBA16F` (или `RGBA8` для 1:1 с AXR) full-res | AXR = `A8R8G8B8`; 8 бит здесь **не нужны** — мы и так в 16F, split-HDR у нас = «второй буфер», а не «8-битная упаковка». Ожидаем: `high = tonemapped_linear_after_scale / 9.0` |
| `s_hdrPosition`, `s_hdrGbuf`, `s_hdrDepth` | без изменений | — | T1 владеет ими |
| `s_hdrFb` | 4 attachments | **5** (color, high, position, gbuf, depth) | `bgfxHDR.cpp:936-937` |
| `s_bloom1/2` | 256², `RGBA8` | без изменений | AXR тоже 8 бит (`r4_rendertarget.cpp:788-792`) |

bgfx пишет один SV_Target, поэтому high **нельзя** получить MRT-мультизаписью из того же PS, что
low. Варианты (в порядке предпочтения):
- **A (рекомендую):** fullscreen-пасc `high_build_ps.sc` сразу после T1-разрева:
  `high = texture2D(s_hdr, tc).rgb_low_linear / 9.0` — но low уже тонимпнут, теряем «до-tonemap»
  семантику. Значит T1-разрева должен писать **и** однозначно восстанавливаемое значение:
  проще — T1 писать в attach0 **low** и в attach1 **high** двумя инструкциями (см. B).
- **B (точный, дешёвый по Bandwidth):** разрешить T1-resolve PS писать `o.low` (attach0) и
  `o.high` (attach1) — это ровно `tonemap()` из §1.2, одна строка `o.high = rgb/def_hdr` плюс
  второй SV_Target в bgfx-шейдере. Требует MRT-паттерна, которого в bgfx нет → нужен
  two-pass: `resolve` (attach0) + `high_from_hdr` (attach1) из промежуточного буфера.
  ⇒ Практический компромисс: **T1 пишет в attach0 pre-tonemap linear (`raw`),
  high-пас пишет attach1 = `raw/9`, а low вычисляется позже в `combine_ps.sc`.**
  Это ровно схема AXR: `high` = pre-tonemap, `low` = tonemapped, и `combine` их не смешивает
  (в AXR low читает только `rt_Generic_0`).

⇒ **Решение (B'):** `s_hdrColor` = **pre-tonemap linear** (как сейчас, но это должен быть
освещённый HDR, а не albedo), `s_hdrHigh` = тот же raw, делённый на 9 (создаётся одним
fullscreen-пасом `high_ps.sc` за 3 строки, читает attach0, пишет attach1 — MRT не нужен,
т.к. attach0 в этот момент никто не перезаписывает; либо вообще high писать в паре с low
в combine). Тогда:
- `low` = `tonemap(raw · exposure · scale)` — **уже так** в `combine_ps.sc:165`
- `high` = `raw / 9` — новый пас
- `bloom` читает `s_hdrHigh` (**это и есть high-путь**)
- `luminance` читает `bloom1` и **не** умножает на 9 (см. §3.2)

### 2.2 Точки врезки в `bgfxHDR.cpp` (строки master @48cbfe9d9)

| Место | Что менять |
|---|---|
| `s_hdrFb` / `CreateHDRTarget` (`:918-937`) | добавить 5-й attachment `s_hdrHigh` + формат + `DestroyTextures` (`:171-185`) |
| `BindScene` (`:1029-1043`) | `bgfx_set_view_clear` очистит и high автоматически; проверить, что 0 ок |
| новый view id рядом `kSceneView`/`kSceneFxView` | `HighPass()` после T1-разрева, до `BloomPass()`/`LuminancePass()` |
| `BloomPass` (`:1125`) | `s_bloomImage` → `s_hdrHigh` вместо `s_hdrColor`; `buildSetup` не меняется (та же геометрия 4-tap) |
| `LuminancePass` (`:1066-1067`) | первый `SubmitLuminancePass` читает `s_hdrColor` → должен читать `s_bloom1` (AXR `blender_luminance.cpp:18`). Опция: оставить чтение `s_hdrHigh` + `*9` — **математически то же самое** (см. §3.2), но тайминг AXR (после bloom) и задержка на 1 кадр не совпадут |
| `GetBloomTexture` (`:1588`) | без изменений |
| `CombinePass` (`:1195-1200`) | `s_hdrSampler` остаётся low-источником; `s_hdrHigh` в combine **не нужен** (AXR его не читает) |
| константы (`:97-99`, `:158-161`) | добавить `kDefHdr = 9.0f`; проверить `kBloomThreshold` — в high-домене порог должен масштабироваться: AXR `0.00001` в high-домене = `9e-5` в low-домене |

### 2.3 Точки врезки в `combine_ps.sc` (master @48cbfe9d9)

| Место | Что |
|---|---|
| шапка (`:5-15`) | high-сэмплер **не добавляем** — AXR combine high не читает |
| `tonemap()` (`:109-135`) | без изменений (уже 1:1 с `tonemap_sRGB`, `high` не участвует) |
| `combine_bloom()` (`:92-96`) | **либо** оставить `high.rgb *= high.a` (AXR main-путь, bloom слабый), **либо** `high.rgb *= high.a * def_hdr` (компенсация 9×, как в distort-ветке `combine_2_naa.ps:163`). По умолчанию — **AXR-вариант без ×9**, вариант с ×9 отдельным флагом для ручной калибровки |
| `main()` (`:165`) | `tonemap(c, u_exposure.x * scale)` — форма верна, но `scale` требует починки (§3.2) |
| — | опционально `u_defHdr` в `u_exposure.z`, чтобы bloom/high компенсация была параметром |

---

## 3. Калибровка яркости

### 3.1 Эталонная формула

Из консоли AXR (`SourcesAXR/Layers/xrRender/xrRender_console.cpp:276-279`, `CMD4` диапазоны `:1109-1112`):

```
ps_r2_tonemap_middlegray  = 0.95      (диапазон 0.0 .. 2.0)
ps_r2_tonemap_adaptation  = 1.00      (0.01 .. 10.0)
ps_r2_tonemap_low_lum     = 0.0035    (0.0001 .. 1.0)
ps_r2_tonemap_amount      = 0.70      (0.0 .. 1.0)
r2_tonemap = R2FLAG_TONEMAP -> amount = 0 если флаг снят
```
Смешивание в CPU (`r4_rendertarget_phase_luminance.cpp:136-145`):
```
f_luminance_adapt = 0.9*f_luminance_adapt + 0.1*dt*tonemap_adaptation     # наш код: bgfxHDR.cpp:1053
tonemap_none  = (1, 0, 1)
tonemap_full  = (middlegray, 1, low_lum)
MiddleGray    = lerp(none, full, amount)
             = (0.965, 0.70, 0.30245, adapt)
```
Итоговая формула масштаба (`bloom_luminance_3.ps:72`):
```
scale = 0.965 / (result*0.70 + 0.30245)
result = luma_источника            # см. ниже про домен
scale = clamp(mix(scale_prev, scale, adapt), 1/128, 20)
```
Мы уже реализуем смешивание 1:1 (`bgfxHDR.cpp:1054-1062`), но **домен `result` у нас неверный**
— см. §3.2. Ошибка AXR: `clamp` не применён (`bloom_luminance_3.ps:77` — мёртвая строка, `:79`
возвращает `rvalue`); наш `luminance_ps.sc:60` clamp применяет. Это отличие нужно решить
явно (рекомендую оставить наш clamp — он защищает от вспышек; в AXR при `result→0`
`scale = 0.965/0.30245 = 3.19`, что не взрывается, но при `amount=0` даёт `scale=1/result`).

### 3.2 Найденный дефект: `result` у нас в 9×-неправильном домене

AXR: `result = 9·luma(high) = luma(sRGB_encode(linear·scale))` — **sRGB-домен**, «как выглядит
картинка». Мы (`luminance_ps.sc:10-14`): `hdrLuminance = dot(s_hdr.rgb, LUM) * 9.0`, где `s_hdr`
до T1 = albedo в sRGB-домене, после T1 = `low` (уже тонимпнутое, 0..1, в sRGB). Оба варианта
дают `result ≈ 9·luma(sRGB)` вместо `luma(sRGB)` ⇒ **наш scale ≈ в 3.6× меньше AXR**
(при `luma=0.2`: AXR `0.965/(0.14+0.302)=2.18`, мы `0.965/(1.26+0.302)=0.617`; при `luma=0.05`:
AXR `0.965/0.337=2.86`, мы `0.965/0.652=1.48`). Картинка систематически темнее AXR,
и после T1 дефект станет ещё глубже (домен станет `low`, а не albedo).

Минимальное исправление (одна константа в `luminance_ps.sc:13`): **`* 9.0` убрать**, оставив
`s_hdrLuminance = luma(s_hdr)`. Эквивалентно поставить `*1.0` и не заводить high-буфер:
`luma(low) ≈ luma(high)*9` с точностью до квантования 8-битного high в AXR.

Правильный (AXR-структурный) вариант: `hdrLuminance = dot(s_hdrHigh, LUM) * 9.0` — совпадает
и по формуле, и по тому, откуда берётся число. Разница только в шуме квантования.

### 3.3 Пин-поинты: что должно получаться (самосогласованные, из формул AXR)

Инверсия нашей цепочки `tonemap` (AXR `tonemap_srgb.h` + `common_functions.h:23-33`) даёт
целевые `raw`-значения, при которых на экране получается заданный 8-бит sRGB:

| Экран (8-бит sRGB) | `LinearTosRGB` на входе tonemap_srgb | `raw·scale` (до contrast-0.7) |
|---|---|---|
| 18/255 = 0.0706 | 0.00310 | 0.00333 |
| 46/255 = 0.1804 (18% серый) | 0.02110 | 0.02236 |
| 128/255 = 0.5020 | 0.20910 | 0.19062 |
| 200/255 = 0.7843 | 0.57710 | 0.34840 |
| 250/255 = 0.9804 | 0.95490 | 0.42000 (Reinhard упирается) |

Проверка «18% серый»: `result = luma(экрана) = 0.1804` ⇒ `scale = 0.965/(0.126+0.302) = 2.252`;
нужный `raw = 0.02236/2.252 = 0.00993` линейного. То есть **эталон: средняя линейная яркость
сцены ≈ 0.0099 при экране 46/255**. Удобная референс-точка: сцена, у которой `luma(low) ≈ 0.18`,
должна давать `scale ≈ 2.25`, и `u_exposure.x * scale` в `combine_ps.sc:165` должно быть ≈ 2.25.

### 3.4 Процедура A/B сверки

Регионы (в долях экрана; мерить 3×3 патч, брать медиану):

| # | Регион | Что проверяем | Допуск (8-бит) |
|---|---|---|---|
| R1 | центр кадра, «средняя» геометрия | общий масштаб | ±2 |
| R2 | тень под крышей/аркой (5–15% света) | low-привязка, клип чёрного | ±3 и ≥ 0 (нет клиппинга в нуле) |
| R3 | небо у горизонта (day) | небо-канал, scale | ±4 |
| R4 | солнце / блик на металле | high-канал, bloom | ±8 (насыщение) |
| R5 | дальний туман | fog-микс, не зависит от split-HDR | ±3 |
| R6 | сцена без неба (в помещении) | чистый albedo→light путь | ±2 |

Глобально: `mean(luma)` по всему кадру — расхождение ≤ 5%; `p99` — ≤ 8%; доля
пересвеченных (>250) — расхождение ≤ 1.5 п.п.; доля провальных (<4) — ≤ 1 п.п.
Гистограммы: сравнивать на log-шкале, окно допуска 1 значений гистограммы.

A/B протокол:
1. Фиксированная сцена + камера + погода + `r2_tonemap` + `tonemap_amount` (0.7) + адаптация
   выключена (`tonemap_adaptation` → 0.01, минимум диапазона), чтобы убрать временную фильтрацию.
2. Кадр A = AXR (эталон), кадр B = bgfx-порт. Оба 1920×1080, один и тот же tonemap-код.
3. Сравнение R1..R6 + глобальные метрики. Расхождение R1/R6 ⇒ `scale`-формула
   (§3.2/§3.1). Расхождение только R3 ⇒ небо/sky-scale. Только R4 ⇒ high-канал/bloom.
4. Один параметр за раз: сначала убрать `*9` (§3.2), потом подобрать `u_exposure.x` (сейчас
   жёстко `1.0`, `bgfxHDR.cpp:1191`), потом решать вопрос bloom `×def_hdr` (§2.3).

### 3.5 Нужен референс от lead

AXR-скриншоты (`axr.png`) в этом worktree нет. Что нужно от lead для калибровки:
- путь к 1–2 референсным кадрам AXR (день, улица, без UI) — иначе §3.4 выполняется
  «вслепую» по формулам §3.3;
- подтверждение, что backbuffer AXR — `R8G8B8A8_UNORM` (не `_SRGB`) и есть ли
  `gammaEncodeLUT` (`r__gamma`, `xrRender_console.cpp:1354`) на пути скриншота: от этого
  зависит, сравниваем ли мы экранные 8-бит значения напрямую.

---

## 4. Риски / что design **не** решает

### 4.1 high-канал в AXR визуально почти мёртв (кроме bloom)
Путь `high`: пишется (`sky2`, `combine_1`) → читается только bloom bright-pass. В
`combine_bloom` (`common_functions.h:77-82`) `high` — это **параметр-имя**, а реально туда
приходит bloom-текстура. Поэтому:
- вклад high в финальную картинку = только через яркость bloom, и в 9× ослабленный
  (нет компенсации `def_hdr` в основном пути, §1.4);
- «8×/9× dynamic range» из комментария — наследие DX9 (там high/low складывались при
  чтении), в R4 кода реконструкции `low + high*9` нет вообще.

Вывод: внедрять split-HDR ради «ярче/реалистичнее» — неверная мотивация. Оправдание —
только структурное (pipeline AXR-совместим, high готов для `distort`/future rain/HUD-mask,
которые в AXR переиспользуют `rt_Generic_1`).

### 4.2 Порядок зависимостей (после T1 = `light-build`)

T1 (deferred sun+hemi resolve) должен:
- писать в `s_hdrColor` **освещённый линейный** цвет, а не albedo;
- не тонимповать (tonemap остаётся в `combine_ps.sc`) — иначе high = low/9 теряет смысл
  и §3.2 снова ломается;
- не превышать диапазон: если T1 начнёт писать значения ≫1, `high` в 8-битном варианте
  переполняется (у нас 16F — ок, но AXR-паритет по точности теряется).

Порядок внедрения:
```
T0  (этот док)                       — только анализ
T1  light-build: освещённый HDR в s_hdrColor  [блокирует всё ниже]
T1.5 калибровка scale: убрать *9 в luminance_ps.sc:13, подобрать u_exposure.x
     → это отдельный коммит, проверяется по §3.4 R1/R6 без high-канала
T2  s_hdrHigh + high-пас (attach 4) + high = raw/9
T3  BloomPass читает s_hdrHigh (bgfxHDR.cpp:1125), b_params.x пересчитать в high-домен
T4  (опц.) LuminancePass читает s_bloom1 + снять задержку 1 кадр (AXR-структура)
T5  (опц.) distort-mask на s_hdrHigh, чтобы повторить переиспользование rt_Generic_1
```
T1.5 стоит **до** T2: пока нет high-буфера, поправить `*9` можно одной строкой и сразу
получить измеримый выигрыш в 3.6× яркости. T2/T3 этого не дают.

### 4.3 Прочее
- MSAA: AXR делает resolve обоих RT в `R8G8B8A8_UNORM` (`phase_combine:401-402`). У нас MSAA
  нет (ветка `_msaa` в AXR для сравнения, не для внедрения).
- `bloom_filter_f` / FASTBLOOM: в bgfx-порте не портированы (нет `bloom_filter_f.sc`), high
  на них не влияет.
- `def_hdr_clip = 0.75` в AXR нигде не используется (только объявление, `common_defines.h:12`).
- Наш `luminance_ps.sc:60` дублирует clamp корректно; решение по «мёртвому clamp» AXR —
  оставить наш (см. §3.1).
