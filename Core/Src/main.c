/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

#include "tusb.h"
#include "usb_descriptors.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

#define ARRAY_SIZE(x) (sizeof(x) / sizeof(x[0]))

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc1;

CRC_HandleTypeDef hcrc;

PCD_HandleTypeDef hpcd_USB_FS;

/* USER CODE BEGIN PV */

/*
 * Da sem usposobil TinyUSB:
 * - dodal git submodule za tinyusb
 * - izklopil USB_DEVICE Middleware od ST
 * - pustil vklopljen Connectivity USB
 * - v Connectivity USB v NVIC settings sem vklopil oba HP in LP callbacka
 * 	 in potem v stm32f1xx_it.c v oboje dal tusb_int_handler(BOARD_TUD_RHPORT, true); return;
 * - v mainu klicem tusb_init in potem brez karksnih koli HAL_Delay v loopu klicem tud_task
 * - tud_hid_report ne smes klicat ce ni tud_hid_ready()
 * - prekopiral tusb_config.h, usb_descriptors.h in usb_descriptors.c iz tinyusb/examples/device/hid_composite
 * - v tusb_config.h nastavil CFG_TUSB_MCU, CFG_TUSB_OS, CFG_TUSB_RHPORT0_MODE
 * - v tusb_descriptors.h pobrisal tiste report id ki jih ne rabim
 * - v tusb_descriptors.c sem nastavil ime naprave in si postavil svoj report descriptor
 * - iz tinyusb/hw/bsp/stm32f1/family.c sem nekam skopiral board_get_unique_id
 * - definirat je treba se tud_hid_get_report_cb in tud_hid_set_report_cb
 * - mogoce se kaj ampak mislim da je to to (glavni problem je bil da manjka dokumentacije, chatgpt je bil precej uporaben)
 *
 * https://github.com/hathach/tinyusb/discussions/633
 * https://docs.tinyusb.org/en/latest/reference/getting_started.html
 * https://ejaaskel.dev/making-usb-device-with-stm32-tinyusb/
 *
 * kako sem CDC naredil hkrati kot HID pa najdi v gitu commit pa se vse vidi
 */

typedef struct __attribute__ ((packed)) {
	uint8_t buttons;
} hshifter_report_t;

typedef struct {
	uint16_t x, y;
} gear_position_t;

typedef struct {
	gear_position_t gear_positions[6];
	gear_position_t reverse_position;
	gear_position_t neutral_position;
} hshifter_config_t;

hshifter_config_t hshifter_config = {0};

uint32_t analog_x = 0;
uint32_t analog_y = 0;

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_ADC1_Init(void);
static void MX_USB_PCD_Init(void);
static void MX_CRC_Init(void);
/* USER CODE BEGIN PFP */

bool FlashWriteConfig(const hshifter_config_t* config);
bool FlashReadConfig(hshifter_config_t* out_config);

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

// input_channel mora biti ADC_CHANNEL_X
uint32_t AnalogRead(uint32_t input_channel, uint32_t samples)
{
	// da smo prijazni
	assert(input_channel >= ADC_CHANNEL_0 && input_channel <= ADC_CHANNEL_15);
	assert(samples < UINT32_MAX / (1<<12)); // da ne bo "sum" overflowal

	ADC_ChannelConfTypeDef adc_channel_conf = {
		.Channel = input_channel,
		.Rank = ADC_REGULAR_RANK_1,
		.SamplingTime = ADC_SAMPLETIME_7CYCLES_5,
	};

	HAL_ADC_ConfigChannel(&hadc1, &adc_channel_conf);

	uint32_t sum = 0;
	for (uint32_t i = 0; i < samples; i++)
	{
		HAL_ADC_Start(&hadc1);
		HAL_ADC_PollForConversion(&hadc1, 1);
		sum += HAL_ADC_GetValue(&hadc1);
	}

	return sum / samples;
}

