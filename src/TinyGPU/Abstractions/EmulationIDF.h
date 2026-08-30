#pragma once
/**
 * @file EmulationIDF.h
 * @brief ESP-IDF-native emulation of exactly the Arduino surface
 * TinyGPU/Drivers and TinyGPU/Input actually call - delay()/millis(),
 * pinMode()/digitalWrite()/digitalRead() (+ HIGH/LOW/OUTPUT/INPUT/
 * INPUT_PULLUP), SPIClass/SPISettings (+ MSBFIRST/LSBFIRST/SPI_MODEn)
 * and a global `SPI` (driver/spi_master.h), and TwoWire (+ a global
 * `Wire`) on the legacy driver/i2c.h controller driver.
 *
 * This is the ESP-IDF branch of TinyGPU/Emulation.h's fallback - include
 * that instead of this file directly; it decides whether a real Arduino
 * core is available, and routes here only when it isn't and
 * ESP_PLATFORM is defined (a plain ESP-IDF component build via
 * `idf_component_register`, without arduino-esp32).
 *
 * Not a general Arduino-core replacement - only covers the calls this
 * library's own Drivers/Input headers make (verified by grepping them),
 * not the full Arduino API. A sketch built against this fallback that
 * calls other Arduino functions will still need its own additions here,
 * or the real Arduino core after all.
 *
 * Not compiled or hardware-tested as part of this repo's own CI (which
 * only exercises the desktop/Arduino-Emulator and Arduino-IDE paths) -
 * treat it as a reasonable-effort starting point and verify against your
 * exact IDF version/target before relying on it on real hardware.
 */

#if !defined(ESP_PLATFORM)
#error "TinyGPU/Emulation/EmulationIDF.h is ESP-IDF-only - include TinyGPU/Emulation.h instead, which routes here only when ESP_PLATFORM is defined."
#endif

#include <stddef.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Namespaced (not global) like the rest of this library - safe to do
// even for delay()/pinMode()/SPIClass/... since every call site (all of
// TinyGPU/Drivers and TinyGPU/Input) lives inside `namespace tinygpu`
// itself, so unqualified lookup finds these the same way it would find
// them in the global namespace on the real-Arduino-core branch of
// TinyGPU/Emulation.h.
namespace tinygpu {

// ---------------------------------------------------------------------
// delay() / millis()
// ---------------------------------------------------------------------

/// Blocks the calling task for `ms` milliseconds via FreeRTOS - the
/// ESP-IDF-native equivalent of Arduino's delay().
inline void delay(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }

/// Milliseconds since boot, matching Arduino's millis() (wraps the same
/// way a 32-bit millisecond counter would, since esp_timer's underlying
/// 64-bit microsecond count is truncated to unsigned long here).
inline unsigned long millis() {
  return static_cast<unsigned long>(esp_timer_get_time() / 1000);
}

// ---------------------------------------------------------------------
// Digital I/O
// ---------------------------------------------------------------------

// constexpr, not #define - a macro can't be namespaced (the preprocessor
// runs before namespaces mean anything to it), so these are ordinary
// namespaced constants instead.
constexpr int HIGH = 1;
constexpr int LOW = 0;
constexpr int INPUT = 0x00;
constexpr int OUTPUT = 0x01;
constexpr int INPUT_PULLUP = 0x02;

/// Configures a GPIO's direction/pull, matching Arduino's pinMode(). A
/// negative pin (this library's "not wired up" sentinel throughout
/// Drivers/Input) is silently ignored, exactly like the callers already
/// assume when they guard on `pin >= 0` before calling at all - and
/// harmlessly for the few call sites that don't bother guarding.
inline void pinMode(int pin, int mode) {
  if (pin < 0) return;
  gpio_config_t cfg = {};
  cfg.pin_bit_mask = (1ULL << pin);
  cfg.mode = (mode == OUTPUT) ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT;
  cfg.pull_up_en =
      (mode == INPUT_PULLUP) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE;
  cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
  cfg.intr_type = GPIO_INTR_DISABLE;
  gpio_config(&cfg);
}

/// Sets a GPIO output level, matching Arduino's digitalWrite().
inline void digitalWrite(int pin, int value) {
  if (pin < 0) return;
  gpio_set_level(static_cast<gpio_num_t>(pin), value);
}

/// Reads a GPIO input level, matching Arduino's digitalRead().
inline int digitalRead(int pin) {
  if (pin < 0) return LOW;
  return gpio_get_level(static_cast<gpio_num_t>(pin));
}

// ---------------------------------------------------------------------
// SPI
// ---------------------------------------------------------------------

constexpr int MSBFIRST = 0;
constexpr int LSBFIRST = 1;
constexpr int SPI_MODE0 = 0;
constexpr int SPI_MODE1 = 1;
constexpr int SPI_MODE2 = 2;
constexpr int SPI_MODE3 = 3;

/// Matches Arduino's SPISettings(clockHz, bitOrder, dataMode) - carries
/// per-transaction bus parameters into SPIClass::beginTransaction().
struct SPISettings {
  uint32_t clock;
  uint8_t bitOrder;
  uint8_t dataMode;

