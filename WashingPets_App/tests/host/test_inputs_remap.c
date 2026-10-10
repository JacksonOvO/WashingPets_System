#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../Core/Src/auto_device.c"
#include "../../Core/Src/task_output.c"

static uint32_t tick;
static uint16_t levels[8];
static unsigned configured;
static AppHardwareConfig_t hardware;
uint32_t HAL_GetTick(void) { return tick; }
const AppHardwareConfig_t *AppParam_GetHardwareConfig(void) { return &hardware; }
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *p, uint16_t pin)
{ return (levels[(uintptr_t)p] & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET; }
void HAL_GPIO_Init(GPIO_TypeDef *p, GPIO_InitTypeDef *i)
{ (void)p; assert(i->Mode == GPIO_MODE_INPUT && i->Pull == GPIO_PULLUP); ++configured; }

static void reset_inputs(uint32_t start)
{
  tick = start;
  for (unsigned p = 0; p < 8; ++p) levels[p] = 0xFFFF;
  levels[(uintptr_t)GPIOB] &= ~GPIO_PIN_2;
  configured = 0;
  AutoDevice_Init();
  assert(configured == 21 && !AutoDevice_IsEmergencyStopped());
}

int main(void)
{
  const AutoGpio expected[] = {
    {GPIOA, GPIO_PIN_3}, {GPIOA, GPIO_PIN_4}, {GPIOA, GPIO_PIN_5}, {GPIOA, GPIO_PIN_6},
    {GPIOA, GPIO_PIN_7}, {GPIOC, GPIO_PIN_4}, {GPIOC, GPIO_PIN_5}, {GPIOB, GPIO_PIN_0},
    {GPIOB, GPIO_PIN_1}, {GPIOB, GPIO_PIN_2}, {GPIOF, GPIO_PIN_11}, {GPIOF, GPIO_PIN_12},
    {GPIOF, GPIO_PIN_13}, {GPIOF, GPIO_PIN_14}, {GPIOE, GPIO_PIN_10}, {GPIOE, GPIO_PIN_11},
    {GPIOE, GPIO_PIN_12}, {GPIOE, GPIO_PIN_13}, {GPIOE, GPIO_PIN_14},
    {GPIOF, GPIO_PIN_15}, {GPIOG, GPIO_PIN_0}
  };
  assert(AUTO_INPUT_FIRST == 0 && AUTO_INPUT_COUNT == 21 && AUTO_INPUT_GPIO_COUNT == 19);
  for (unsigned i = 0; i < AUTO_INPUT_COUNT; ++i)
  {
    assert(input_table[i].port == expected[i].port && input_table[i].pin == expected[i].pin);
    for (unsigned j = 0; j < i; ++j)
      assert(input_table[i].port != input_table[j].port || input_table[i].pin != input_table[j].pin);
    for (unsigned j = AUTO_OUTPUT_FIRST; j < AUTO_OUTPUT_COUNT; ++j)
      assert(input_table[i].port != output_hw_table[j].port || input_table[i].pin != output_hw_table[j].pin);
    reset_inputs(UINT32_MAX - 1000U);
    unsigned port = (uintptr_t)input_table[i].port;
    levels[port] ^= input_table[i].pin;
    uint8_t changed = HAL_GPIO_ReadPin(input_table[i].port, input_table[i].pin) == GPIO_PIN_RESET;
    assert(AutoDevice_ReadInputRaw(i) == changed);
    AutoDevice_ProcessInputs();
    uint32_t filter = i <= AUTO_INPUT_TANK_HIGH ? 3000 : 30;
    tick += filter - 1;
    AutoDevice_ProcessInputs();
    assert(AutoDevice_ReadInput(i) != changed);
    ++tick;
    AutoDevice_ProcessInputs();
    assert(AutoDevice_ReadInput(i) == changed);
    if (i == AUTO_INPUT_EMERGENCY_STOP) assert(AutoDevice_IsEmergencyStopped());
  }
  reset_inputs(0);
  levels[(uintptr_t)GPIOA] &= ~GPIO_PIN_3;
  AutoDevice_ProcessInputs(); tick = 2999; AutoDevice_ProcessInputs();
  levels[(uintptr_t)GPIOA] |= GPIO_PIN_3;
  AutoDevice_ProcessInputs(); tick = 3000; AutoDevice_ProcessInputs();
  assert(!AutoDevice_ReadInput(AUTO_INPUT_BATH_LIQUID_LOW));
  hardware.lower_door_bypass = 1;
  assert(AutoDevice_ReadInput(AUTO_INPUT_LOWER_DOOR_CLOSED));
  assert(!AutoDevice_ReadInputRaw(AUTO_INPUT_LOWER_DOOR_CLOSED));
  hardware.lower_door_bypass = 0;
  output_auto_mode = 1;
  input_stable[AUTO_INPUT_UPPER_DOOR_CLOSED] = 1;
  input_stable[AUTO_INPUT_LOWER_DOOR_CLOSED] = 1;
  for (unsigned mask = 0; mask < 256; ++mask)
  {
    for (unsigned i = 0; i < 8; ++i) input_stable[AUTO_INPUT_SPARE_1 + i] = (mask >> i) & 1U;
    assert(output_interlock_allow(AUTO_OUTPUT_SHOWER_MIDDLE, 1));
    assert(!output_interlock_allow(AUTO_OUTPUT_DOOR_CLOSE, 1));
  }
  input_stable[AUTO_INPUT_UPPER_DOOR_CLOSED] = 0;
  assert(!output_interlock_allow(AUTO_OUTPUT_SHOWER_MIDDLE, 1));
  assert(output_interlock_allow(AUTO_OUTPUT_DOOR_CLOSE, 1));
  puts("Inputs: 21 exact pins, no output conflicts, 3s/30ms filters, bounce/wrap, emergency and 256 spare patterns PASS");
}