// Invoked when the host requests data (e.g. GET_REPORT)
uint16_t tud_hid_get_report_cb(uint8_t instance,
                               uint8_t report_id,
                               hid_report_type_t report_type,
                               uint8_t* buffer,
                               uint16_t reqlen) {
    // Return length of data copied to buffer
    return 0;
}

// Invoked when the host sends data (e.g. SET_REPORT)
void tud_hid_set_report_cb(uint8_t instance,
                           uint8_t report_id,
                           hid_report_type_t report_type,
                           uint8_t const* buffer,
                           uint16_t bufsize) {
    // Handle received data
}

void HandleCommand(const char* cmd)
{
	if (strcmp(cmd, "get pos") == 0)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%lu %lu\n", analog_x, analog_y);
		tud_cdc_write_str(buf);
	}
	else if (strncmp(cmd, "get ", 4) == 0 && cmd[4] >= '1' && cmd[4] <= '6' && cmd[5] == '\0')
	{
		int gear = cmd[4] - '1';
		char buf[64];
		snprintf(buf, sizeof(buf), "%u %u\n", hshifter_config.gear_positions[gear].x,
				hshifter_config.gear_positions[gear].y);
		tud_cdc_write_str(buf);
	}
	else if (strcmp(cmd, "get R") == 0)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%u %u\n", hshifter_config.reverse_position.x,
				hshifter_config.reverse_position.y);
		tud_cdc_write_str(buf);
	}
	else if (strcmp(cmd, "get N") == 0)
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "%u %u\n", hshifter_config.neutral_position.x,
			hshifter_config.neutral_position.y);
		tud_cdc_write_str(buf);
	}
	else if (strncmp(cmd, "set ", 4) == 0 && cmd[4] >= '1' && cmd[4] <= '6' && cmd[5] == '\0')
	{
		int gear = cmd[4] - '1';
		hshifter_config.gear_positions[gear].x = analog_x;
		hshifter_config.gear_positions[gear].y = analog_y;
		tud_cdc_write_str("ok\n");
	}
	else if (strcmp(cmd, "set R") == 0)
	{
		hshifter_config.reverse_position.x = analog_x;
		hshifter_config.reverse_position.y = analog_y;
		tud_cdc_write_str("ok\n");
	}
	else if (strcmp(cmd, "set N") == 0)
	{
		hshifter_config.neutral_position.x = analog_x;
		hshifter_config.neutral_position.y = analog_y;
		tud_cdc_write_str("ok\n");
	}
	else if (strcmp(cmd, "flash write") == 0)
	{
		bool success = FlashWriteConfig(&hshifter_config);
		if (success)
			tud_cdc_write_str("ok\n");
		else
			tud_cdc_write_str("unknown error\n");
	}
	else if (strcmp(cmd, "flash read") == 0)
	{
		bool success = FlashReadConfig(&hshifter_config);
		if (success)
			tud_cdc_write_str("ok\n");
		else
			tud_cdc_write_str("unknown error\n");
	}
	else if (strcmp(cmd, "test") == 0)
	{
		tud_cdc_write_str("ok\n");
	}
	else
	{
		char buf[64];
		snprintf(buf, sizeof(buf), "invalid input '%s'\n", cmd);
		tud_cdc_write_str(buf);
	}
}

void HandleCDCInput()
{
	static char cdc_read_buf[64] = {0};
	static uint32_t cdc_read_buf_index = 0;

	char received = tud_cdc_read_char();

	if ((received == '\r' || received == '\n') || cdc_read_buf_index >= sizeof(cdc_read_buf) - 1)
	{
		cdc_read_buf[cdc_read_buf_index] = '\0';
		cdc_read_buf_index = 0;
		tud_cdc_write_char('\n');
		HandleCommand(cdc_read_buf);
		tud_cdc_write_flush();
	}
	else if (received >= 32 && received < 127)
	{
		cdc_read_buf[cdc_read_buf_index] = received;
		cdc_read_buf_index++;
		tud_cdc_write_char(received);
		tud_cdc_write_flush();
	}
	else if ((received == '\b' || received == 127) && cdc_read_buf_index > 0)
	{
		cdc_read_buf_index--;
		tud_cdc_write_str("\b \b");
		tud_cdc_write_flush();
	}
}

