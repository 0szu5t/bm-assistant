(wszystko to napisał gemini 3.1 który został poddany wielu bardzo bolesnym torturom)

Witaj w zespole! Pracujesz nad zaawansowanym prototypem robota. Abyś mógł bezpiecznie programować i nie uszkodzić sprzętu, przestrzegaj poniższych zasad.

🛠 Procedura Wgrywania Kodu (Krytyczna!)

Zasilanie robota jest potężne i może przeciążyć port USB Twojego komputera. Zawsze wgrywaj kod w ten sposób:

    Odłącz silniki: Wyciągnij przewody z pinów ENA oraz ENB w ESP32. To unieruchomi napęd. // Pierwszy i ostatni pin na mostku H, w kolorach czarny i biały, są oznaczone białą taśmą.

    Zasilanie robota: Włącz główny przełącznik akumulatorów na ON.

    Połączenie: Podłącz kabel USB do ESP32.

    Wgrywanie: W Arduino IDE kliknij "Wgraj". Gdy zobaczysz napis Connecting..., wciśnij i trzymaj przycisk BOOT na płytce ESP32, aż zobaczysz procenty postępu. // Bullshit, kliknij "wgraj" i trzymaj ten przycisk aż WSZYSTKO się skończy.

    Po wgraniu: Odłącz kabel USB, wepnij ENA i ENB na miejsce. // Jak pomylisz ich miejsca to zamorduje Cie <3

⚡️ Zasady dotyczące Pinów i Kabli

    Pajączek: Większość połączeń jest lutowana bezpośrednio ("pajączek"). Nie szarp za kable – mogą się urwać lub zrobić zwarcie. // Nic takiego tam nie ma ale bądź ostrożny, szczegolnie z latającym modułem PAM który powinien sobie latać gdzieś z przodu bo nie chciało mi się go przyklejać

    Zakaz metalu: Nigdy nie dotykaj płytki ani centrów połączeń metalowymi narzędziami (śrubokrętem, pęsetą), gdy zasilanie jest włączone.

    Izolacja: Jeśli zobaczysz goły drucik wystający z połączenia, natychmiast wyłącz prąd. // Rób co chcesz, polecam zakleić to wtedy

🔋 Traktowanie Akumulatorów (Ogniwa 18650)

    Moc: Te baterie potrafią oddać ogromny prąd. Jeśli poczujesz zapach spalenizny lub zobaczysz dym – natychmiast użyj głównego przełącznika OFF.

    Ciepło: Sprawdzaj co jakiś czas, czy akumulatory lub przetwornica LM2596 nie robią się gorące. Ciepłe są OK, gorące oznaczają błąd w kodzie lub zwarcie.

    Napięcie: Nie zostawiaj robota włączonego na wiele godzin bez nadzoru, aby nie rozładować ogniw poniżej bezpiecznego poziomu.
	

⚠️ Księga Awarii i Przypadków Beznadziejnych (TO TEŻ PRZECZYTAJ)

Jeśli robot nagle przestaje działać, zachowuje się nieprzewidywalnie lub wydaje dziwne dźwięki, zanim zaczniesz panikować – sprawdź poniższą listę najczęstszych błędów.
1. Dioda przetwornicy (LM2596) ledwo świeci, a miernik pokazuje 1.3V

    Co zrobiłeś: Doprowadziłeś do potężnego zwarcia na linii 5V. Najprawdopodobniej połączyłeś gdzieś Plus z Minusem (np. zwarłeś kabelki śrubokrętem, uszkodziłeś izolację, albo wpiąłeś zewnętrzne, gołe bolce ENA/ENB na mostku do masy).

    Co się dzieje: Główny system ratunkowy przetwornicy zobaczył ogromny skok prądu i w ułamku sekundy "zdusił" zasilanie do 1.3V, żeby zapobiec stopieniu kabli i pożarowi.

    Jak ratować: Wyłącz prąd (OFF)! Niczego nie spaliłeś. Musisz metodycznie, krok po kroku, znaleźć i usunąć fizyczne zwarcie w kablach robota.

