/* Hardware pwm controlled fan driver for Raspberry Pi */

#include <bcm2835.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

/* Global Constants */
#define PIN RPI_GPIO_P1_12 // GPIO(pin 18) or Board pin 12 [RaspberryPi 3B+]
#define PWM_CHANNEL 0 // PWM channel of the defined pwm pin
#define BASE_CLOCK 19.2 // Base clock rate in Mhz (CONSTANT)
#define CLOCK_DIVIDER 16 // Define your required nearest clock divider value here (Ex: BCM2835_PWM_CLOCK_DIVIDER_16) which is in range of your required frequency
#define REQ_FREQ 24 // Define your required frequency here in Mhz (1/1.2Mhz approx) for a valid range in accordance to the CLOCK_DIVIDER value
#define FAN_MIN 25 // Min treshold of the fan, if speeds fall below this value service is killed to avoid stalling or in worst case to avoid frying the stalled fan
#define REFRESH_INTERVAL 1000 // Time to wait for the loop (milliseconds)
#define TEMP_OFFSET 35 // Lowest temp
#define HYSTERISIS 1 // The difference of current temperature and defined temp_offset that has to be met in order to run the fan

/* Start of Main */
int main(int argc, char **argv) {

  if (!bcm2835_init())
    return 1;

  /* Define Temperature and Fanspeed(range: 0-100%) tresholds */
  int tempSteps[] = {40, 45, 50, 55, 60};
  int fanSpeedStepsInt[] ={35, 50, 70, 80, 100};

  /* Calculate range for given frequency */
  float range = BASE_CLOCK * pow(10,6) / (CLOCK_DIVIDER * REQ_FREQ);
  printf("calculated range for given freq is : %.2f\n", range);

  /* Function to calculate and return speed ratios of the range */
  float calcRatio(int n) {
    float result=0;
    result = (float)n / 100 * range;
    return result;
  }

  /* calc fan min speed */
  float fan_min_speed = calcRatio(FAN_MIN);
  printf("Ratio of minimum fan speed is : %.2f\n", fan_min_speed);

  /* Clock and PWM GPIO pin setup */
  bcm2835_gpio_fsel(PIN, BCM2835_GPIO_FSEL_ALT5);

  bcm2835_pwm_set_clock(BCM2835_PWM_CLOCK_DIVIDER_16);
  bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 1);
  bcm2835_pwm_set_range(PWM_CHANNEL, range);

  /* vars to hold array lengths for further use */
  int tempStepsSize, fanSpeedStepsSize;
  tempStepsSize = sizeof(tempSteps)/sizeof(int);
  fanSpeedStepsSize = sizeof(fanSpeedStepsInt)/sizeof(int);

  /* Calculate ratios for each treshold of fanSpeedStepsInt in accordance with the range */
  float fanSpeedSteps[fanSpeedStepsSize];
  for (int i=0; i<=fanSpeedStepsSize-1; i++) {
      float rangeVal = calcRatio(fanSpeedStepsInt[i]);
      printf("Ratio of tempstep %d of range %.2f is : %.2f (%d%)\n",fanSpeedStepsInt[i], range, rangeVal, fanSpeedStepsInt[i]);
      fanSpeedSteps[i] = rangeVal;
  }

  if (tempStepsSize != fanSpeedStepsSize) {
    printf("No.of temperature steps are not equal to Fanspeed steps\n");
    return 0;
  }

  /* Ramp up the fan to full speed on start just for the keks */
  bcm2835_pwm_set_data(PWM_CHANNEL, calcRatio(100));
  bcm2835_delay(10000);
  printf("Done firing at startup\n");

  /* LOCAL CONSTANTS */
  float setFanSpeed, fanOldSpeed=0, curFanSpeed, curCpuTemp;
  FILE *thermal;

  while (1) {
    /*GRAB TEMP*/
    thermal = popen("/opt/vc/bin/vcgencmd measure_temp | cut -c6-9", "r");
    fscanf(thermal, "%f", &curCpuTemp);
    pclose(thermal);

    if (abs(curCpuTemp - TEMP_OFFSET > HYSTERISIS)) {
      if (curCpuTemp < tempSteps[0]) {
        /* Here we setFanSpeed to low if below min temp */
        setFanSpeed = fanSpeedSteps[0];
      } else if (curCpuTemp > tempSteps[tempStepsSize-1]) {
        /* Here we setFanSpeed to full if above max temp */
        setFanSpeed = fanSpeedSteps[fanSpeedStepsSize-1];
      } else {
	  /* If curCpuTemp is in between two temperature values calculate the speed ratio using linear interpolation */
          for (int i=0; i<=tempStepsSize-1; i++) {
            if ((curCpuTemp >= tempSteps[i]) && (curCpuTemp < tempSteps[i+1])) {
              setFanSpeed = (float)((fanSpeedSteps[i+1]-fanSpeedSteps[i]) \
                                /(tempSteps[i+1]-tempSteps[i])*(curCpuTemp-tempSteps[i]) \
                                +fanSpeedSteps[i]);
            }
          }
      }

      /* current fan speed in % of initially set tresholds for easy understanding of debug msg */
      curFanSpeed = (float)setFanSpeed / range * 100;

      if ((setFanSpeed != fanOldSpeed)) {
        if (((setFanSpeed >= fan_min_speed) || (setFanSpeed == 0)))
        {
          /* set fan speed */
          bcm2835_pwm_set_data(PWM_CHANNEL, setFanSpeed);
          fanOldSpeed = setFanSpeed;
          printf("current temp : %.2fC | fanspeed : %.2f%\n", curCpuTemp, curFanSpeed);
        }
        else
        {
          if (curFanSpeed != 0)
	  {
            printf("ERROR: Speed was %.2f\n", curFanSpeed);
            printf("ERROR: Below minimum fan speed\n");
            printf("Stopped service to avoid stall, Please assign tresholds higher than fans minimum speed and restart service\n");
            bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);
            bcm2835_close();
            return 0;
	  }
	  else
	  {
	    printf("SOMETHING WRONG OR NOTHING TO DO\n");
	  }
        }
      }
      /* Refresh interval */
      bcm2835_delay(REFRESH_INTERVAL);
    }
  }
  bcm2835_pwm_set_mode(PWM_CHANNEL, 1, 0);
  bcm2835_close();
  return 0;
}
 
