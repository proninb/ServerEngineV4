# SOURCE-CPU-SAMPLING-02

**Статус: фактический capture и анализ завершены; ranking пригоден с ограничениями полноты стеков.**
Итоговые результаты приведены ниже. Предварительные проверки среды сохранены как история. Source V2, WHO/type resolution,
Direct G, persisted formats и принятые архитектурные ограничения не менялись.

## Проверка среды

Установлены WPR, xperf и WPA/WPAExporter. WPR перед проверкой не записывал trace.
Попытка `wpr -start CPU -filemode` вне sandbox завершилась ошибкой **0xc5585011**:
`Failed to enable the policy to profile system performance`.
Текущий Windows token не административный; `whoami /priv` не содержит
SeSystemProfilePrivilege (есть только SeChangeNotify/SeIncreaseWorkingSet).
Отдельная попытка xperf вернула **0x3ec, Invalid flags**; это сообщение само по себе
не используется как доказательство причины privileges. После попыток WPR сообщает
`WPR is not recording`. Чужие сессии не останавливались, системная policy не менялась.

Sandbox escalation не выдаёт административный Windows token. Это ограничение ОС,
а не отклонение действия автоматическим approval review. Дальнейшие попытки без
другого token не дадут нужный CPU trace.

## EXE и matching PDB

Сохранённый production benchmark из PUBLISH-END-TO-END-PROFILE-01 не имеет Release
PDB/RSDS identity. Он оставлен нетронутым. Отдельно собран тот же production code:

- MSVC Release, /O2 /Ob2 /DNDEBUG, IPO/LTCG ON;
- дополнительно /Zi и /DEBUG:FULL;
- сохранены /OPT:REF /OPT:ICF, /INCREMENTAL:NO;
- PGO OFF, обычные production feature defaults; никаких probes/clock scopes.

EXE/PDB проверены непосредственно по CodeView и MSF7 info stream:

- GUID: `cbfc32a9-81f3-4d72-9ee2-4ba17caf8048`, age **1**;
- EXE SHA256: `4c43e0cf343aa60bfe7ecfb817aea585803a70699fa05dd2a8caa3d33abfae71`;
- PDB SHA256: `bd109fc96201e03ee3fcecd0bf91b4bcf7c01f815a47e71cfd43a9a21451b095`.

Новый .text SHA отличается от старого no-PDB executable. Поэтому это не заявление
о побайтово неизменённом старом EXE с «подходящими» символами: PDB от нового EXE
нельзя применять к старому. В будущей серии control и sampled обязаны использовать
одну и ту же зафиксированную новую пару. IPO ради символов не отключался.

## Выполненный контроль без ETW

На symbols build выполнено 18 full PUBLISH: два исключённых warmup, четыре блока
по четыре контрольных вызова. Native вне sandbox, без builds/tests во время серии.
После каждого вызова совпали golden compiled.bin и runtime.bin SHA256.

Медиана 16 измеренных controls:

| Метрика | Время |
| --- | ---: |
| Полный PUBLISH (coarse timeline) | 1373,17 мс |
| Source wall | 429,39 мс |

Это валидация новой symbols build и готовый control-only dataset. Нет ETW overhead
estimate: sampled вызовов не было. С историческими медианами это не парное A/B и
не свидетельство ускорения/регрессии из-за debug information.

## Подготовленный сбор

`experiments/source-cpu-sampling-02/capture.ps1` запускается в административной
PowerShell. Он не повышает привилегии сам и не меняет policy. Подробные команды
сборки, контроля и анализа — в README рядом.

План collector: два warmup; восемь control/sampled блоков в чередующемся порядке;
по шесть отдельных full PUBLISH на режим/блок. Одинаковые EXE/PDB проверяются по
GUID/age/hash перед каждым блоком; golden artifacts — после каждого вызова.
Сохраняются PID, total/Source wall, caller timestamps, stdout/stderr и ETL каждого
sampled блока. Счётчики прошлого clock-heavy эксперимента не включаются.

Только свой WPR instance с уникальным именем. Stop выполняется в finally и только
после успешного start, с тем же instance name. При ошибке stop сохраняется owned
instance для восстановления; глобальный cancel/stop чужой записи не используется.
Существующие datasets не перезаписываются. Профилирование охватывает процесс целиком;
Python/golden hashing отсеиваются по записанным benchmark PIDs.

Collector различает `blocked`, `control_only`, `captured_not_analyzed` и `failed`.
Непустой ETL ещё не считается успешным sampling audit.

## Анализ и критерии приёмки

WPA CPU Usage (Sampled), matching local symbols, только фактически профилируемые
block/PID pairs. Inclusive и exclusive samples/CPU weights рассматриваются отдельно;
CPU time не приравнивается к wall time. Нужен аудит sample interval, event/stack loss,
неразрешённых application frames и полноты стека. Helpers, исчезнувшие при IPO,
нельзя объявлять бесплатными или восстанавливать выдуманными frames.

Подготовлен offline aggregator для нормализованных полных stack rows: cursor,
binding, object lookup, endpoint/member transitions, path interning, Graph writes,
provenance, прочие функции. Inclusive категории могут пересекаться; exclusive
выбирается по фактическому leaf. Recursion не умножает inclusive function samples.
Нет Source frame — нет принудительного назначения Graph sample фазе Source.

Минимальные review gates: 10 000 Source samples, минимум четыре блока, измеренные
нулевые event/stack losses, проверенные symbols и отсутствие missing/unresolved
application stacks. Это предварительные пороги качества, не доказательство
статистической независимости samples или точности долей. Автоматический адаптер
WPA tree export пока не проверен на реальном ETL; нельзя выдавать tree subtotals
за независимые полные stacks. Этот участок ещё предстоит завершить на capture.

