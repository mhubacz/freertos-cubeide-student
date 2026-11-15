/**
 * @file    multisensor.c
 * @brief   Biblioteka nakładki KA-Nucleo-Multisensor dla NUCLEO-G491RE (opis funkcji w multisensor.h).
 *
 * Sterowniki przeniesione z firmware testowego KA_Multisensor_G491 (sprawdzonego na płytce):
 *  - DS18B20 (1-Wire, PB4)       – szczeliny czasowe liczone licznikiem cykli DWT,
 *  - LPS25HB (I2C, PB10/PB11)    – I2C programowe: STM32G491 nie ma sprzętowego I2C na tych pinach,
 *  - LIS35DE (SPI1, CS PA4)      – SPI w trybie 3, 5,3 MHz,
 *  - diody RGB (PB6–PB9)         – charlieplexing: dioda świeci, gdy anoda = 1, katoda = 0,
 *                                  pozostałe linie w stanie wysokiej impedancji,
 *  - wyświetlacz (PC0–PC11)      – bezpośrednie sterowanie liniami segmentów i COM.
 *
 * Piny konfiguruje MS_Init() – biblioteka działa także w projekcie, w którym CubeMX
 * tych pinów nie ustawił.
 */
#include "multisensor.h"

#if defined(__has_include)
#if __has_include("FreeRTOS.h") && __has_include("task.h")
#include "FreeRTOS.h"
#include "task.h"
#if (INCLUDE_xTaskGetSchedulerState == 1) || (configUSE_TIMERS == 1)
#define MS_RTOS 1
#endif
#endif
#endif

/* ------------------------------------------------------------------ czas */

static void ms_delay(uint32_t ms)
{
#ifdef MS_RTOS
  if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
  {
    const TickType_t t = pdMS_TO_TICKS(ms);
    vTaskDelay((t > 0U) ? t : 1U);      /* zadanie czeka, procesor wolny */
    return;
  }
#endif
  HAL_Delay(ms);
}

static void delay_us(uint32_t us)
{
  /* DWT->CYCCNT liczy cykle zegara rdzenia (170 MHz → 170 cykli na mikrosekundę). */
  const uint32_t start = DWT->CYCCNT;
  const uint32_t ticks = us * (SystemCoreClock / 1000000U);
  while ((DWT->CYCCNT - start) < ticks)
  {
  }
}

/* ------------------------------------------------------------------ wyświetlacz */

/* Piny portu C: segmenty A, B, C, D, E, F, G, DP oraz wyjścia COM1–COM4.
 * Kolejność COM (która pozycja jest pierwsza od lewej) – do sprawdzenia na płytce:
 * DISP_SetPositions(DISP_POS_1) powinno zapalić skrajną lewą cyfrę. */
static const uint8_t k_seg_pin[8] = { 6U, 4U, 3U, 1U, 2U, 5U, 0U, 7U };
static const uint8_t k_com_pin[4] = { 11U, 10U, 9U, 8U };

static const uint8_t k_digit[10] = {
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_C | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F,               /* 0 */
  DISP_SEG_B | DISP_SEG_C,                                                                 /* 1 */
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_D | DISP_SEG_E | DISP_SEG_G,                          /* 2 */
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_C | DISP_SEG_D | DISP_SEG_G,                          /* 3 */
  DISP_SEG_B | DISP_SEG_C | DISP_SEG_F | DISP_SEG_G,                                       /* 4 */
  DISP_SEG_A | DISP_SEG_C | DISP_SEG_D | DISP_SEG_F | DISP_SEG_G,                          /* 5 */
  DISP_SEG_A | DISP_SEG_C | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F | DISP_SEG_G,             /* 6 */
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_C,                                                    /* 7 */
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_C | DISP_SEG_D | DISP_SEG_E | DISP_SEG_F | DISP_SEG_G,/* 8 */
  DISP_SEG_A | DISP_SEG_B | DISP_SEG_C | DISP_SEG_D | DISP_SEG_F | DISP_SEG_G,             /* 9 */
};

void DISP_SetSegments(enum dispSeg seg)
{
  uint32_t set = 0U;
  uint32_t reset = 0U;
  for (uint32_t i = 0U; i < 8U; i++)
  {
    if (((uint32_t)seg & (1UL << i)) != 0U)
    {
      set |= 1UL << k_seg_pin[i];
    }
    else
    {
      reset |= 1UL << k_seg_pin[i];
    }
  }
  GPIOC->BSRR = set | (reset << 16);     /* jeden zapis – wszystkie linie naraz */
}