  SPISettings() : clock(1000000), bitOrder(MSBFIRST), dataMode(SPI_MODE0) {}
  SPISettings(uint32_t clockHz, uint8_t order, uint8_t mode)
      : clock(clockHz), bitOrder(order), dataMode(mode) {}
};

/// ESP-IDF driver/spi_master.h-backed stand-in for Arduino's SPIClass,
/// covering exactly what this library's SPI-based display/touch drivers
/// call: beginTransaction()/endTransaction() and both transfer()
/// overloads (single byte, and in-place full-duplex buffer). Chip select
/// is NOT driven by this class - callers here always toggle their own CS
/// pin via digitalWrite() around each transfer, the same division of
/// responsibility Arduino's own SPIClass uses - so the underlying
/// esp_lcd/spi_master device is configured with no CS pin of its own
/// (spics_io_num = -1).
class SPIClass {
 public:
  SPIClass() = default;
  explicit SPIClass(spi_host_device_t host) : host_(host) {}

  /// Initializes the SPI bus pins. Matches arduino-esp32's
  /// SPIClass::begin(sck, miso, mosi, ss) - `ss` is accepted for
  /// signature compatibility but unused (see class comment: CS is always
  /// caller-driven here). Safe to call more than once.
  bool begin(int sckPin, int misoPin = -1, int mosiPin = -1, int ssPin = -1) {
    (void)ssPin;
    if (busInitialized_) return true;
    spi_bus_config_t busCfg = {};
    busCfg.sclk_io_num = sckPin;
    busCfg.miso_io_num = misoPin;
    busCfg.mosi_io_num = mosiPin;
    busCfg.quadwp_io_num = -1;
    busCfg.quadhd_io_num = -1;
    busCfg.max_transfer_sz = 4096;
    if (spi_bus_initialize(host_, &busCfg, SPI_DMA_CH_AUTO) != ESP_OK) {
      return false;
    }
    busInitialized_ = true;
    return true;
  }

  /// Attaches (or reattaches, if the settings changed) an SPI device with
  /// the given clock/mode and begins a transaction. Every transfer() in
  /// this library happens between a beginTransaction()/endTransaction()
  /// pair, matching the real SPIClass contract.
  void beginTransaction(SPISettings settings) {
    if (device_ != nullptr &&
        (settings.clock != lastClock_ || settings.dataMode != lastMode_)) {
      spi_bus_remove_device(device_);
      device_ = nullptr;
    }
    if (device_ == nullptr) {
      spi_device_interface_config_t devCfg = {};
      devCfg.clock_speed_hz = static_cast<int>(settings.clock);
      devCfg.mode = settings.dataMode;
      devCfg.spics_io_num = -1;  // caller drives CS itself - see class comment
      devCfg.queue_size = 1;
      // Only MSBFIRST is ever requested by this library's own drivers;
      // best-effort LSB-first support for any other caller.
      devCfg.flags = (settings.bitOrder == LSBFIRST)
                         ? (SPI_DEVICE_TXBIT_LSBFIRST | SPI_DEVICE_RXBIT_LSBFIRST)
                         : 0;
      if (spi_bus_add_device(host_, &devCfg, &device_) != ESP_OK) {
        device_ = nullptr;
        return;
      }
      lastClock_ = settings.clock;
      lastMode_ = settings.dataMode;
    }
  }

