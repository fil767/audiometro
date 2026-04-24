/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    main.c
  * @brief   Audiometro base – B-L475E-IOT01A
  *
  * COME FUNZIONA (versione semplificata, un solo orecchio):
  *
  *  1. Viene generata una tabella (LUT) con i valori di una sinusoide.
  *  2. TIM4 scandisce la LUT tramite DMA → DAC → uscita analogica su PA4.
  *     Cambiando il periodo di TIM4 si cambia la frequenza del suono.
  *  3. TIM2 scatta ogni 100 ms e aumenta il volume (gain) poco alla volta.
  *  4. Quando l'utente sente il suono, preme il pulsante (PC13).
  *     Il valore di gain in quel momento viene salvato come risultato.
  *  5. Si passa alla frequenza successiva e si ripete.
  *  6. Alla fine i risultati vengono inviati al PC via UART (115200 baud).
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <math.h>
#include <stdio.h>
#include <stdarg.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* --- Parametri principali --- */
#define N_SAMPLES    128     /* campioni nella LUT: più sono, più l'onda è precisa */
#define N_FREQ       7       /* numero di frequenze testate                        */
#define GAIN_START   0.05f   /* volume iniziale (quasi zero)                       */
#define GAIN_STEP    0.02f   /* quanto aumenta il volume ad ogni tick di TIM2      */
#define GAIN_MAX     1.0f    /* volume massimo (piena scala DAC)                   */
#define PAUSE_TICKS  10      /* tick di pausa silenziosa tra una frequenza e l'altra (10 × 100ms = 1s) */
#define USE_PC_AUDIO 1       /* 1 = audio generato da script Python su PC via UART */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
DAC_HandleTypeDef hdac1;
DMA_HandleTypeDef hdma_dac_ch1;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim4;

UART_HandleTypeDef huart1;

/* USER CODE BEGIN PV */

/* Frequenze audiometriche standard in Hz */
static const float FREQ[N_FREQ] = {125, 250, 500, 1000, 2000, 4000, 8000};

/* LUT: valori della sinusoide a piena scala (0..4095) */
static uint16_t lut[N_SAMPLES];

/* Buffer DMA: LUT scalata per il gain corrente.
   È diviso in due metà per il double-buffering (vedi callback DMA). */
static uint16_t dac_buf[2 * N_SAMPLES];

/* --- Stato del test --- */
static int   freq_idx     = 0;      /* quale frequenza stiamo testando ora */
static float gain         = GAIN_START;
static int   pause_cnt    = 0;
static int   test_done    = 0;
static volatile uint8_t btn_pressed = 0;  /* settato dall'interrupt del pulsante */

/* Risultati: gain al momento della pressione, poi convertito in dBFS */
static float results[N_FREQ];

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_TIM2_Init(void);
static void MX_DAC1_Init(void);
static void MX_USART1_UART_Init(void);
static void MX_TIM4_Init(void);
/* USER CODE BEGIN PFP */
static void build_lut(void);
static void fill_dac_buf(int half);
static void start_tone(float freq);
static void stop_tone(void);
static void send_results(void);
static void pc_audio_set_gain(float new_gain);
static void uart_send_line(const char *fmt, ...);
/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/*
 * build_lut
 * Riempie la tabella con i valori di un'onda sinusoidale.
 * Formula: sample = (sin(angolo) + 1) / 2 * 4095
 *   → sposta il segnale da [-1,+1] a [0, 4095] (range del DAC a 12 bit)
 */
static void build_lut(void)
{
    for (int i = 0; i < N_SAMPLES; i++)
    {
        float angle = 2.0f * (float)M_PI * i / N_SAMPLES;
        lut[i] = (uint16_t)((sinf(angle) + 1.0f) * 2047.5f);
    }
}

static void uart_send_line(const char *fmt, ...)
{
  char line[96];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  if (len <= 0)
  {
    return;
  }

  if (len >= (int)sizeof(line))
  {
    len = (int)sizeof(line) - 1;
  }

  HAL_UART_Transmit(&huart1, (uint8_t *)line, len, HAL_MAX_DELAY);
}

/*
 * fill_dac_buf
 * Copia una metà del buffer DMA moltiplicando ogni campione per il gain.
 * Il gain scala l'ampiezza: gain=1.0 → volume massimo, gain=0.05 → quasi zero.
 *
 * Il buffer è diviso in due metà perché mentre il DMA trasmette una metà,
 * la CPU aggiorna l'altra (double buffering → nessun rumore o salto nel suono).
 */
static void fill_dac_buf(int half)
{
    int offset = half * N_SAMPLES;
    for (int i = 0; i < N_SAMPLES; i++)
    {
        float val = lut[i] * gain;
        if (val > 4095.0f) val = 4095.0f;
        dac_buf[offset + i] = (uint16_t)val;
    }
}

