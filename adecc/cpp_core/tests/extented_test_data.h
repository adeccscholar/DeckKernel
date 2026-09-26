// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_data.h
\brief Representative accounting master data and postings for the extended integration tests.

\details
Defines account types, account classes, nominal accounts, tax classes, and postings used to drive the
persistence and range examples. The data provides a realistic domain-shaped input for demonstrating that the
library transports typed values rather than framework records.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Practical Example: Financial Data as a Typed Data Flow".
- "SystemData as an Application of the Type List".
- "Database as Source, Transformation, and Sink".
- "The Grid as a Projection, Not as Truth".

\see ../ARCHITECTURE.md#files-as-typed-data-flows

\version 1.0
\date 26.09.2026
\author Volker Hillmann (adecc Systemhaus GmbH)

\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH

\licenseblock{LicenseRef-PolyForm-Noncommercial-1.0.0}
This file is licensed under the PolyForm Noncommercial License 1.0.0.
Use, modification, and distribution are permitted only as defined by that license.
The complete and controlling terms are available at
https://polyformproject.org/licenses/noncommercial/1.0.0/.
Any use not permitted by that license requires separate permission or a separate
license from adecc Systemhaus GmbH.
\endlicenseblock

*/

#pragma once

#include "extented_test_types.h"
#include "stream_tools.h"

#include <vector>

