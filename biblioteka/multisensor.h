/**
 * @file    multisensor.h
 * @brief   Biblioteka nakładki KA-Nucleo-Multisensor dla NUCLEO-G491RE (HAL, STM32CubeIDE).
 *
 * Instalacja: multisensor.h → Core/Inc, multisensor.c → Core/Src.
 * Użycie:     #include "multisensor.h", a w main() po funkcjach MX_..._Init():  MS_Init();
 *
 * Biblioteka sama konfiguruje piny, których używa (wyświetlacz, diody RGB, 1-Wire,
 * programowe I2C, SPI1 akcelerometru). Nie korzysta z przerwań ani z liczników.
 * Po dodaniu FreeRTOS czekanie na wynik pomiaru temperatury oddaje procesor
 * (vTaskDelay), a bez FreeRTOS jest to HAL_Delay.
 *
 * Funkcje czujników są blokujące i nie są chronione przed jednoczesnym użyciem
 * z kilku zadań – czujnik powinno obsługiwać jedno zadanie albo mutex.
 */
#ifndef MULTISENSOR_H
#define MULTISENSOR_H

#include "main.h"      /* stm32g4xx_hal.h: HAL_StatusTypeDef, HAL_Delay */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ inicjalizacja */

/** Konfiguracja pinów i czujników nakładki. Wywołać raz, po funkcjach MX_..._Init(). */
void MS_Init(void);

/* ------------------------------------------------------------------ czujniki */

/** Temperatura z termometru DS18B20 w 0,1 °C (np. 215 = 21,5 °C). Pomiar trwa ok. 750 ms. */
HAL_StatusTypeDef TEMP_Read(int16_t *temp);

/** Ciśnienie z barometru LPS25HB w 0,1 hPa (np. 10132 = 1013,2 hPa) i temperatura w 0,1 °C.
 *  Barometr mierzy w sposób ciągły 7 razy na sekundę; wskaźnik temp może być NULL. */
HAL_StatusTypeDef BARO_Read(uint16_t *press, int16_t *temp);

/** Przyspieszenie w osiach X, Y, Z z akcelerometru LIS35DE w mg (zakres ±2 g, 100 pomiarów/s).
 *  Płytka leżąca płasko: x ≈ 0, y ≈ 0, z ≈ 1000. */
HAL_StatusTypeDef ACC_Read(int16_t *x, int16_t *y, int16_t *z);

/* ------------------------------------------------------------------ diody RGB */

enum rgbColor { RGB_RED = 0, RGB_GREEN = 1, RGB_BLUE = 2 };

/** Zapala diodę led (0–3) w jednym kolorze; pozostałe diody gasną.
 *  Diody są połączone metodą charlieplexingu: jednocześnie może świecić tylko jedna. */
void RGB_Set(uint8_t led, enum rgbColor color);

/** Gasi wszystkie diody RGB. */
void RGB_Off(void);

/* ------------------------------------------------------------------ wyświetlacz */

/** Segmenty (maska bitowa, można łączyć operatorem |). Stan wysoki zapala segment. */
enum dispSeg {
  DISP_SEG_NONE = 0x00,
  DISP_SEG_A = 0x01, DISP_SEG_B = 0x02, DISP_SEG_C = 0x04, DISP_SEG_D = 0x08,
  DISP_SEG_E = 0x10, DISP_SEG_F = 0x20, DISP_SEG_G = 0x40, DISP_SEG_DP = 0x80
};

/** Pozycje (cyfry) wyświetlacza (maska bitowa, można łączyć operatorem |).
 *  Wybrana pozycja dostaje na wyjściu COM stan niski. */
enum dispPos {
  DISP_POS_NONE = 0x00,
  DISP_POS_1 = 0x01, DISP_POS_2 = 0x02, DISP_POS_3 = 0x04, DISP_POS_4 = 0x08,
  DISP_POS_ALL = 0x0F
};

/** Ustawia stan wszystkich linii segmentów A–G i DP: zapalone są segmenty z maski seg. */
void DISP_SetSegments(enum dispSeg seg);

/** Ustawia stan wyjść COM1–COM4: aktywne są pozycje z maski pos, pozostałe wyłączone. */
void DISP_SetPositions(enum dispPos pos);

/** Ustawia segmenty cyfry 0–9 (inna wartość gasi segmenty). Pozycje wybiera DISP_SetPositions().
 *  Cyfra ma do 7 segmentów – zobacz UWAGA (prąd) poniżej. */
void DISP_SetDigit(uint8_t digit);

/** Maska segmentów cyfry 0–9 (inna wartość: DISP_SEG_NONE), bez zapalania czegokolwiek.
 *  Przy multipleksowaniu cyfrę zapala się w częściach: DISP_DigitSegments(d) & DISP_GROUP_1 itd. */
enum dispSeg DISP_DigitSegments(uint8_t digit);

/** Grupy po najwyżej 3 segmenty: A–C, D–F, G i DP. */
#define DISP_GROUP_1  (DISP_SEG_A | DISP_SEG_B | DISP_SEG_C)
#define DISP_GROUP_2  (DISP_SEG_D | DISP_SEG_E | DISP_SEG_F)
#define DISP_GROUP_3  (DISP_SEG_G | DISP_SEG_DP)

/* UWAGA (prąd): wyjścia COM są połączone z pinami mikrokontrolera bez tranzystorów, jeden
 * segment pobiera ok. 6 mA. W każdej chwili – także przy multipleksowaniu – zapalaj najwyżej
 * 3 segmenty na jednej pozycji i ten sam segment na najwyżej 3 pozycjach. */

#ifdef __cplusplus
}
#endif

#endif /* MULTISENSOR_H */