/*
 * start_tone
 * Avvia la riproduzione di un tono alla frequenza richiesta.
 *
 * TIM4 genera un evento ogni (ARR+1) cicli di clock.
 * Ogni evento fa avanzare il DMA di un campione nella LUT.
 * Con N_SAMPLES campioni per periodo:
 *   f_audio = SystemCoreClock / (ARR+1) / N_SAMPLES
 * Quindi:  ARR = SystemCoreClock / (f_audio * N_SAMPLES) - 1
 */
static void start_tone(float freq)
{
#if USE_PC_AUDIO
  uart_send_line("AUDIO START %.1f %.3f\r\n", freq, gain);
#else
    uint32_t arr = (uint32_t)(SystemCoreClock / (freq * N_SAMPLES)) - 1;
    __HAL_TIM_SET_AUTORELOAD(&htim4, arr);
    __HAL_TIM_SET_COUNTER(&htim4, 0);

    fill_dac_buf(0);
    fill_dac_buf(1);

    HAL_TIM_Base_Start(&htim4);
    HAL_DAC_Start_DMA(&hdac1, DAC_CHANNEL_1,
                      (uint32_t *)dac_buf, 2 * N_SAMPLES,
                      DAC_ALIGN_12B_R);
  #endif
}

/*
 * stop_tone
 * Ferma il DAC e TIM4.
 */
static void stop_tone(void)
{
#if USE_PC_AUDIO
  uart_send_line("AUDIO STOP\r\n");
#else
    HAL_DAC_Stop_DMA(&hdac1, DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim4);
#endif
}

static void pc_audio_set_gain(float new_gain)
{
#if USE_PC_AUDIO
  uart_send_line("AUDIO GAIN %.3f\r\n", new_gain);
#else
  (void)new_gain;
#endif
}

/*
 * send_results
 * Invia i risultati via UART al PC (115200 baud).
 * Il valore in dBFS è negativo: più è vicino a 0, più l'utente ha bisogno
 * di un volume alto per sentire → udito peggiore a quella frequenza.
 */
static void send_results(void)
{
    char line[64];
  uart_send_line("AUDIO DONE\r\n");
    HAL_UART_Transmit(&huart1,
        (uint8_t *)"=== Risultati Audiometria ===\r\n", 31, HAL_MAX_DELAY);

    for (int i = 0; i < N_FREQ; i++)
    {
        int len = snprintf(line, sizeof(line),
            "Freq %4.0f Hz → %.1f dBFS\r\n",
            FREQ[i], results[i]);
        HAL_UART_Transmit(&huart1, (uint8_t *)line, len, HAL_MAX_DELAY);
    }
}

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_TIM2_Init();
  MX_USART1_UART_Init();
#if !USE_PC_AUDIO
  MX_DMA_Init();
  MX_DAC1_Init();
  MX_TIM4_Init();