Tool tests **3/3**: учёт inclusive/exclusive и recursion, PID filtering/неполные стеки,
matching реального PDB и отказ на намеренно изменённом EXE GUID. Первые два теста
используют синтетические records и не являются измерениями production CPU.

## Первоначальный план (выполнен последующим административным capture)

1. Административный запуск capture.ps1.
2. Проверка действительных ETL stacks, symbols, dropped events и observer delta.
3. Экспорт/нормализация реальных stack rows и inclusive/exclusive CPU ranking.
4. Только затем — один алгоритмический эксперимент с парным полным PUBLISH.

До этих шагов SOURCE-CPU-SAMPLING-02 не объявляется завершённым диагностическим
этапом. Production bottleneck и допустимая цель следующего ускорения не определены.

Артефакты: `build/source-cpu-sampling-02/`: symbols/Release, preflight manifest,
control measurements/logs, blocked-check manifest, build/configure logs, frozen
EXE/PDB и manifest. Фактический ETW dataset и результаты указаны ниже.


## Фактический результат 22 октября 2026

Dataset: `build/source-cpu-sampling-02/capture-20261022-212914/`.
Восемь ETL, 48 control и 48 sampled PUBLISH, два warmup. Все вызовы
прошли проверку golden compiled/runtime SHA-256. Production код не изменён;
использован отдельный оптимизированный EXE с /Zi, /DEBUG:FULL и IPO ON.
Это не побайтово тот же старый EXE без PDB. Control и sampled используют одну пару.

Реальные SampledProfile events прочитаны установленной Microsoft TraceEvent 3.1.27
через `reader/`; только записанные block/PID, без context-switch stack events.
GUID/age загруженного application module совпадают с frozen PDB. OS symbols
не загружались из сети. Интервал sampling — 1 мс. Артефакты анализа:
`analysis/cpu-summary.json`, `functions.csv`, `groups.csv`, восемь JSONL полных стеков,
метаданные reader, symbol logs и xperf trace statistics.

| Проверка | Результат |
|---|---:|
| CPU samples целевых процессов | 309 488 |
| Samples с распознаваемым Source frame | 20 904 |
| Блоки со Source samples | 8 |
| Потерянные ETW events/buffers | 0 / 0 |
| Samples без стека | 285 (0,0921% всех целевых) |
| Unresolved application samples | 0 |

Строгий первоначальный zero-missing-stack gate **не выполнен**. Если все 285
неполных samples принадлежали Source, их количество равно 1,36% распознаваемых
Source samples. Это граница только для отсутствующих стеков, не оценка всей ошибки
атрибуции: IPO может скрыть Source frames. Статус —
`samples_available_with_stack_coverage_limitations`, не exact semantic decomposition.

### Стоимость наблюдения

| Среднее по 48 вызовам режима | Control | Sampled | Разница |
|---|---:|---:|---:|
| PUBLISH wall, мс | 1345,222 | 1470,230 | +125,008 (+9,29%) |
| Source wall, мс | 423,897 | 435,967 | +12,071 (+2,85%) |

Медиана восьми парных разностей средних блоков: +124,578 мс PUBLISH и
+13,247 мс Source. Это overhead sampling, а не регрессия алгоритма.

### CPU distribution внутри распознаваемого Source

| Группа | Inclusive samples | Exclusive samples |
|---|---:|---:|
| Graph writes и их индексы | 4969 | 4154 |
| String Table | 2454 | 1982 |
| Cursor | 4394 | 1957 |
| Endpoint parser | 5848 | 1338 |
| Endpoint path interning и индекс | 3145 | 1198 |
| Identity binding | 1198 | 916 |
| Member transitions | 1650 | 885 |
| Provenance | 379 | 306 |
| Other | 3747 | 8168 |

Inclusive группы пересекаются; exclusive дают ровно 20 904 samples. Группировка
опирается на имена существующих функций, включая internal index helpers.
Object lookup не имеет отдельного различимого hotspot; отсутствие отдельного
frame при IPO не означает нулевую стоимость.

Наиболее частые exclusive application leaves: общий vector::push_back — 2086;
string_table::intern_prehashed — 1661; read_endpoint — 1338;
insert_link_target_index — 1328; assignment — 1231; advance — 1133.
Имя vector<derived_type_record>::push_back нельзя трактовать буквально: ICF/IPO
объединяет машинный код. Среди этих leaves 1301 имеют ближайшего application caller
intern_resolved_endpoint_path, 626 — append_member, 111 — add_resolved_link.
Они остаются Other в строгой leaf-классификации, а не искусственно переносятся
в exclusive родителя. Path interning уже использует range insert для steps;
простая замена push_back на пакетную запись steps не является новым решением.

### Ровно один следующий эксперимент

**SOURCE-STRING-PROBE-03**: проверить групповой поиск fingerprint в существующем
string_table index вместо последовательного просмотра bucket slots. Сохранить
exact byte comparison при совпадении fingerprint, порядок string_id, правила
baseline/delta и прежний путь semantic_input → intern_prehashed. Никакой второй
таблицы WHO, нового Graph слоя или предварительного глобального интернирования.
Это кандидат, не реализованная оптимизация и не обещание выигрыша: String Table
имеет 1982 exclusive samples (9,48% различимого Source CPU), но sampling не
отделяет probing от копирования/вставки внутри intern_prehashed.

Проверка кандидата: один executable с A/B, unchanged golden hashes, парные
полные PUBLISH без ETW и отдельная проверка collision/rehash/baseline semantics.
При отсутствии убедительного полного wall-time выигрыша оставить baseline.
Не повторять отвергнутый combined Graph upsert probe эксперимент.

Production PUBLISH не изменён. Полученный control около 1,345 с — измерение
этой серии, а не доказанное новое ускорение относительно исторических серий.
