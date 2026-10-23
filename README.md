# Crystal Voice · STUDIO KAUTZ

AU/VST3-Plugin für den Gesangskanal. Rechnet zwei Störanteile aus dem Mikrofonsignal heraus, und zwar mit Information, die vorab bekannt ist:

1. **Instrumental-Übersprechen** über PA und Monitore. Referenz ist die Instrumental-Spur selbst, sampleexakt. Ein adaptiver Filter lernt den Weg Lautsprecher → Halle → Mikro und zieht die Vorhersage ab.
2. **Raumhall der Stimme.** Aus der Referenz-Vocal-Spur und der gemessenen Raum-Impulsantwort wird laufend berechnet, wie viel Hallenergie gerade in jedem Frequenzband zu erwarten ist. Diese Anteile werden abgesenkt.

Der Audiopfad hat **0 Samples Latenz**. Alles, was Zeit braucht (Analyse, Filterberechnung), läuft im Seitenpfad und steuert einen minimalphasigen Filter.

---

## Bedienung

Die Oberfläche ist einem Studio-Magnettonschrank der 1960er nachempfunden: Hammerschlaglack mit Siebdruck (Name, Logo), Einschübe mit Schlitzschrauben, schwarze Einlagen hinter Blechausschnitten.
- **Instrumente:** INPUT und OUTPUT als VU-Meter (0 VU = −18 dBFS, 300 ms), FEEDBACK zeigt die gemessene Absenkung des Playbacks (0–30 dB).
- **STATUS-Lampen:** LINK = Instrumental kommt an · ROOM = Raumkorrektur läuft · FAULT = es fehlt etwas (Raum-IR oder Referenz-Vocal).
- **VOCAL / INSTRUMENTAL:** Schieberegler, Wert im Zählwerk darunter. Doppelklick = 50.
- **SYNC:** Alu-Drehknopf, senkrecht ziehen; Doppelklick = 0.
- **MICROPHONE / PRESENCE / FUNCTION:** Drehschalter. Auf die Beschriftung einer Stellung klicken, auf den Knopf klicken (schaltet weiter) oder Mausrad.
- **FUNCTION:** PROCESS = Hauptinstanz auf dem Mikrokanal · SEND INSTR. / SEND REF. = Sender auf einer Quellspur (siehe Einrichtung).
- **DATA:** Auswahl der Spuren INSTRUMENTAL und REFERENCE VOCAL; Raum-IR laden oder mit **MEASURE** messen; Klartext-Status auf der Karte.

| Regler | Wirkung |
|---|---|
| **VOCAL** 0–100 | 0 = aus. 50 = Hallschätzung einfach abgezogen, Absenkung höchstens −12 dB. 100 = doppelt abgezogen, bis −24 dB. |
| **INSTRUMENTAL** 0–100 | 0–50: Abzug der Playback-Vorhersage von 0 auf 100 %. 50–100: zusätzlich Unterdrückung des verbleibenden Rests bis −20 dB. |
| **MICROPHONE** NEUTRAL / PR 37 / KSM9 / KMS 105 | NEUTRAL (Standard) = keine Klangänderung. Sonst sanfte Anpassung an die Kapsel, siehe unten. Umschalten wird über 30 ms überblendet. |
| **PRESENCE** | Glocke +2 dB bei 3 kHz, breit (Q 1,0). |
| **SYNC** ±100 ms | Verschiebt die Referenz-Vocal-Spur gegen den Live-Gesang. Startwert 0; nur nachstellen, wenn der Sänger deutlich vor oder hinter der Referenz liegt. Unkritisch: Die Simulation bleibt bei ±40 ms Versatz wirksam. |

Bei VOCAL 0 / INSTRUMENTAL 0 und MICROPHONE NEUTRAL ohne Presence läuft das Signal bitgenau unverändert durch.

## Mikrofon-Anpassung

Grundlage sind die Herstellerangaben und Testberichte. Die Hersteller veröffentlichen die Kurven nur als Grafik, daher sind die Eingriffe bewusst klein gehalten (höchstens 2,5 dB). Die Klangstufe sitzt hinter der Entstörung, arbeitet minimalphasig und hat 0 Samples Latenz.