  /// No-op: the underlying device stays attached for reuse across
  /// transactions (see beginTransaction()) - nothing needs releasing per
  /// transaction with the spi_master driver.
  void endTransaction() {}

  /// Full-duplex single-byte transfer, matching Arduino's
  /// SPIClass::transfer(uint8_t).
  uint8_t transfer(uint8_t data) {
    if (device_ == nullptr) return 0;
    uint8_t rx = 0;
    spi_transaction_t t = {};
    t.length = 8;
    t.tx_buffer = &data;
    t.rx_buffer = &rx;
    spi_device_transmit(device_, &t);
    return rx;
  }

  /// In-place full-duplex bulk transfer, matching Arduino's
  /// SPIClass::transfer(void*, size_t) - `buf` is both the data sent and
  /// (overwritten with) the data received.
  void transfer(void* buf, size_t count) {
    if (device_ == nullptr || buf == nullptr || count == 0) return;
    spi_transaction_t t = {};
    t.length = count * 8;
    t.tx_buffer = buf;
    t.rx_buffer = buf;
    spi_device_transmit(device_, &t);
  }

 private:
  spi_host_device_t host_ = SPI2_HOST;
  spi_device_handle_t device_ = nullptr;
  uint32_t lastClock_ = 0;
  uint8_t lastMode_ = 0xFF;
  bool busInitialized_ = false;
};

/// Global default instance, matching Arduino's bare `SPI` object (used as
/// e.g. TouchDriverXPT2046's default constructor argument).
inline SPIClass SPI;

// ---------------------------------------------------------------------
// Wire (I2C)
// ---------------------------------------------------------------------

/// Legacy driver/i2c.h-backed stand-in for Arduino's TwoWire, covering
/// exactly what this library's I2C touch drivers call: beginTransmission/
/// write/endTransmission/requestFrom/available/read. endTransmission(false)
/// defers the actual bus write so a following requestFrom() can chain it
/// into one repeated-start transaction (write register address, repeated
/// start, read data) instead of two separate stop/start transactions -
/// several of this library's touch controllers document that this
/// matters for reliable register reads.
class TwoWire {
 public:
  TwoWire() = default;
  explicit TwoWire(i2c_port_t port) : port_(port) {}

  /// Initializes the I2C bus pins/speed. Matches arduino-esp32's
  /// TwoWire::begin(sda, scl, freq). Safe to call more than once.
  bool begin(int sdaPin, int sclPin, uint32_t freqHz = 100000) {
    if (installed_) return true;
    i2c_config_t cfg = {};
    cfg.mode = I2C_MODE_MASTER;
    cfg.sda_io_num = sdaPin;
    cfg.scl_io_num = sclPin;
    cfg.sda_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.scl_pullup_en = GPIO_PULLUP_ENABLE;
    cfg.master.clk_speed = freqHz;
    if (i2c_param_config(port_, &cfg) != ESP_OK) return false;
    if (i2c_driver_install(port_, I2C_MODE_MASTER, 0, 0, 0) != ESP_OK) {
      return false;
    }
    installed_ = true;
    return true;
  }

  /// Starts buffering a write to `address` - matches
  /// TwoWire::beginTransmission().
  void beginTransmission(uint8_t address) {
    address_ = address;
    txLen_ = 0;
    pendingRestart_ = false;
  }

  /// Buffers one byte for the next endTransmission() - matches
  /// TwoWire::write(uint8_t). Silently drops bytes past the small fixed
  /// buffer (this library's own drivers write at most a few register-
  /// address bytes per transaction).
  void write(uint8_t value) {
    if (txLen_ < sizeof(txBuf_)) txBuf_[txLen_++] = value;
  }