void DISP_SetPositions(enum dispPos pos)
{
  uint32_t set = 0U;
  uint32_t reset = 0U;
  for (uint32_t i = 0U; i < 4U; i++)
  {
    if (((uint32_t)pos & (1UL << i)) != 0U)
    {
      reset |= 1UL << k_com_pin[i];     /* aktywna pozycja: COM w stanie niskim */
    }
    else
    {
      set |= 1UL << k_com_pin[i];
    }
  }
  GPIOC->BSRR = set | (reset << 16);
}

enum dispSeg DISP_DigitSegments(uint8_t digit)
{
  return (enum dispSeg)((digit < 10U) ? k_digit[digit] : DISP_SEG_NONE);
}

void DISP_SetDigit(uint8_t digit)
{
  DISP_SetSegments(DISP_DigitSegments(digit));
}

/* ------------------------------------------------------------------ diody RGB */

#define RGB_LINE0  6U                         /* linie L0..L3 = PB6..PB9 */
#define RGB_MODER  (0xFFUL << (RGB_LINE0 * 2U))

/* Numer kombinacji (anoda, katoda) dla [dioda][kolor]. Kombinacja n: anoda = n / 3,
 * katoda = (n % 3)-ta z pozostałych linii. Mapa do sprawdzenia na płytce
 * (test RGB_MAP w firmware KA_Multisensor_G491). */
static const uint8_t k_rgb_map[4][3] = {
  /*        R   G   B */
  /* 0 */ { 0,  1,  2 },
  /* 1 */ { 3,  4,  5 },
  /* 2 */ { 6,  7,  8 },
  /* 3 */ { 9, 10, 11 },
};

/* Rejestr MODER portu B jest zmieniany operacją odczyt-modyfikacja-zapis. Sekcja krytyczna
 * (krótka, bez wywołań systemu) chroni ją, gdy RGB_Set/RGB_Off wywołuje kilka zadań FreeRTOS. */
static void rgb_off_unsafe(void)
{
  GPIOB->MODER &= ~RGB_MODER;                 /* wszystkie linie jako wejścia (Hi-Z) */
  GPIOB->BSRR = (0x0FUL << RGB_LINE0) << 16;  /* ODR = 0 */
}

void RGB_Off(void)
{
  const uint32_t pm = __get_PRIMASK();
  __disable_irq();
  rgb_off_unsafe();
  __set_PRIMASK(pm);
}

void RGB_Set(uint8_t led, enum rgbColor color)
{
  if ((led > 3U) || ((uint32_t)color > 2U))
  {
    RGB_Off();
    return;
  }
  const uint8_t n = k_rgb_map[led][color];
  const uint32_t a = n / 3U;
  uint32_t k = n % 3U;
  if (k >= a)
  {
    k++;
  }
  const uint32_t pa = RGB_LINE0 + a;
  const uint32_t pk = RGB_LINE0 + k;
  const uint32_t pm = __get_PRIMASK();
  __disable_irq();
  rgb_off_unsafe();
  GPIOB->BSRR = (1UL << pa) | (1UL << (pk + 16U));            /* najpierw poziomy...   */
  GPIOB->MODER |= (1UL << (pa * 2U)) | (1UL << (pk * 2U));    /* ...potem dwa wyjścia  */
  __set_PRIMASK(pm);
}

/* ------------------------------------------------------------------ 1-Wire, DS18B20 (PB4) */

#define OW_PIN 4U

static inline void ow_low(void)     { GPIOB->BSRR = 1UL << (OW_PIN + 16U); }
static inline void ow_release(void) { GPIOB->BSRR = 1UL << OW_PIN; }
static inline uint32_t ow_level(void) { return (GPIOB->IDR >> OW_PIN) & 1UL; }

/* Szczeliny 1-Wire trwają kilkadziesiąt mikrosekund; przerwanie w środku szczeliny
 * zmieniłoby bit, dlatego wykonujemy je z zablokowanymi przerwaniami. */
static int ow_reset(void)
{
  ow_low();
  delay_us(480U);
  const uint32_t pm = __get_PRIMASK();
  __disable_irq();
  ow_release();
  delay_us(70U);
  const int presence = (ow_level() == 0U);
  __set_PRIMASK(pm);
  delay_us(410U);
  return presence;
}

static void ow_write_bit(uint32_t bit)
{
  const uint32_t pm = __get_PRIMASK();
  __disable_irq();
  ow_low();
  delay_us(bit ? 6U : 60U);
  ow_release();
  __set_PRIMASK(pm);
  delay_us(bit ? 64U : 10U);
}