| Stellung | Kapsel laut Datenblatt | Korrektur | Wirkung (gerechnet) |
|---|---|---|---|
| **PR 37** | Dynamisch, Superniere, 40 Hz–18 kHz, betonte obere Mitten. Die Membranmasse kostet Luft im obersten Bereich. | Hochpass 50 Hz · Kuhschwanz −1 dB @ 150 Hz (Nahbesprechung) · Höhen-Kuhschwanz +2,5 dB ab 9 kHz | +1,5 dB @ 10 kHz, +2,4 dB @ 16 kHz, −0,8 dB @ 120 Hz |
| **KSM9** | Kondensator, 50 Hz–20 kHz, sehr glatt, Bass schon ab Werk sanft abfallend. Wenig zu korrigieren. | Hochpass 60 Hz (Hand- und Bühnengeräusche) · +1 dB @ 5 kHz, breit | +1,0 dB @ 5 kHz, sonst linear |
| **KMS 105** | Kondensator, Superniere, 20 Hz–20 kHz, linear bei 5 cm, ca. +5 dB um 12 kHz, Bass fällt mit Abstand unter 200 Hz. | Hochpass 45 Hz · −2,5 dB @ 12 kHz (Q 1,2) · Kuhschwanz +1 dB @ 180 Hz | −2,5 dB @ 12 kHz, +0,7 dB @ 120 Hz |

**Hinweis PR 37 + PRESENCE:** Das PR 37 betont die oberen Mitten schon selbst. Mit Presence summiert sich das; in einer hellen Halle kann es dann spitz werden.

## Einrichtung

**Spuren verbinden (Logic/MainStage, sinngemäß in jeder DAW)**

Ein Plugin kann andere Spuren nicht selbst abgreifen, und Logic bietet pro Plugin nur einen Sidechain. Deshalb arbeitet Crystal Voice mit Sende-Instanzen:

1. **Instrumental-Spur:** Crystal Voice als Insert, FUNCTION auf **SEND INSTR.**, OUTPUT **THRU** (die Spur klingt normal weiter).
2. **Referenz-Vocal-Spur:** Crystal Voice als Insert, FUNCTION auf **SEND REF.**, OUTPUT **MUTE** – die Spur bleibt aktiv, ist aber nicht zu hören. (Die Spur nicht in der DAW stummschalten, sonst rechnet der Host sie evtl. nicht.)
3. **Mikrokanal:** Crystal Voice als Insert, FUNCTION auf **PROCESS**. Unter DATA erscheinen die Sende-Spuren in den Menüs INSTRUMENTAL und REFERENCE VOCAL. **AUTO** (Standard) nimmt die erste passende Sende-Spur.
4. Die Namen der Sende-Spuren übernimmt das Plugin vom Host, falls er sie liefert (VST3); sonst im Sender unter NAME eintragen (z. B. „Playback“, „Lead Guide“).

Hinweise:
- Gesendet wird nur bei **laufendem Transport**; abgeglichen wird über die Song-Position. Reihenfolge und Vorlauf, mit denen der Host die Spuren rechnet, spielen keine Rolle.
- Alle Instanzen müssen im selben Format laufen (alle AU oder alle VST3) – der Austausch geschieht innerhalb des Plugin-Bausteins.
- Ohne Sende-Instanz für das Instrumental nutzt AUTO weiterhin den **Host-Sidechain** des Plugins (wie bisher).
- Die Referenz-Vocal-Spur sollte möglichst trocken sein (Studio/Kabine). Enthält sie schon Hall, wird zu viel Hall erwartet: VOCAL niedriger fahren.

**Raum-IR messen – direkt im Plugin (MEASURE)**