2. Zamiana miejscami pinów ENA i ENB w ESP32 (Stan Krytyczny!) // to jest ważne jak coś

    Co zrobiłeś: Podpiąłeś napęd (tył) pod kod sterowania kierownicą, a kierownicę (przód) pod kod napędu.

    Co się dzieje fizycznie: * Tylne koła dostaną zjawisko "Kopnij i Trzymaj". Będą brutalnie szarpać przez ułamek sekundy, a potem cicho buczeć, ledwo się kręcąc.

        ZAGROŻENIE: Silnik przedni (ze sprężyną) dostanie instrukcję "kręć się jednostajnie i bez przerwy". Ponieważ sprężyna i fizyczny plastik zablokują go po 35 stopniach, silnik wejdzie w stan utyku (stall). Stanie się grzałką.

    Skutek: Jeśli tego szybko nie wyłączysz, przedni silnik przepali się po kilku minutach, a mostek L298N zagotuje się z gorąca. Zawsze potrójnie sprawdzaj te piny!

3. Podbicie mocy w kodzie do 100% (PWM = 255 dla napędu) // to jest jeszcze ważniejsze

    Co zrobiłeś: Zmieniłeś w kodzie testDriveSpeed = 150 na 255, myśląc, że robot będzie jeździł szybciej i fajniej.

    Co się dzieje: Silniki w autku są przystosowane do 3.7V. Przy wartości 255 ładujesz w nie potężne ~6.0V.

    Skutek: Robot wystartuje jak rakieta, zębatki zaczną przeraźliwie wyć, ale po 10-15 minutach takiej jazdy stopisz plastikowe obudowy silników lub całkowicie spalisz ich szczotki wewnątrz. Maksymalna bezpieczna wartość do ciągłej jazdy to okolice 150-180.

4. "Złośliwy port USB" (Błąd wgrywania, komputer nie widzi ESP32)

    Co zrobiłeś: Próbujesz wgrać kod z podpiętymi pinami ENA/ENB, ale przycisk głównego akumulatora masz na OFF.

    Co się dzieje: Gdy wgrywasz kod, a logika rusza silnikami, próbują one wyssać prąd z portu USB komputera. Twój laptop w panice odcina połączenie, przez co Arduino IDE wyrzuca błędy (np. PermissionError, No serial data received).

    Jak ratować: Pamiętaj o Żelaznej Procedurze Wgrywania: Odpinasz ENA/ENB z ESP32 na czas wgrywania, i dopiero po sukcesie wpinasz je z powrotem.

5. "Autko na podłodze traci siły, a w powietrzu działa"

    Co zrobiłeś: Nie włączyłeś akumulatora na ON, a testujesz robota podpiętego tylko do USB, mając jednak wpięte ENA/ENB.

    Co się dzieje: Zjawisko zasilania wstecznego. Silniki dostają nędzne resztki prądu (około 500mA) z portu komputera. Starcza to na zakręcenie kołami w powietrzu bez oporu, ale pod ciężarem robota na dywanie silniki natychmiastowo klękają. Włącz zasilanie (ON)!

6. Założenie z powrotem czarnej zworki "5V EN" na mostek L298N // o to sie nie martw dopuki sam nie będziesz majstrować przy kablach nic Ci nie grozi

    Co zrobiłeś: Znalazłeś zworkę na biurku i pomyślałeś, że "lepiej żeby tam była".

    Co się dzieje: Skoro zasilamy pin 5V na mostku z naszej potężnej przetwornicy LM2596 (i mamy podpięte 12V z akumulatora), ta zworka sprawiłaby, że wewnętrzny system zasilania mostka L298N zacząłby drastycznie "walczyć" o napięcie z przetwornicą.

    Skutek: Bardzo niestabilne zasilanie, dziwne zachowanie ekraniku, a w dłuższej perspektywie – ryzyko uszkodzenia mostka. Zworka musi leżeć w koszu na śmieci!

7. Katastrofa Ostateczna: Pomylenie Plusa z Minusem przy akumulatorze // Robot zabije Ciebie a jeżeli to jakimś cudem przeżyjesz to potem ja dokonam jego woli

    Co zrobiłeś: Wpiąłeś czerwony kabel do MINUSA na przetwornicy / mostku, a czarny do PLUSA.

    Co się dzieje: Odwrotna polaryzacja. Układy nie mają przed tym twardego zabezpieczenia.

    Skutek: Natychmiastowe (w ułamku sekundy) zwęglenie układu scalonego LM2596, przepalenie mostka L298N i prawdopodobne wysadzenie mikrokontrolera ESP32. Kolory kabli zasilających są absolutnie święte. Czerwony to Plus (VCC), Czarny to Minus (GND). Nigdy, przenigdy ich nie zamieniaj.