static uint32_t ow_read_bit(void)
{
  const uint32_t pm = __get_PRIMASK();
  __disable_irq();
  ow_low();
  delay_us(6U);
  ow_release();
  delay_us(9U);
  const uint32_t bit = ow_level();
  __set_PRIMASK(pm);
  delay_us(55U);
  return bit;
}

static void ow_write_byte(uint8_t b)
{
  for (uint32_t i = 0U; i < 8U; i++)            /* najmłodszy bit pierwszy */
  {
    ow_write_bit(b & 1U);
    b >>= 1;
  }
}

static uint8_t ow_read_byte(void)
{
  uint8_t b = 0U;
  for (uint32_t i = 0U; i < 8U; i++)
  {
    b = (uint8_t)((b >> 1) | (ow_read_bit() << 7));
  }
  return b;
}

static uint8_t ow_crc8(const uint8_t *d, uint32_t len)
{
  uint8_t crc = 0U;
  for (uint32_t i = 0U; i < len; i++)
  {
    uint8_t b = d[i];
    for (uint32_t j = 0U; j < 8U; j++)
    {
      const uint8_t mix = (uint8_t)((crc ^ b) & 1U);
      crc >>= 1;
      if (mix != 0U)
      {
        crc ^= 0x8CU;
      }
      b >>= 1;
    }
  }
  return crc;
}

HAL_StatusTypeDef TEMP_Read(int16_t *temp)
{
  uint8_t sp[9];
  if (temp == NULL)
  {
    return HAL_ERROR;
  }
  if (!ow_reset())
  {
    return HAL_ERROR;                            /* brak impulsu obecności */
  }
  ow_write_byte(0xCCU);                          /* SKIP ROM – jeden układ na linii */
  ow_write_byte(0x44U);                          /* CONVERT T */
  const uint32_t t0 = HAL_GetTick();
  do
  {
    ms_delay(10U);
    if ((HAL_GetTick() - t0) > 1000U)
    {
      return HAL_TIMEOUT;
    }
  } while (ow_read_bit() == 0U);                 /* w trakcie pomiaru układ odpowiada zerem */

  if (!ow_reset())
  {
    return HAL_ERROR;
  }
  ow_write_byte(0xCCU);
  ow_write_byte(0xBEU);                          /* READ SCRATCHPAD */
  for (uint32_t i = 0U; i < 9U; i++)
  {
    sp[i] = ow_read_byte();
  }
  if (ow_crc8(sp, 8U) != sp[8])
  {
    return HAL_ERROR;
  }
  const int16_t raw = (int16_t)(((uint16_t)sp[1] << 8) | sp[0]);   /* 1/16 °C */
  *temp = (int16_t)(((int32_t)raw * 10) / 16);
  return HAL_OK;
}

/* ------------------------------------------------------------------ I2C programowe, LPS25HB */

#define SCL_PIN   10U
#define SDA_PIN   11U
#define HALF_US   5U                             /* ~100 kHz */
#define LPS_ADDR  0x5CU

static inline void scl_high(void) { GPIOB->BSRR = 1UL << SCL_PIN; }
static inline void scl_low(void)  { GPIOB->BSRR = 1UL << (SCL_PIN + 16U); }
static inline void sda_high(void) { GPIOB->BSRR = 1UL << SDA_PIN; }
static inline void sda_low(void)  { GPIOB->BSRR = 1UL << (SDA_PIN + 16U); }
static inline uint32_t sda_read(void) { return (GPIOB->IDR >> SDA_PIN) & 1UL; }

static void scl_release(void)
{
  scl_high();
  const uint32_t t0 = DWT->CYCCNT;               /* układ może przytrzymać SCL (clock stretching) */
  while ((((GPIOB->IDR >> SCL_PIN) & 1UL) == 0U) &&
         ((DWT->CYCCNT - t0) < 1000U * (SystemCoreClock / 1000000U)))
  {
  }
}

static void i2c_start(void)
{
  sda_high(); scl_release(); delay_us(HALF_US);
  sda_low();  delay_us(HALF_US);
  scl_low();
}

static void i2c_stop(void)
{
  sda_low();  delay_us(HALF_US);
  scl_release(); delay_us(HALF_US);
  sda_high(); delay_us(HALF_US);
}

