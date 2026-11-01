#include "hardware.h"

#include <cstdio>
#include <thread>

namespace wb {

const char* toString(DeviceKind k) {
    switch (k) {
        case DeviceKind::Microcontroller: return "microcontroller";
        case DeviceKind::Fpga: return "fpga";
        case DeviceKind::JtagAdapter: return "jtag_adapter";
    }
    return "?";
}

const char* toString(LinkState s) {
    switch (s) {
        case LinkState::Disconnected: return "disconnected";
        case LinkState::Connected: return "connected";
        case LinkState::Error: return "error";
    }
    return "?";
}

const char* toString(Fault f) {
    switch (f) {
        case Fault::None: return "none";
        case Fault::TimeoutOnce: return "timeout_once";
        case Fault::TimeoutAlways: return "timeout_always";
    }
    return "?";
}

namespace {
std::string fmt(double v, int decimals) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.*f", decimals, v);
    return b;
}
}  // namespace

// ---------------- MockAdapterBase ----------------

MockAdapterBase::MockAdapterBase(DeviceInfo info, std::uint64_t seed, int handshakeMs)
    : info_(std::move(info)), rng_(seed), handshakeMs_(handshakeMs) {}

double MockAdapterBase::noise(double amplitude) {
    const double u = static_cast<double>(rng_() >> 11) * (1.0 / 9007199254740992.0);
    return (2.0 * u - 1.0) * amplitude;
}

bool MockAdapterBase::consumeTimeoutFault() {
    if (fault_ == Fault::TimeoutOnce) {
        fault_ = Fault::None;  // one-shot: the next attempt succeeds
        return true;
    }
    return fault_ == Fault::TimeoutAlways;
}

OpResult MockAdapterBase::connect(int timeoutMs) {
    if (state_ == LinkState::Connected) return {};
    if (consumeTimeoutFault()) {
        // Simulate waiting for a handshake that never arrives.
        std::this_thread::sleep_for(std::chrono::milliseconds(timeoutMs));
        state_ = LinkState::Error;
        return OpResult::fail("Handshake timed out after " + std::to_string(timeoutMs) +
                              " ms (simulated). Check cable/power, then retry.");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(handshakeMs_));
    state_ = LinkState::Connected;
    return {};
}

void MockAdapterBase::disconnect() { state_ = LinkState::Disconnected; }

OpResult MockAdapterBase::readTelemetry(Telemetry& out) {
    if (state_ != LinkState::Connected)
        return OpResult::fail(std::string("Device is ") + toString(state_) + "; connect it before reading telemetry.");
    if (consumeTimeoutFault()) {
        state_ = LinkState::Error;
        return OpResult::fail("Telemetry read timed out (simulated). The link was marked as error; reconnect to recover.");
    }
    out = Telemetry{};
    out.deviceId = info_.id;
    out.sequence = ++seq_;
    fillTelemetry(out);
    return {};
}

// ---------------- Concrete mocks ----------------

MockMicrocontroller::MockMicrocontroller(std::string id)
    : MockAdapterBase({std::move(id), DeviceKind::Microcontroller,
                       "Cortex-M4 class MCU board (simulated)",
                       "USB CDC-ACM virtual COM port, 115200 8N1 framing",
                       "3.3 V I/O (VDD), 5 V USB input",
                       {"uart_telemetry", "gpio_8", "adc_12bit_4ch", "swd_or_jtag_debug_port"},
                       true},
                      0x4d43551, 20) {}

void MockMicrocontroller::fillTelemetry(Telemetry& t) {
    t.fields = {{"die_temp_C", fmt(31.5 + noise(0.4), 2)},
                {"vdd_V", fmt(3.30 + noise(0.01), 3)},
                {"adc0_raw", std::to_string(2048 + static_cast<int>(noise(20)))},
                {"note", "simulated frame"}};
}

MockFpga::MockFpga(std::string id)
    : MockAdapterBase({std::move(id), DeviceKind::Fpga,
                       "Artix-7 class FPGA board (simulated)",
                       "USB-UART bridge for status; bitstream via JTAG adapter",
                       "1.0 V VCCINT, 1.8 V VCCAUX, 3.3 V / 1.8 V I/O banks",
                       {"status_registers", "xadc_temperature", "uart_status", "jtag_tap"},
                       true},
                      0xF96A, 35) {}

void MockFpga::fillTelemetry(Telemetry& t) {
    t.fields = {{"core_temp_C", fmt(42.0 + noise(0.6), 2)},
                {"vccint_V", fmt(1.000 + noise(0.005), 3)},
                {"done_pin", "1"},
                {"user_reg0", "0xCAFE0001"},
                {"note", "simulated frame"}};
}