namespace extented_test {

inline std::vector<acct_type_ty> const vecAccountTypes{
   { 0, "Sachkonto", "SK", "Allgemeines Sachkonto der Finanzbuchhaltung" },
   { 0, "Debitor",   "DE", "Kundenkonto beziehungsweise Forderungskonto fuer einen konkreten Debitor" },
   { 0, "Kreditor",  "KR", "Lieferantenkonto beziehungsweise Verbindlichkeitskonto fuer einen konkreten Kreditor" }
};

inline std::vector<acct_class_ty> const vecAccountClasses {
   { 0, "Aktivkonto",          "Bestandskonto der Aktivseite, typischer Saldo im Soll",   "ASSET",     "Debit",  false, true,  true,  false, 10 },
   { 0, "Passivkonto",         "Bestandskonto der Passivseite, typischer Saldo im Haben", "LIABILITY", "Credit", false, true,  false, true,  20 },
   { 0, "Eigenkapital",        "Eigenkapitalkonto, typischer Saldo im Haben",             "EQUITY",    "Credit", false, true,  false, true,  30 },
   { 0, "Ertragskonto",        "Erfolgskonto für Erträge, erhöht das Ergebnis",           "REVENUE",   "Credit", true,  false, false, true,  40 },
   { 0, "Aufwandskonto",       "Erfolgskonto für Aufwendungen, mindert das Ergebnis",     "EXPENSE",   "Debit",  true,  false, true,  false, 50 },
   { 0, "Vorsteuerkonto",      "Steuerliches Aktivkonto für abziehbare Vorsteuer",        "TAX_ASSET", "Debit",  false, true,  true,  false, 60 },
   { 0, "Umsatzsteuerkonto",   "Steuerliches Passivkonto für geschuldete Umsatzsteuer",   "TAX_LIAB",  "Credit", false, true,  false, true,  70 }
};

inline std::vector<acct_raw_ty> const vecAccounts{
   {  0, 1000, "Bank Hauptkonto",                             "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1010, "Bank Nebenkonto",                             "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1020, "Kasse",                                       "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1100, "Forderungen Inland",                          "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1110, "Forderungen Ausland",                         "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1200, "Geleistete Anzahlungen",                      "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1300, "Vorräte Rohstoffe",                           "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1310, "Vorräte Handelswaren",                        "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1400, "Vorsteuer 19%",                               "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1407, "Vorsteuer 7 Prozent",                         "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1410, "Vorsteuer innergemeinschaftlicher Erwerb",    "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1577, "Abziehbare Vorsteuer 19 Prozent",             "Aktivkonto",    "SK", "EUR", true  },
   {  0, 1500, "Aktive Rechnungsabgrenzung",                  "Aktivkonto",    "SK", "EUR", true  },
   {  0, 2000, "Verbindlichkeiten Inland",                    "Passivkonto",   "SK", "EUR", true  },
   {  0, 2010, "Verbindlichkeiten Ausland",                   "Passivkonto",   "SK", "EUR", true  },
   {  0, 2100, "Erhaltene Anzahlungen",                       "Passivkonto",   "SK", "EUR", true  },
   {  0, 2200, "Umsatzsteuer 19%",                            "Passivkonto",   "SK", "EUR", true  },
   {  0, 2207, "Umsatzsteuer 7 Prozent",                      "Passivkonto",   "SK", "EUR", true  },
   {  0, 2210, "Umsatzsteuer innergemeinschaftlicher Erwerb", "Passivkonto",   "SK", "EUR", true  },
   {  0, 2220, "Umsatzsteuer Reverse Charge",                 "Passivkonto",   "SK", "EUR", true  },
   {  0, 2300, "Darlehen kurzfristig",                        "Passivkonto",   "SK", "EUR", true  },
   {  0, 2310, "Darlehen langfristig",                        "Passivkonto",   "SK", "EUR", true  },
   {  0, 2400, "Rückstellungen Personal",                     "Passivkonto",   "SK", "EUR", true  },
   {  0, 2410, "Rückstellungen Steuern",                      "Passivkonto",   "SK", "EUR", true  },
   {  0, 2500, "Passive Rechnungsabgrenzung",                 "Passivkonto",   "SK", "EUR", true  },
   {  0, 2600, "Eigenkapital",                                "Passivkonto",   "SK", "EUR", true  },

   {  0, 4000, "Umsatzerlöse Standard",                       "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4010, "Umsatzerlöse Beratung",                       "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4020, "Umsatzerlöse Software",                       "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4030, "Umsatzerlöse Wartung",                        "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4100, "Sonstige betriebliche Erträge",               "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4110, "Zinserträge",                                 "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4120, "Skontoerträge",                               "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4130, "Kursgewinne",                                 "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4140, "Mieterträge",                                 "Ertragskonto",  "SK", "EUR", true  },
   {  0, 4150, "Erträge aus Anlagenabgang",                   "Ertragskonto",  "SK", "EUR", true  },

   {  0, 5000, "Materialaufwand",                             "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5010, "Wareneinsatz",                                "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5020, "Fremdleistungen",                             "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5100, "Personalaufwand Gehälter",                    "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5110, "Personalaufwand Sozialabgaben",               "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5200, "Miete",                                       "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5210, "Energie",                                     "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5220, "Telekommunikation",                           "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5230, "Versicherungen",                              "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5240, "Reisekosten",                                 "Aufwandskonto", "SK", "EUR", true  },

   {  0, 5300, "Marketing",                                   "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5310, "Fortbildung",                                 "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5320, "Rechts- und Beratungskosten",                 "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5330, "Abschreibungen",                              "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5340, "Bankgebühren",                                "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5350, "Zinsaufwand",                                 "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5360, "Kursverluste",                                "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5370, "Büromaterial",                                "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5380, "Bewirtung",                                   "Aufwandskonto", "SK", "EUR", true  },
   {  0, 5390, "Sonstiger Aufwand",                           "Aufwandskonto", "SK", "EUR", false }
};

inline std::vector<tax_class_ty> const vecTaxClasses {
   { 0, "UST_19",       "Umsatzsteuer 19 Prozent Inland",               19.00,   {}, 2200, false, true},
   { 0, "UST_7",        "Umsatzsteuer 7 Prozent Inland",                 7.00,   {}, 2207, false, true  },
   { 0, "UST_0",        "Umsatzsteuerfrei Inland",                       0.00,   {},   {}, false, false },
   { 0, "UST_0_EU",     "Innergemeinschaftliche Lieferung steuerfrei",   0.00,   {},   {}, false, false },
   { 0, "UST_0_EXPORT", "Ausfuhrlieferung Drittland steuerfrei",         0.00,   {},   {}, false, false },

   { 0, "VST_19",       "Vorsteuer 19 Prozent Inland",                  19.00, 1400,   {}, true,  false },
   { 0, "VST_7",        "Vorsteuer 7 Prozent Inland",                    7.00, 1407,   {}, true,  false },
   { 0, "VST_0",        "Vorsteuerfrei Inland",                          0.00,   {},   {}, false, false },

   { 0, "VST_0_EU_RC",  "EU Reverse Charge Eingangsl. ohne Steuerb.",    0.00,   {},    {}, false, false},

   { 0, "RC_EU_19",     "Reverse Charge EU Leistung 19 Prozent",        19.00, 1400, 2220, true,  true  },
   { 0, "IG_E_19",      "Innergemeinschaftlicher Erwerb 19 Prozent",    19.00, 1410, 2210, true,  true  },

   { 0, "NONE",         "Nicht steuerrelevante Zahlung oder Umbuchung",  0.00,   {},   {}, false, false }
};

inline std::vector<entry_raw_ty> const vecPostings {
   {  0, "2026-01-05", "RE-10001", 1100, 4000,  1190.00, "UST_19",       "Ausgangsrechnung Standardleistung" },
   {  0, "2026-01-08", "RE-10002", 1100, 4010,  2380.00, "UST_19",       "Ausgangsrechnung Beratung" },
   {  0, "2026-01-10", "RE-10003", 1110, 4020,  3570.00, "UST_0_EXPORT", "Ausgangsrechnung Software Ausland" },
   {  0, "2026-01-12", "ZA-20001", 1000, 1100,  1190.00, "NONE",         "Zahlungseingang Kunde Inland" },
   {  0, "2026-01-14", "ZA-20002", 1000, 1100,  2380.00, "NONE",         "Zahlungseingang Beratung" },

   {  0, "2026-01-15", "ER-30001", 5000, 2000,   350.00, "VST_19",       "Eingangsrechnung Material" },
   {  0, "2026-01-16", "ER-30002", 5010, 2000,   820.00, "VST_19",       "Eingangsrechnung Handelswaren" },
   {  0, "2026-01-18", "ER-30003", 5020, 2010,  1250.00, "VST_0_EU_RC",  "Eingangsrechnung Fremdleistung Ausland" },
   {  0, "2026-01-20", "ZA-20003", 2000, 1000,   350.00, "NONE",         "Zahlung Lieferant Material" },
   {  0, "2026-01-22", "ZA-20004", 2000, 1000,   820.00, "NONE",         "Zahlung Lieferant Handelswaren" },

   {  0, "2026-02-01", "RE-10004", 1100, 4030,  1785.00, "UST_19",       "Ausgangsrechnung Wartung" },
   {  0, "2026-02-03", "RE-10005", 1100, 4000,  5950.00, "UST_19",       "Ausgangsrechnung Projektlieferung" },
   {  0, "2026-02-05", "ZA-20005", 1000, 1100,  1785.00, "NONE",         "Zahlungseingang Wartung" },
   {  0, "2026-02-07", "ER-30004", 5100, 2000,  2100.00, "VST_19",       "Monatsmiete Büro" },
   {  0, "2026-02-08", "ER-30005", 5210, 2000,   640.00, "VST_19",       "Strom und Energie" },

   {  0, "2026-02-10", "ZA-20006", 2000, 1000,  2100.00, "NONE",         "Zahlung Monatsmiete" },
   {  0, "2026-02-12", "ZA-20007", 2000, 1000,   640.00, "NONE",         "Zahlung Energieversorger" },
   {  0, "2026-02-14", "BN-40001", 5340, 1000,    18.50, "VST_0",        "Bankgebühren Januar" },
   {  0, "2026-02-15", "RE-10006", 1110, 4020,  4284.00, "UST_0_EU",     "Ausgangsrechnung Softwarelizenz" },
   {  0, "2026-02-17", "ZA-20008", 1000, 1110,  3570.00, "NONE",         "Zahlungseingang Auslandskunde" },

   {  0, "2026-03-01", "GE-50001", 5100, 1000,  9000.00, "NONE",         "Gehaltslauf März" },
   {  0, "2026-03-01", "GE-50002", 5110, 1000,  1800.00, "NONE",         "Sozialabgaben März" },
   {  0, "2026-03-03", "RE-10007", 1100, 4010,  2975.00, "UST_19",       "Beratungsleistung Sprint Review" },
   {  0, "2026-03-04", "RE-10008", 1100, 4030,   595.00, "UST_19",       "Wartungsvertrag Kunde A" },
   {  0, "2026-03-05", "ZA-20009", 1000, 1100,  2975.00, "NONE",         "Zahlungseingang Beratung Sprint Review" },

   {  0, "2026-03-07", "ER-30006", 5220, 2000,   220.00, "VST_19",       "Telekommunikation" },
   {  0, "2026-03-08", "ER-30007", 5230, 2000,   480.00, "VST_0",        "Versicherung Betrieb" },
   {  0, "2026-03-10", "ZA-20010", 2000, 1000,   220.00, "NONE",         "Zahlung Telekommunikation" },
   {  0, "2026-03-11", "ZA-20011", 2000, 1000,   480.00, "NONE",         "Zahlung Versicherung" },
   {  0, "2026-03-12", "SK-60001", 2000, 4120,    25.00, "VST_19",       "Skonto Lieferant" },

   {  0, "2026-04-01", "RE-10009", 1100, 4000,  7140.00, "UST_19",       "Ausgangsrechnung Implementierung" },
   {  0, "2026-04-02", "RE-10010", 1110, 4020,  8568.00, "UST_0_EU",     "Ausgangsrechnung Softwarepaket Ausland" },
   {  0, "2026-04-05", "ZA-20012", 1000, 1100,  7140.00, "NONE",         "Zahlungseingang Implementierung" },
   {  0, "2026-04-07", "ER-30008", 5300, 2000,  1500.00, "VST_19",       "Marketingkampagne" },
   {  0, "2026-04-08", "ER-30009", 5310, 2000,   950.00, "VST_19",       "Fortbildung Architekturteam" },

   {  0, "2026-04-10", "ZA-20013", 2000, 1000,  1500.00, "NONE",         "Zahlung Marketing" },
   {  0, "2026-04-12", "ZA-20014", 2000, 1000,   950.00, "NONE",         "Zahlung Fortbildung" },
   {  0, "2026-04-14", "ER-30010", 5320, 2000,  1320.00, "VST_19",       "Rechtsberatung Vertrag" },
   {  0, "2026-04-15", "ER-30011", 5370, 2000,   145.00, "VST_19",       "Büromaterial" },
   {  0, "2026-04-16", "ZA-20015", 2000, 1000,  1320.00, "NONE",         "Zahlung Rechtsberatung" },

   {  0, "2026-05-01", "GE-50003", 5100, 1000,  9000.00, "NONE",         "Gehaltslauf Mai" },
   {  0, "2026-05-01", "GE-50004", 5110, 1000,  1800.00, "NONE",         "Sozialabgaben Mai" },
   {  0, "2026-05-03", "RE-10011", 1100, 4010,  4165.00, "UST_19",       "Ausgangsrechnung Architekturberatung" },
   {  0, "2026-05-04", "RE-10012", 1100, 4030,   595.00, "UST_19",       "Wartungsvertrag Kunde B" },
   {  0, "2026-05-06", "ZA-20016", 1000, 1100,  4165.00, "NONE",         "Zahlungseingang Architekturberatung" },

   {  0, "2026-05-08", "ER-30012", 5240, 2000,   780.00, "VST_19",       "Reisekosten Kundentermin" },
   {  0, "2026-05-09", "ER-30013", 5380, 2000,   260.00, "VST_19",       "Bewirtung Geschäftspartner" },
   {  0, "2026-05-10", "ZA-20017", 2000, 1000,   780.00, "NONE",         "Zahlung Reisekostenabrechnung" },
   {  0, "2026-05-11", "BN-40002", 5340, 1000,    21.75, "VST_0",        "Bankgebühren April" },
   {  0, "2026-05-12", "ZA-20018", 1000, 1110,  4284.00, "NONE",         "Zahlungseingang Softwarelizenz Ausland" }
};
} // namespace extented_test {