#define CONFIG_FLASH_PAGE_ADDR   (FLASH_BANK1_END - FLASH_PAGE_SIZE + 1) // last 1 KB page

bool FlashWriteConfig(const hshifter_config_t* config)
{
	HAL_FLASH_Unlock();

	FLASH_EraseInitTypeDef erase_def = {
		.TypeErase = FLASH_TYPEERASE_PAGES,
		.PageAddress = CONFIG_FLASH_PAGE_ADDR,
		.NbPages = 1
	};
	uint32_t page_error = 0;
	HAL_StatusTypeDef status = HAL_FLASHEx_Erase(&erase_def, &page_error);
	if (status != HAL_OK || page_error != 0xFFFFFFFF)
	{
		HAL_FLASH_Lock();
		return false; // failed
	}

	uint32_t addr = CONFIG_FLASH_PAGE_ADDR;
	for (uint32_t i = 0; i < sizeof(hshifter_config_t) / 4; i++)
	{
		HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, ((uint32_t*)config)[i]);
		addr += 4;
	}

	// Velikost configa mora biti veckratnik 4
	uint32_t crc = HAL_CRC_Calculate(&hcrc, (uint32_t*)config, sizeof(hshifter_config_t) / 4);
	HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr, crc);

	HAL_FLASH_Lock();
	return true;
}

