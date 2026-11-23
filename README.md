# Systemy operacyjne w automatyce — FreeRTOS w STM32CubeIDE

Instrukcje laboratoryjne FreeRTOS 1/3–3/3 dla STM32CubeIDE (Windows 10), płytki NUCLEO-G491RE
i nakładki KA-Nucleo-Multisensor.

**Instrukcje:** [mhubacz.github.io/freertos-cubeide-student](https://mhubacz.github.io/freertos-cubeide-student/)
(wersja z obserwacją pracy systemu: [/obserwacje/](https://mhubacz.github.io/freertos-cubeide-student/obserwacje/)).
Strona działa też bez internetu – wystarczy otworzyć `docs/index.html`.

| Plik | Zawartość |
|---|---|
| `projekt/KA_Multisensor_G491.ioc` | plik konfiguracyjny STM32CubeMX – od niego zaczynasz projekt z nakładką (instrukcja 2/3, zadanie 1) |
| `biblioteka/multisensor.h` | do katalogu `Core/Inc` projektu – opis wszystkich funkcji nakładki |
| `biblioteka/multisensor.c` | do katalogu `Core/Src` projektu |
| `docs/` | strony z instrukcjami |

Od instrukcji 2/3: w `main()` po funkcjach `MX_..._Init()` wywołaj `MS_Init();`.

Przełącznik *Sprzęt* w menu wybiera wariant instrukcji: sama płytka Nucleo w instrukcji 2/3 albo z nakładką od 2/3 – ustaw ten, który wskazał prowadzący.

Pod każdym zadaniem na stronie wpisujesz kod lub odpowiedź; odpowiedzi zapisują się w przeglądarce.
Na koniec zajęć wygeneruj sprawozdanie (PDF) i prześlij je na [sprawozdania.kia.prz.edu.pl](https://sprawozdania.kia.prz.edu.pl).