Verfahren: exponentieller Sinus-Sweep 30 Hz–20 kHz (Farina), −12 dBFS, 6 s. Ablauf: 1 s Ruhe (Grundrauschen) → Sweep → 4 s Nachhall. Danach wird die Impulsantwort berechnet, auf den Bereich oberhalb des Rauschens gekürzt, als `~/Music/Crystal Voice/Raum-IR <Datum>.wav` gespeichert und sofort verwendet. Die Statuskarte meldet Nachhallzeit (T60) und Rauschabstand.

So geht's:
1. **Kleinen aktiven Lautsprecher an die Mundposition** stellen (Mitte der 2 × 2 m, Mundhöhe, zum Mikro gerichtet). Den Ausgang des Gesangskanals für die Messung auf das Interface-Ausgangspaar dieses Lautsprechers legen.
   Warum nicht über die PA: Das Plugin braucht das Verhältnis Direktschall zu Hall *von der Mundposition aus*. Von der PA aus gemessen ist der Direktschall viel schwächer – der Hall würde überschätzt und VOCAL griffe zu stark.
2. Transport **stoppen** (bei laufendem Song verweigert das Plugin die Messung), Spur auf **Eingangs-Monitoring** bzw. Aufnahmebereitschaft, damit die DAW das Plugin auch im Stand rechnet.
3. Lautsprecher mäßig laut, Mikro am Stativ, Ruhe im Saal → **MEASURE**. 11 s später ist die IR geladen. **STOP** bricht jederzeit ab.
4. Meldung prüfen: SNR ab ca. 40 dB ist gut. Bei Übersteuerung wird die Messung verworfen; bei geringem Rauschabstand lauter messen.

Klirr des Lautsprechers stört nicht: Beim exponentiellen Sweep landen die Verzerrungsprodukte zeitlich vor dem Direktschall und werden abgeschnitten. Getestet (`tests/ir_test.cpp`, Ergebnis in `tests/ir_ergebnis.txt`): T60 von 0,8 / 1,6 / 2,6 s bei 44,1 / 48 / 96 kHz auf ±1 % getroffen, mit 7 ms Interface-Latenz und 1 % Lautsprecherklirr.

Alternativ lässt sich eine extern gemessene IR (z. B. Room EQ Wizard) als Mono-WAV über **LOAD** laden.

**Soundcheck**
- 10–20 s Instrumental über die PA laufen lassen, ohne Gesang. Dann hat der Playback-Filter den Raumweg gelernt. Danach führt er in jeder Gesangspause nach. Während gesungen wird, ist er eingefroren. Woher es weiß, wann gesungen wird: aus der Referenz-Vocal-Spur.
- Spurwahl und Pfad der Raum-IR werden im Projekt gespeichert. Den gelernten Filter speichert das Plugin nicht; er wird nach dem Laden in Sekunden neu gelernt.

## Messwerte (Simulation)

Synthetische Halle mit T60 1,6 s, Stimmhall −10 dB unter Direktschall, PA-Übersprechen mit 12 ms Laufzeit. Die Raum-IR fürs Modell ist eine *andere* Messung mit gleicher Hüllkurve und anderer Feinstruktur (simuliert eine leicht andere Position). Die Referenz-Vocal-Spur ist 1 ms versetzt und hat einen anderen Pegel. Vollständige Ausgabe: `tests/sim_ergebnis.txt`.

| | Ergebnis |
|---|---|
| Playback-Übersprechen, INSTR 50 / 100 | −15 dB / −24 dB |
| Stimmhall in Pausen und Ausklängen, VOCAL 50 / 100 | −10 dB / −21 dB |
| Stimmhall während gehaltener Töne, VOCAL 100 | −2,6 dB (Direktschall −1,3 dB) |
| Timing-Fehler des Takes ±40 ms | Hall in Pausen weiterhin −16 bis −23 dB |
| CPU (48 kHz, ein Kern) | ca. 7 % |

**Grenze:** Während ein Ton gehalten wird, liegt sein Nachhall auf denselben Frequenzen wie der Ton selbst. Mit Betrags-Filtern ist er dort kaum zu trennen, aber dort maskiert ihn die Stimme auch. Hörbar wird Hall in Pausen, bei Tonwechseln und im Ausklang, und dort greift das Plugin voll.