static int i2c_write(uint8_t b)
{
  for (uint32_t i = 0U; i < 8U; i++)
  {
    if ((b & 0x80U) != 0U) { sda_high(); } else { sda_low(); }
    b = (uint8_t)(b << 1);
    delay_us(HALF_US); scl_release(); delay_us(HALF_US); scl_low();
  }
  sda_high();
  delay_us(HALF_US); scl_release();
  const int ack = (sda_read() == 0U);
  delay_us(HALF_US); scl_low();
  return ack;
}

static uint8_t i2c_read(int ack)
{
  uint8_t b = 0U;
  sda_high();
  for (uint32_t i = 0U; i < 8U; i++)
  {
    delay_us(HALF_US); scl_release();
    b = (uint8_t)((b << 1) | sda_read());
    delay_us(HALF_US); scl_low();
  }
  if (ack) { sda_low(); } else { sda_high(); }
  delay_us(HALF_US); scl_release(); delay_us(HALF_US); scl_low();
  sda_high();
  return b;
}

static HAL_StatusTypeDef lps_write(uint8_t reg, uint8_t val)
{
  i2c_start();
  const int ok = i2c_write(LPS_ADDR << 1) && i2c_write(reg) && i2c_write(val);
  i2c_stop();
  return ok ? HAL_OK : HAL_ERROR;
}

static HAL_StatusTypeDef lps_read(uint8_t reg, uint8_t *buf, uint32_t len)
{
  i2c_start();
  int ok = i2c_write(LPS_ADDR << 1) && i2c_write((uint8_t)(reg | 0x80U));  /* bit 7: autoinkrementacja */
  if (ok)
  {
    i2c_start();                                 /* powtórzony START – odczyt */
    ok = i2c_write((uint8_t)((LPS_ADDR << 1) | 1U));
  }
  for (uint32_t i = 0U; ok && (i < len); i++)
  {
    buf[i] = i2c_read(i < (len - 1U));           /* ostatni bajt bez potwierdzenia */
  }
  i2c_stop();
  return ok ? HAL_OK : HAL_ERROR;
}

HAL_StatusTypeDef BARO_Read(uint16_t *press, int16_t *temp)
{
  uint8_t b[5];
  if (press == NULL)
  {
    return HAL_ERROR;
  }
  if (lps_read(0x28U, b, 5U) != HAL_OK)          /* PRESS_OUT_XL..H, TEMP_OUT_L..H */
  {
    return HAL_ERROR;
  }
  const uint32_t p = ((uint32_t)b[2] << 16) | ((uint32_t)b[1] << 8) | b[0];   /* 1/4096 hPa */
  const int16_t t = (int16_t)(((uint16_t)b[4] << 8) | b[3]);                   /* 42,5 °C + t/480 */
  *press = (uint16_t)((p * 10U) / 4096U);
  if (temp != NULL)
  {
    *temp = (int16_t)(425 + t / 48);
  }
  return HAL_OK;
}

/* ------------------------------------------------------------------ SPI1, LIS35DE */

#define ACC_CS_PIN 4U                            /* PA4 */

static uint8_t spi_xfer(uint8_t tx)
{
  while ((SPI1->SR & SPI_SR_TXE) == 0U)
  {
  }
  *(volatile uint8_t *)&SPI1->DR = tx;           /* zapis 8-bitowy – jedna ramka */
  while ((SPI1->SR & SPI_SR_RXNE) == 0U)
  {
  }
  return *(volatile uint8_t *)&SPI1->DR;
}

static void acc_cs(int active)
{
  if (active)
  {
    GPIOA->BSRR = 1UL << (ACC_CS_PIN + 16U);
  }
  else
  {
    while ((SPI1->SR & SPI_SR_BSY) != 0U)
    {
    }
    GPIOA->BSRR = 1UL << ACC_CS_PIN;
  }
}

static void acc_write(uint8_t reg, uint8_t val)
{
  acc_cs(1);
  (void)spi_xfer(reg & 0x3FU);
  (void)spi_xfer(val);
  acc_cs(0);
}

HAL_StatusTypeDef ACC_Read(int16_t *x, int16_t *y, int16_t *z)
{
  uint8_t b[5];
  if ((x == NULL) || (y == NULL) || (z == NULL))
  {
    return HAL_ERROR;
  }
  acc_cs(1);
  (void)spi_xfer(0x80U | 0x40U | 0x29U);         /* odczyt, autoinkrementacja, OUT_X */
  for (uint32_t i = 0U; i < 5U; i++)             /* OUT_X, -, OUT_Y, -, OUT_Z */
  {
    b[i] = spi_xfer(0x00U);
  }
  acc_cs(0);
  *x = (int16_t)((int8_t)b[0] * 18);             /* 18 mg na jednostkę przy ±2 g */
  *y = (int16_t)((int8_t)b[2] * 18);
  *z = (int16_t)((int8_t)b[4] * 18);
  return HAL_OK;
}