#endif
  /* USER CODE BEGIN 2 */

    build_lut();
    uart_send_line("AUDIO MODE PC\r\n");
    start_tone(FREQ[freq_idx]);
    HAL_TIM_Base_Start_IT(&htim2);  /* avvia il timer di controllo */

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
    while (1)
    {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
        if (test_done)
        {
            send_results();
            while (1);  /* fine test, CPU si ferma qui */
        }
    }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Configure the main internal regulator output voltage
  */
  if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_MSI;
  RCC_OscInitStruct.MSIState = RCC_MSI_ON;
  RCC_OscInitStruct.MSICalibrationValue = 0;
  RCC_OscInitStruct.MSIClockRange = RCC_MSIRANGE_6;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_MSI;
  RCC_OscInitStruct.PLL.PLLM = 1;
  RCC_OscInitStruct.PLL.PLLN = 40;
  RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV7;
  RCC_OscInitStruct.PLL.PLLQ = RCC_PLLQ_DIV2;
  RCC_OscInitStruct.PLL.PLLR = RCC_PLLR_DIV2;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief DAC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_DAC1_Init(void)
{

  /* USER CODE BEGIN DAC1_Init 0 */

  /* USER CODE END DAC1_Init 0 */

  DAC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN DAC1_Init 1 */

  /* USER CODE END DAC1_Init 1 */

  /** DAC Initialization
  */
  hdac1.Instance = DAC1;
  if (HAL_DAC_Init(&hdac1) != HAL_OK)
  {
    Error_Handler();
  }

  /** DAC channel OUT1 config
  */
  sConfig.DAC_SampleAndHold = DAC_SAMPLEANDHOLD_DISABLE;
  sConfig.DAC_Trigger = DAC_TRIGGER_T4_TRGO;
  sConfig.DAC_OutputBuffer = DAC_OUTPUTBUFFER_ENABLE;
  sConfig.DAC_ConnectOnChipPeripheral = DAC_CHIPCONNECT_DISABLE;
  sConfig.DAC_UserTrimming = DAC_TRIMMING_FACTORY;
  if (HAL_DAC_ConfigChannel(&hdac1, &sConfig, DAC_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN DAC1_Init 2 */

  /* USER CODE END DAC1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 7999;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 999;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim2) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim2, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 0;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim4, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief USART1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_USART1_UART_Init(void)
{

  /* USER CODE BEGIN USART1_Init 0 */

  /* USER CODE END USART1_Init 0 */

  /* USER CODE BEGIN USART1_Init 1 */

  /* USER CODE END USART1_Init 1 */
  huart1.Instance = USART1;
  huart1.Init.BaudRate = 115200;
  huart1.Init.WordLength = UART_WORDLENGTH_8B;
  huart1.Init.StopBits = UART_STOPBITS_1;
  huart1.Init.Parity = UART_PARITY_NONE;
  huart1.Init.Mode = UART_MODE_TX_RX;
  huart1.Init.HwFlowCtl = UART_HWCONTROL_NONE;
  huart1.Init.OverSampling = UART_OVERSAMPLING_16;
  huart1.Init.OneBitSampling = UART_ONE_BIT_SAMPLE_DISABLE;
  huart1.AdvancedInit.AdvFeatureInit = UART_ADVFEATURE_NO_INIT;
  if (HAL_UART_Init(&huart1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USART1_Init 2 */

  /* USER CODE END USART1_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Channel3_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Channel3_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Channel3_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOA, GPIO_PIN_5, GPIO_PIN_RESET);

  /*Configure GPIO pin : Button_Pin */
  GPIO_InitStruct.Pin = Button_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_FALLING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(Button_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : PA5 */
  GPIO_InitStruct.Pin = GPIO_PIN_5;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* EXTI interrupt init*/
  HAL_NVIC_SetPriority(EXTI15_10_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(EXTI15_10_IRQn);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */
/* all'inizio del callback, dentro USER CODE BEGIN 4 */
/*
 * HAL_TIM_PeriodElapsedCallback  –  chiamata ogni 100ms da TIM2
 *
 * Gestisce tre situazioni:
 *  1. Pulsante premuto → salva il risultato, avvia la pausa
 *  2. Gain arrivato al massimo senza risposta → frequenza non sentita
 *  3. Pausa terminata → passa alla frequenza successiva
 */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
    if (htim != &htim2) return;
    HAL_GPIO_TogglePin(GPIOA, GPIO_PIN_5);
    /* --- caso 1 e 2: il tono è in riproduzione --- */
    if (!test_done && pause_cnt == 0)
    {
        if (btn_pressed)
        {
            /* L'utente ha sentito il suono: salva gain come dBFS */
            results[freq_idx] = 20.0f * log10f(gain);
            stop_tone();
            btn_pressed = 0;
            pause_cnt   = 1;  /* inizia la pausa */
        }
        else
        {
            /* Nessuna risposta: aumenta il volume */
            gain += GAIN_STEP;
          if (gain > GAIN_MAX)
          {
            gain = GAIN_MAX;
          }
          pc_audio_set_gain(gain);

            if (gain >= GAIN_MAX)
            {
                /* Frequenza non percepita */
                results[freq_idx] = -100.0f;
                stop_tone();
                pause_cnt = 1;
            }
        }
    }
    /* --- caso 3: pausa silenziosa tra frequenze --- */
    else if (pause_cnt > 0)
    {
        pause_cnt++;
        if (pause_cnt > PAUSE_TICKS)
        {
            pause_cnt = 0;
            freq_idx++;

            if (freq_idx >= N_FREQ)
            {
                test_done = 1;  /* tutte le frequenze completate */
            }
            else
            {
                gain = GAIN_START;
                start_tone(FREQ[freq_idx]);
            }
        }
    }
}

/* DMA ha trasmesso la prima metà → aggiornala per il ciclo successivo */
void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
#if !USE_PC_AUDIO
    fill_dac_buf(0);
#else
  (void)hdac;
#endif
}

/* DMA ha trasmesso la seconda metà → aggiornala per il ciclo successivo */
void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
#if !USE_PC_AUDIO
    fill_dac_buf(1);
#else
  (void)hdac;
#endif
}

/* Interrupt del pulsante (PC13, falling edge) → imposta solo il flag */
void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == Button_Pin)
        btn_pressed = 1;
}

/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
