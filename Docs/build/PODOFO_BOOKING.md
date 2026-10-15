# PoDoFo Booking: Offline-PDF-Extraktion (C++23)

## Motivation

PoDoFo 1.1.1 liefert Texteinträge mit Seitenzahl, X- und Y-Position. Dieses
Lehrprojekt tastet sich an eine mehrseitige Buchungstabelle heran, ohne
Kundendaten hochzuladen oder zu speichern. Es ist bewusst ein Diagnosewerkzeug,
noch **kein** produktiv belastbarer Import für Finanzdaten.

Erwarteter Anfang: Beleg, Buchungstext, Datum, Betrag in EU. Zweizeilige
Buchungstexte werden an die vorausgehende Buchung angehängt, sofern ihre
Textposition eingerückt ist. `Übertrag` wird als Prüfsumme, nicht als
Buchung interpretiert. `Gesamtsumme` ist die abschließende Prüfung. Beträge
werden centgenau als vorzeichenbehaftete ganze Zahlen verarbeitet.
Unbekannte Textzeilen erzeugen Warnungen, und eine abweichende Gesamtsumme
führt zum Fehlerstatus. Ohne echte Beispieldatei sind die Layoutheuristiken
nur vorläufig. Gescannte PDFs benötigen OCR (nicht Bestandteil des Beispiels).

## Lokaler CMake-Build mit manuellem Third-Party-Stack

Der Projektordner `test/PoDoFoBooking` hat ein **eigenständiges** CMakeLists.
Es werden weder BuildEngine noch Bootstrap, PowerShell oder Downloads
aufgerufen. Vorhanden sein muss das zu C++Builder/bcc64x passende installierte
PoDoFo 1.1.1, einschließlich kompatibler Abhängigkeiten. Deren CMake-Prefixes
werden bei Bedarf über `CMAKE_PREFIX_PATH` angegeben. Die installierte
PoDoFo-Version stellt das Target `podofo::podofo` bereit.

Beispiel ab Repository-Wurzel (Pfade an die lokale Installation anpassen):

```cmd
cmake -S test/PoDoFoBooking -B test/PoDoFoBooking/build/Release -G Ninja ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_CXX_COMPILER="C:/Embarcadero/Studio/37.0/bin/bcc64x.exe" ^
  -DPODOFO_PACKAGE_DIR="D:/local/thirdparty/podofo/1.1.1/lib/win64/Release/cmake/podofo"

cmake --build test/PoDoFoBooking/build/Release
ctest --test-dir test/PoDoFoBooking/build/Release --output-on-failure
cmake --install test/PoDoFoBooking/build/Release --prefix apps/Release --component podofo_booking
```

Das Testprogramm `parser_test.cpp` verwendet **ausschließlich synthetische
Daten** und läuft ohne geheime PDF-Dateien. Bei Shared-Builds müssen die
PoDoFo- und Abhängigkeits-DLLs im lokalen Windows-`PATH` verfügbar sein.

## Ausführung ausschließlich auf dem eigenen Rechner

```cmd
apps\Release\adecc_podofo_booking.exe --inspect "D:\private\kunde.pdf"
apps\Release\adecc_podofo_booking.exe --extract "D:\private\kunde.pdf"
apps\Release\adecc_podofo_booking.exe --extract "D:\private\kunde.pdf" --show-bookings
apps\Release\adecc_podofo_booking.exe --extract "D:\private\kunde.pdf" --csv "D:\private\buchungen.csv"
```

`--inspect` zeigt die **vertraulichen Originaltexte** mit Seiten- und
Koordinateninformation an, also nicht in geteilte Logs umleiten oder hochladen.
`--extract` gibt standardmäßig nur Anzahl, Prüfungen und Positionswarnungen
aus. `--show-bookings` zeigt vertrauliche Buchungsdaten ausdrücklich an.
CSV wird nur auf Wunsch und nur nach erfolgreichem Summenabgleich erzeugt;
vorhandene Zieldateien werden nicht überschrieben.

Bei unbekanntem PDF-Layout ist ein Fehler ein erwünschtes Ergebnis. Nur
redigierte Koordinaten/Strukturinformationen zum Debugging weitergeben.
Ein nächster Entwicklungsschritt ist die Zuordnung anhand X-Spaltenbereichen
und eine FSM für Seitenumbrüche innerhalb einer Buchung; derzeit verwendet
der Parser eine erste zeilenbasierte Mustererkennung.