  /// Sends the buffered write. `sendStop = false` defers the actual I2C
  /// transaction to the following requestFrom() call, so it becomes a
  /// single repeated-start write-then-read - see class comment. Returns
  /// 0 on success, matching Arduino's TwoWire::endTransmission() (which
  /// also uses 0 for success).
  uint8_t endTransmission(bool sendStop = true) {
    if (!sendStop) {
      pendingRestart_ = true;
      return 0;
    }
    const esp_err_t err = runWriteOnly();
    return (err == ESP_OK) ? 0 : 4;
  }

  /// Reads up to `length` bytes from `address` into the internal receive
  /// buffer - matches TwoWire::requestFrom(). If a prior
  /// endTransmission(false) is pending, folds it into this call as one
  /// repeated-start write-then-read transaction; otherwise this is a
  /// plain read. Returns the number of bytes actually read (0 on error).
  size_t requestFrom(int address, int length) {
    length = clampLength(length);
    esp_err_t err;
    if (pendingRestart_) {
      err = runWriteThenRead(static_cast<uint8_t>(address), length);
      pendingRestart_ = false;
    } else {
      err = runReadOnly(static_cast<uint8_t>(address), length);
    }
    rxLen_ = (err == ESP_OK) ? static_cast<size_t>(length) : 0;
    rxPos_ = 0;
    return rxLen_;
  }

  /// Bytes still unread from the last requestFrom() - matches
  /// TwoWire::available().
  int available() const { return static_cast<int>(rxLen_ - rxPos_); }

  /// Reads one byte from the last requestFrom(), or -1 if none remain -
  /// matches TwoWire::read().
  int read() {
    if (rxPos_ >= rxLen_) return -1;
    return rxBuf_[rxPos_++];
  }

 private:
  static constexpr size_t kBufSize = 8;
  static constexpr TickType_t kTimeout = pdMS_TO_TICKS(1000);

  i2c_port_t port_ = I2C_NUM_0;
  uint8_t address_ = 0;
  uint8_t txBuf_[kBufSize] = {};
  size_t txLen_ = 0;
  bool pendingRestart_ = false;
  uint8_t rxBuf_[kBufSize] = {};
  size_t rxLen_ = 0;
  size_t rxPos_ = 0;
  bool installed_ = false;

  static int clampLength(int length) {
    if (length <= 0) return 0;
    if (static_cast<size_t>(length) > kBufSize) return static_cast<int>(kBufSize);
    return length;
  }

  void queueReadBytes(i2c_cmd_handle_t cmd, int length) {
    if (length > 1) {
      i2c_master_read(cmd, rxBuf_, length - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, rxBuf_ + (length - 1), I2C_MASTER_NACK);
  }

  esp_err_t runWriteOnly() {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(
        cmd, static_cast<uint8_t>((address_ << 1) | I2C_MASTER_WRITE), true);
    if (txLen_ > 0) i2c_master_write(cmd, txBuf_, txLen_, true);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(port_, cmd, kTimeout);
    i2c_cmd_link_delete(cmd);
    txLen_ = 0;
    return err;
  }

  esp_err_t runWriteThenRead(uint8_t address, int length) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(
        cmd, static_cast<uint8_t>((address_ << 1) | I2C_MASTER_WRITE), true);
    if (txLen_ > 0) i2c_master_write(cmd, txBuf_, txLen_, true);
    i2c_master_start(cmd);  // repeated start
    i2c_master_write_byte(
        cmd, static_cast<uint8_t>((address << 1) | I2C_MASTER_READ), true);
    queueReadBytes(cmd, length);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(port_, cmd, kTimeout);
    i2c_cmd_link_delete(cmd);
    txLen_ = 0;
    return err;
  }

  esp_err_t runReadOnly(uint8_t address, int length) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(
        cmd, static_cast<uint8_t>((address << 1) | I2C_MASTER_READ), true);
    queueReadBytes(cmd, length);
    i2c_master_stop(cmd);
    const esp_err_t err = i2c_master_cmd_begin(port_, cmd, kTimeout);
    i2c_cmd_link_delete(cmd);
    return err;
  }
};

/// Global default instance, matching Arduino's bare `Wire` object (used
/// as e.g. TouchDriverFT6236's default constructor argument).
inline TwoWire Wire;

}  // namespace tinygpu