bool FlashReadConfig(hshifter_config_t* out_config)
{
	hshifter_config_t config = {0};
	memcpy(&config, (void*)CONFIG_FLASH_PAGE_ADDR, sizeof(hshifter_config_t));

	uint32_t crc = HAL_CRC_Calculate(&hcrc, (uint32_t*)&config, sizeof(hshifter_config_t) / 4);
	uint32_t crc_stored = *(uint32_t*)(CONFIG_FLASH_PAGE_ADDR + sizeof(hshifter_config_t));

	if (crc == crc_stored)
	{
		*out_config = config;
		return true;
	}
	return false;
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
  MX_ADC1_Init();
  MX_USB_PCD_Init();
  MX_CRC_Init();
  /* USER CODE BEGIN 2 */

  HAL_ADCEx_Calibration_Start(&hadc1);

  FlashReadConfig(&hshifter_config);

  tusb_rhport_init_t dev_init = {
   .role = TUSB_ROLE_DEVICE,
   .speed = TUSB_SPEED_AUTO
  };
  tusb_init(0, &dev_init);

  uint8_t gear_prev_states[16] = {0};
  uint8_t gear_prev_states_index = 0;

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
	analog_x = AnalogRead(ADC_CHANNEL_1, 16);
	analog_y = AnalogRead(ADC_CHANNEL_0, 16);

	/*
			R   |  1  |   3  |  5
			    |     |      |
			    |  2  |   4  |  6
			    |     |      |
			boundry_1 |   boundry_3
			      boundry_2
	 */
	uint32_t boundry_1 = (hshifter_config.reverse_position.x * 2
		+ hshifter_config.gear_positions[0].x
		+ hshifter_config.gear_positions[1].x) / 4;
	uint32_t boundry_2 = (hshifter_config.gear_positions[0].x
		+ hshifter_config.gear_positions[1].x
		+ hshifter_config.gear_positions[2].x
		+ hshifter_config.gear_positions[3].x) / 4;
	uint32_t boundry_3 = (hshifter_config.gear_positions[2].x
		+ hshifter_config.gear_positions[3].x
		+ hshifter_config.gear_positions[4].x
		+ hshifter_config.gear_positions[5].x) / 4;

	uint8_t button = 0;

	if (analog_x < boundry_1)
	{
		if (analog_y > (hshifter_config.reverse_position.y + hshifter_config.neutral_position.y) / 2)
			button = 7;
	}
	else if (analog_x < boundry_2)
	{
		if (analog_y > (hshifter_config.gear_positions[0].y + hshifter_config.neutral_position.y) / 2)
			button = 1;
		else if (analog_y < (hshifter_config.gear_positions[1].y + hshifter_config.neutral_position.y) / 2)
			button = 2;
	}
	else if (analog_x < boundry_3)
	{
		if (analog_y > (hshifter_config.gear_positions[2].y + hshifter_config.neutral_position.y) / 2)
			button = 3;
		else if (analog_y < (hshifter_config.gear_positions[3].y + hshifter_config.neutral_position.y) / 2)
			button = 4;
	}
	else
	{
		if (analog_y > (hshifter_config.gear_positions[4].y + hshifter_config.neutral_position.y) / 2)
			button = 5;
		else if (analog_y < (hshifter_config.gear_positions[5].y + hshifter_config.neutral_position.y) / 2)
			button = 6;
	}

	// shrani nekaj stanj za nazaj in potem poslji output samo
	// ce so vsa ta nedavna stanja enaka
	gear_prev_states[gear_prev_states_index] = button;
	gear_prev_states_index = (gear_prev_states_index + 1) % ARRAY_SIZE(gear_prev_states);

	bool output_stable = true;
	for (int i = 0; i < ARRAY_SIZE(gear_prev_states); i++)
	{
		if (gear_prev_states[i] != button)
		{
			output_stable = false;
			break;
		}
	}

	if (tud_hid_ready() && output_stable)
	{
		hshifter_report_t hshifter_report;
		hshifter_report.buttons = 1 << button;
		tud_hid_report(REPORT_ID_HSHIFTER, (const void*)&hshifter_report, sizeof(hshifter_report_t));
	}

	if (tud_cdc_connected() && tud_cdc_available())
	{
		HandleCDCInput();
	}

	tud_task();
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
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
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
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC|RCC_PERIPHCLK_USB;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  PeriphClkInit.UsbClockSelection = RCC_USBCLKSOURCE_PLL_DIV1_5;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC1_Init(void)
{

  /* USER CODE BEGIN ADC1_Init 0 */

  /* USER CODE END ADC1_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */

  /* USER CODE END ADC1_Init 1 */

  /** Common config
  */
  hadc1.Instance = ADC1;
  hadc1.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc1.Init.ContinuousConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc1.Init.DataAlign = ADC_DATAALIGN_RIGHT;
  hadc1.Init.NbrOfConversion = 1;
  if (HAL_ADC_Init(&hadc1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_0;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_1CYCLE_5;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC1_Init 2 */

  /* USER CODE END ADC1_Init 2 */

}

/**
  * @brief CRC Initialization Function
  * @param None
  * @retval None
  */
static void MX_CRC_Init(void)
{

  /* USER CODE BEGIN CRC_Init 0 */

  /* USER CODE END CRC_Init 0 */

  /* USER CODE BEGIN CRC_Init 1 */

  /* USER CODE END CRC_Init 1 */
  hcrc.Instance = CRC;
  if (HAL_CRC_Init(&hcrc) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN CRC_Init 2 */

  /* USER CODE END CRC_Init 2 */

}

/**
  * @brief USB Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_PCD_Init(void)
{

  /* USER CODE BEGIN USB_Init 0 */

  /* USER CODE END USB_Init 0 */

  /* USER CODE BEGIN USB_Init 1 */

  /* USER CODE END USB_Init 1 */
  hpcd_USB_FS.Instance = USB;
  hpcd_USB_FS.Init.dev_endpoints = 8;
  hpcd_USB_FS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_FS.Init.low_power_enable = DISABLE;
  hpcd_USB_FS.Init.lpm_enable = DISABLE;
  hpcd_USB_FS.Init.battery_charging_enable = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_FS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_Init 2 */

  /* USER CODE END USB_Init 2 */

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
/* USER CODE BEGIN MX_GPIO_Init_1 */
/* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOD_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();

/* USER CODE BEGIN MX_GPIO_Init_2 */
/* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

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

#ifdef  USE_FULL_ASSERT
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