## Bauen (macOS)

Voraussetzungen: Xcode (mit Command Line Tools) und CMake (`brew install cmake`). JUCE 8 wird beim ersten Konfigurieren automatisch geladen.

```bash
cmake -B build -G Xcode
cmake --build build --config Release --target CrystalVoice_AU CrystalVoice_VST3
# Ergebnis: "Crystal Voice.component" und "Crystal Voice.vst3", Hersteller STUDIO KAUTZ
```

Die Plugins werden automatisch nach `~/Library/Audio/Plug-Ins/Components` bzw. `…/VST3` kopiert (Universal Binary, Apple Silicon + Intel, ab macOS 11).

AU prüfen und Logic neu scannen lassen:

```bash
auval -v aufx Crv1 Kutz
```

Messung bzw. Simulation erneut laufen lassen:

```bash
cmake --build build --config Release --target cv_sim && ./build/Release/cv_sim
```

## Aufbau

```
core/            DSP-Kern, reines C++17, ohne JUCE
  Aec.*          Playback-Canceller: partitioniertes Frequenzbereichs-NLMS, 500 ms, ohne Latenz
  RoomModel.*    Raummodell aus der IR: späte Energie je Verzögerung und Band
  StreamHub.h    Signalaustausch zwischen Instanzen (Sender → Hauptinstanz), über die Song-Position
  Engine.*       Signalfluss, Gain-Berechnung, minimalphasiger FIR (Cepstrum), Überblendung
  IrMeasure.*    Raummessung: Sweep erzeugen, Dekonvolution, Kürzen, T60/SNR
  ToneStage.h    Mikrofon-Anpassung und Presence (Biquads, Überblendung beim Umschalten)
  Bands.h/Fft.h  ERB-Bänder, FFT
Source/          JUCE-Plugin: Processor (Betriebsarten, Spurwahl, Sidechain, Messung), Editor
tests/sim.cpp            Simulation mit Messung in dB
tests/plugin_smoke.cpp   Host-Simulation (cmake -DCV_BUILD_SMOKE=ON)
tests/editor_snapshot.cpp  rendert die Oberfläche als PNG (gleiche Option)
Resources/       Schrift Barlow Condensed (SIL Open Font License, Text in Resources/licenses), Logo
```

## Nicht abgedeckt / nächste Schritte

- **Wedges:** Kommt die eigene Stimme über Bodenmonitore zurück, ist das ein Feedback-Pfad. Den behandelt Crystal Voice nicht. In-Ear ist unkritisch.
- **Playback-Nachhall jenseits von 500 ms** wird nicht abgezogen. Der Rest wird von der Restunterdrückung (INSTRUMENTAL > 50) erfasst.
- **Test mit echtem Material:** Aufnahme vom Bühnenmikro mit laufendem Playback (mit Gesang und mit Pausen), dazu Instrumental, Referenz-Vocal und IR. Damit lassen sich die Kennlinien der Regler auf die reale Halle abstimmen.
- Option: Gelernten Playback-Filter im Projekt speichern, damit er direkt ab dem ersten Takt greift.

## Mac-Version ohne eigenes Xcode (GitHub)

`.github/workflows/build-mac.yml` baut AU und VST3 auf einem macOS-Server von GitHub.
1. Kostenloses Konto auf github.com, neues Repository anlegen, den Inhalt dieses Ordners hochladen (inkl. `.github`).
2. Reiter **Actions** → Lauf „Build macOS“ abwarten (ca. 10 min) → unter **Artifacts** `CrystalVoice-macOS` herunterladen.
3. Für einen öffentlichen Download: Tag `v0.1.0` setzen → die ZIP erscheint unter **Releases**.

Öffentliche Repositories bauen kostenlos ohne Limit. Bei privaten zählt die Mac-Laufzeit gegen das monatliche Freikontingent.
Die ZIP ist nicht signiert; die beiliegende INSTALLATION.txt erklärt den einmaligen Terminal-Befehl.