/* ------------------------------------------------------------------ inicjalizacja */

static void pin(GPIO_TypeDef *port, uint32_t pins, uint32_t mode, uint32_t alt)
{
  GPIO_InitTypeDef g = {0};
  g.Pin = pins;
  g.Mode = mode;
  g.Pull = GPIO_NOPULL;
  g.Speed = GPIO_SPEED_FREQ_LOW;
  g.Alternate = alt;
  HAL_GPIO_Init(port, &g);
}

void MS_Init(void)
{
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_PWR_CLK_ENABLE();
  __HAL_RCC_SPI1_CLK_ENABLE();

  /* PB4 i PB6 po resecie mogą mieć wewnętrzny pull-down kontrolera USB-C (UCPD). */
  PWR->CR3 |= PWR_CR3_UCPD_DBDIS;

  /* Licznik cykli DWT do opóźnień mikrosekundowych. */
  CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
  DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

  /* Wyświetlacz: wszystko zgaszone, potem wyjścia PC0–PC11. */
  DISP_SetSegments(DISP_SEG_NONE);
  DISP_SetPositions(DISP_POS_NONE);
  pin(GPIOC, 0x0FFFU, GPIO_MODE_OUTPUT_PP, 0U);

  /* Diody RGB: linie PB6–PB9 w stanie wysokiej impedancji. */
  RGB_Off();
  GPIOB->PUPDR &= ~RGB_MODER;

  /* 1-Wire PB4 oraz I2C PB10/PB11: open-drain, linia zwolniona (pull-upy na nakładce). */
  GPIOB->BSRR = (1UL << OW_PIN) | (1UL << SCL_PIN) | (1UL << SDA_PIN);
  pin(GPIOB, (1UL << OW_PIN) | (1UL << SCL_PIN) | (1UL << SDA_PIN), GPIO_MODE_OUTPUT_OD, 0U);

  /* SPI1: PB3 SCK, PA6 MISO, PA7 MOSI (AF5), PA4 CS; master, tryb 3, 170 MHz / 32. */
  GPIOA->BSRR = 1UL << ACC_CS_PIN;
  pin(GPIOA, 1UL << ACC_CS_PIN, GPIO_MODE_OUTPUT_PP, 0U);
  pin(GPIOB, GPIO_PIN_3, GPIO_MODE_AF_PP, GPIO_AF5_SPI1);
  pin(GPIOA, GPIO_PIN_6 | GPIO_PIN_7, GPIO_MODE_AF_PP, GPIO_AF5_SPI1);
  SPI1->CR1 = 0U;
  SPI1->CR1 = SPI_CR1_MSTR | SPI_CR1_CPOL | SPI_CR1_CPHA | (4UL << SPI_CR1_BR_Pos) | SPI_CR1_SSM | SPI_CR1_SSI;
  SPI1->CR2 = (7UL << SPI_CR2_DS_Pos) | SPI_CR2_FRXTH;
  SPI1->CR1 |= SPI_CR1_SPE;

  /* Odblokowanie magistrali I2C (do 9 taktów SCL), potem STOP. */
  for (uint32_t i = 0U; (i < 9U) && (sda_read() == 0U); i++)
  {
    scl_low(); delay_us(HALF_US); scl_release(); delay_us(HALF_US);
  }
  i2c_stop();

  /* LIS35DE: przeładowanie kalibracji, potem 100 Hz, ±2 g, osie X, Y, Z. */
  acc_write(0x21U, 0x40U);                       /* CTRL_REG2: BOOT */
  HAL_Delay(5U);
  acc_write(0x20U, 0x47U);                       /* CTRL_REG1: PD | Zen | Yen | Xen */

  /* LPS25HB: przeładowanie kalibracji, potem pomiar ciągły 7 Hz z BDU. */
  (void)lps_write(0x21U, 0x80U);                 /* CTRL_REG2: BOOT */
  HAL_Delay(5U);
  (void)lps_write(0x20U, 0xA4U);                 /* CTRL_REG1: PD | ODR 7 Hz | BDU */
  HAL_Delay(200U);                               /* pierwsze wyniki obu czujników */
}