MockJtagAdapter::MockJtagAdapter(std::string id)
    : MockAdapterBase({std::move(id), DeviceKind::JtagAdapter,
                       "FT2232H-class JTAG debug probe (simulated)",
                       "USB 2.0 High-Speed vendor class (driven by OpenOCD in a real setup)",
                       "Follows target VTREF (1.8-3.3 V); does not power the target",
                       {"jtag_scan_chain", "idcode_read", "boundary_scan", "tck_up_to_30MHz",
                        "owns TCK/TMS/TDI/TDO/nTRST/nSRST while attached"},
                       true},
                      0x17A6, 15) {}

void MockJtagAdapter::fillTelemetry(Telemetry& t) {
    t.fields = {{"vtref_V", fmt(3.30 + noise(0.02), 2)},
                {"tck_kHz", "1000"},
                {"chain_length", "2"},
                {"idcode_0", "0x4BA00477 (Arm debug port, simulated)"},
                {"idcode_1", "0x0362D093 (XC7A35T-class FPGA TAP, simulated)"},
                {"note", "simulated scan"}};
}

// ---------------- DeviceManager ----------------

DeviceManager::DeviceManager() : start_(std::chrono::steady_clock::now()) {}

double DeviceManager::now() const {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
}

void DeviceManager::log(const std::string& id, const std::string& level, const std::string& msg) {
    events_.push_back({now(), id, level, msg});
    if (events_.size() > 500) events_.erase(events_.begin());
}

IDeviceAdapter* DeviceManager::find(const std::string& id) const {
    const auto it = devices_.find(id);
    return it == devices_.end() ? nullptr : it->second.get();
}

std::vector<DeviceInfo> DeviceManager::discover() {
    std::lock_guard<std::mutex> lk(mu_);
    auto add = [&](std::unique_ptr<IDeviceAdapter> d) {
        const std::string id = d->info().id;
        if (!devices_.count(id)) devices_.emplace(id, std::move(d));
    };
    add(std::make_unique<MockMicrocontroller>("mcu0"));
    add(std::make_unique<MockFpga>("fpga0"));
    add(std::make_unique<MockJtagAdapter>("jtag0"));
    log("-", "info", "Simulated discovery found 3 devices (mcu0, fpga0, jtag0).");
    std::vector<DeviceInfo> out;
    for (const auto& kv : devices_) out.push_back(kv.second->info());
    return out;
}

std::vector<std::string> DeviceManager::ids() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<std::string> out;
    for (const auto& kv : devices_) out.push_back(kv.first);
    return out;
}

bool DeviceManager::has(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    return find(id) != nullptr;
}

LinkState DeviceManager::state(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    return d ? d->state() : LinkState::Disconnected;
}

Fault DeviceManager::fault(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    return d ? d->fault() : Fault::None;
}

DeviceInfo DeviceManager::info(const std::string& id) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    return d ? d->info() : DeviceInfo{};
}

OpResult DeviceManager::connect(const std::string& id, int attempts, int timeoutMs) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    if (!d) return OpResult::fail("Unknown device '" + id + "'. Run discovery first.");
    if (attempts < 1 || attempts > 10) return OpResult::fail("Attempts must be between 1 and 10.");
    if (timeoutMs < 10 || timeoutMs > 5000) return OpResult::fail("Timeout must be between 10 and 5000 ms.");
    OpResult r;
    for (int a = 1; a <= attempts; ++a) {
        r = d->connect(timeoutMs);
        if (r.ok) {
            log(id, "info", "Connected" + (a > 1 ? " on attempt " + std::to_string(a) + " (recovered)" : std::string()) + ".");
            return r;
        }
        log(id, a < attempts ? "warn" : "error", "Attempt " + std::to_string(a) + "/" + std::to_string(attempts) + ": " + r.error);
    }
    return r;
}

OpResult DeviceManager::disconnect(const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    if (!d) return OpResult::fail("Unknown device '" + id + "'.");
    d->disconnect();
    log(id, "info", "Disconnected.");
    return {};
}

OpResult DeviceManager::telemetry(const std::string& id, Telemetry& out) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    if (!d) return OpResult::fail("Unknown device '" + id + "'.");
    const OpResult r = d->readTelemetry(out);
    out.hostTime_s = now();
    if (!r.ok) log(id, "error", r.error);
    else log(id, "info", "Telemetry frame #" + std::to_string(out.sequence) + " received.");
    return r;
}

OpResult DeviceManager::setFault(const std::string& id, Fault f) {
    std::lock_guard<std::mutex> lk(mu_);
    auto* d = find(id);
    if (!d) return OpResult::fail("Unknown device '" + id + "'.");
    d->setFault(f);
    log(id, "warn", std::string("Fault injection set to '") + toString(f) + "' (simulated).");
    return {};
}

std::vector<DeviceEvent> DeviceManager::events() const {
    std::lock_guard<std::mutex> lk(mu_);
    return events_;
}

}  // namespace wb
