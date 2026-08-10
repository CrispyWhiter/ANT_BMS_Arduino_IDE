#pragma once

#include <Arduino.h>

namespace AppConfig {

// Firmware identity
namespace Firmware {
constexpr char Name[] = "Multi-BMS ESP32-S3 Display";
constexpr char Version[] = "v1.0.0";
constexpr char Developer[] = "无用脑洞研究所";
}

// Hardware pins
namespace Pins {
constexpr int TftCs = 10;
constexpr int TftReset = 9;
constexpr int TftDc = 8;
constexpr int TftMosi = 11;
constexpr int TftSclk = 12;
constexpr int TftMiso = 13;
constexpr int TftBacklight = 7;
constexpr uint8_t TftBacklightOn = HIGH;
constexpr int UserButton = 6;
}

// Display and LVGL
namespace Display {
constexpr uint16_t Width = 320;
constexpr uint16_t Height = 240;
constexpr uint16_t LvglBufferLines = 12;
constexpr uint32_t SpiWriteFrequencyHz = 10000000;
constexpr uint32_t SpiReadFrequencyHz = 10000000;
constexpr uint8_t Rotation = 3;

constexpr bool RgbOrderBgr = false;
constexpr bool InvertColors = false;
constexpr bool SwapColorBytes = false;
constexpr uint8_t VisibleCellCount = 21;

constexpr uint32_t ResetHighBeforePulseMs = 20;
constexpr uint32_t ResetLowPulseMs = 30;
constexpr uint32_t ResetRecoveryMs = 150;
constexpr uint32_t SleepCommandSettleMs = 10;
constexpr uint32_t WakeupSettleMs = 120;

constexpr uint32_t BacklightPwmFrequencyHz = 5000;
constexpr uint8_t BacklightPwmResolutionBits = 8;
}

// Automatic display power control
namespace DisplayPower {
constexpr bool DefaultEnabled = true;
constexpr uint8_t DefaultSleepMinutes = 5;
constexpr uint8_t MinimumSleepMinutes = 5;
constexpr uint8_t MaximumSleepMinutes = 10;

constexpr uint8_t DimLeadMinutes = 3;
constexpr uint8_t MinimumDerivedDimMinutes = 2;
constexpr uint8_t derivedDimMinutes(uint8_t sleepMinutes) {
  return sleepMinutes > DimLeadMinutes
             ? static_cast<uint8_t>(sleepMinutes - DimLeadMinutes)
             : MinimumDerivedDimMinutes;
}

constexpr uint8_t NormalBrightnessPercent = 100;
constexpr uint8_t DimBrightnessPercent = 25;

constexpr float ImmediateActiveCurrentA = -3.0f;
constexpr float SustainedActiveCurrentA = -1.5f;
constexpr float StableIdleMinimumCurrentA = -1.0f;
constexpr float ReadyFluctuationA = 0.8f;
constexpr float StableFluctuationA = 0.3f;
constexpr float FluctuationMustReachCurrentA = -0.5f;
constexpr float CurrentFilterAlpha = 0.35f;
constexpr uint32_t CurrentSampleWindowMs = 9000;
constexpr uint32_t CurrentDataFreshMs = 8000;
}

// User button
namespace Button {
constexpr uint8_t ActiveLevel = LOW;
constexpr uint32_t DebounceMs = 35;

constexpr uint32_t MultiClickWindowMs = 1000;
constexpr uint8_t ScreenToggleClickCount = 3;

constexpr uint32_t ReservedLongPressMs = 10000;
}

// BLE transport
namespace Ble {
constexpr char LocalDeviceName[] = "BMS-Display";
constexpr char AntPairingNameKeyword[] = "ANT@";
constexpr char JikongNamePrefix[] = "JK";
constexpr char JiabaidaNameKeyword[] = "JBD";
constexpr char JiabaidaAlternateNameKeyword[] = "xiaoxiang";
constexpr char YanyangNameKeyword[] = "YY";
constexpr char YanyangModuleKeyword[] = "B40";

constexpr char CommonServiceUuid[] = "ffe0";
constexpr char CommonDataUuid[] = "ffe1";

constexpr char JiabaidaServiceUuid[] = "ff00";
constexpr char JiabaidaWriteUuid[] = "ff02";
constexpr char JiabaidaNotifyUuid[] = "ff01";

constexpr char NordicUartServiceUuid[] = "6e400001-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char NordicUartWriteUuid[] = "6e400002-b5a3-f393-e0a9-e50e24dcca9e";
constexpr char NordicUartNotifyUuid[] = "6e400003-b5a3-f393-e0a9-e50e24dcca9e";

constexpr uint32_t ConfigurationScanDurationMs = 12000;
constexpr uint32_t ReconnectScanDurationMs = 6000;
constexpr uint32_t ScanRetryDelayMs = 1200;
constexpr uint32_t BootReconnectDelayMs = 1200;

constexpr uint16_t ReconnectScanInterval = 70;
constexpr uint16_t ReconnectScanWindow = 70;
constexpr uint16_t ConfigurationScanInterval = 70;
constexpr uint16_t ConfigurationScanWindow = 70;

constexpr uint32_t FastReconnectDelayMs = 500;
constexpr uint32_t FastReconnectTimeoutMs = 2800;
constexpr uint8_t FastReconnectAttempts = 1;

constexpr uint32_t PeerDisconnectRetryDelayMs = 2500;
constexpr uint32_t RapidDisconnectWindowMs = 5000;

constexpr uint32_t ConnectDelayMs = 40;
constexpr uint32_t ConnectTimeoutMs = 6500;
constexpr uint8_t ConnectRetries = 1;

constexpr uint32_t FirstStatusFastRetryMs = 500;
constexpr uint8_t FirstStatusFastRequestCount = 3;
constexpr uint32_t FirstStatusSlowRetryMs = 2000;
constexpr uint32_t FirstStatusTimeoutMs = 20000;

constexpr uint32_t StatusRequestPeriodMs = 3000;
constexpr uint32_t JiabaidaCellRequestDelayMs = 120;
constexpr uint8_t YanyangDefaultModbusAddress = 1;

constexpr uint32_t StatusProbeAfterMs = 12000;
constexpr uint32_t StatusProbePeriodMs = 4000;
constexpr uint32_t StatusHardRecoveryMs = 45000;
constexpr uint32_t RawNotificationDeadMs = 30000;
constexpr uint8_t MaxConsecutiveWriteFailures = 3;
constexpr uint32_t DeviceInfoAfterFirstStatusMs = 1800;

constexpr uint16_t ConnectionMinInterval = 24;
constexpr uint16_t ConnectionMaxInterval = 48;
constexpr uint16_t ConnectionLatency = 0;
constexpr uint16_t ConnectionSupervisionTimeout = 600;

constexpr uint32_t ActionDisconnectTimeoutMs = 2500;
constexpr uint32_t ScanEndSuppressWindowMs = 1000;
constexpr uint32_t CallbackExitSettleMs = 80;

constexpr uint8_t MaxConfigurationResults = 12;
constexpr uint8_t MaxStoredDeviceNameLength = 47;

constexpr size_t NotificationBufferSize = 1024;
constexpr size_t NotificationDrainChunkSize = 256;
}

// Range estimation
namespace Range {

constexpr float ReserveCapacityAh = 14.0f;
constexpr float DefaultKilometersPerAmpHour = 2.5f;
constexpr float MinimumKilometersPerAmpHour = 0.01f;
constexpr float MaximumKilometersPerAmpHour = 100.0f;
}

// Web configuration portal
namespace WebConfig {
constexpr char AccessPointName[] = "无用脑洞研究所";
constexpr uint16_t HttpPort = 80;
constexpr uint8_t ApChannel = 6;
constexpr uint8_t MaxStations = 1;

constexpr uint32_t InitialAccessWindowMs = 120000;

constexpr uint32_t FallbackAfterBleStartMs = 30000;
constexpr uint32_t BleResumeAfterClientLeavesMs = 800;
constexpr uint32_t PrePortalScanTimeoutMs = 18000;
constexpr uint32_t PortalRetryDelayMs = 3000;

constexpr uint8_t StartupAttempts = 3;
constexpr uint32_t StartupRetryDelayMs = 250;
constexpr uint32_t RadioSettleDelayMs = 150;

constexpr int8_t SoftApTxPowerQuarterDbm = 40;

constexpr char DefaultPassword[] = "123456";
constexpr uint8_t PasswordMinLength = 6;
constexpr uint8_t PasswordMaxLength = 16;

constexpr uint32_t SaveRestartDelayMs = 1600;
constexpr uint32_t RescanRestartDelayMs = 900;
constexpr uint32_t RebootFlushDelayMs = 80;
}

// Protocol limits
namespace Protocol {
constexpr size_t MaxFrameSize = 384;
constexpr uint32_t FrameAssemblyTimeoutMs = 1000;
constexpr uint8_t MaxCellCount = 32;
constexpr uint8_t MaxTemperatureCount = 6;
constexpr uint32_t StatusLogPeriodMs = 2000;

constexpr float MinCellVoltage = 0.10f;
constexpr float MaxCellVoltage = 6.50f;
constexpr float MaxPackVoltage = 1000.0f;
constexpr float MaxAbsCurrent = 5000.0f;
constexpr float MaxAbsPowerW = 5000000.0f;
constexpr float MaxCapacityAh = 1000000.0f;
constexpr int16_t MinTemperatureC = -80;
constexpr int16_t MaxTemperatureC = 200;
}

// Runtime timing
namespace Runtime {
constexpr uint32_t SerialReadyDelayMs = 500;
constexpr uint32_t DisplayToBleDelayMs = 300;
constexpr uint32_t LongPressRestartDelayMs = 600;
constexpr uint32_t MainLoopYieldMs = 2;
constexpr uint32_t ForcedRestartFlushDelayMs = 80;
constexpr uint32_t ResumeReconnectDelayMs = 100;
}

}
