// Mock hardware adapters for the embedded-integration view.
//
// EVERYTHING HERE IS SIMULATED. No USB, serial or JTAG traffic is generated. The adapters
// model the *software contract* a real driver would satisfy (discovery, capability report,
// connect/disconnect, telemetry, timeouts) so the UI and recovery logic can be built and
// tested before hardware exists.
#pragma once
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <vector>

namespace wb {

enum class DeviceKind { Microcontroller, Fpga, JtagAdapter };
enum class LinkState { Disconnected, Connected, Error };
enum class Fault { None, TimeoutOnce, TimeoutAlways };

const char* toString(DeviceKind k);
const char* toString(LinkState s);
const char* toString(Fault f);

struct DeviceInfo {
    std::string id;
    DeviceKind kind = DeviceKind::Microcontroller;
    std::string model;       // device class the mock imitates
    std::string transport;   // host link a real device would use
    std::string ioVoltage;
    std::vector<std::string> capabilities;
    bool simulated = true;
};

struct Telemetry {
    std::string deviceId;
    std::uint64_t sequence = 0;
    double hostTime_s = 0;  // seconds since manager creation
    std::vector<std::pair<std::string, std::string>> fields;
};

struct OpResult {
    bool ok = true;
    std::string error;  // actionable message when !ok
    static OpResult fail(std::string e) { return {false, std::move(e)}; }
};

class IDeviceAdapter {
public:
    virtual ~IDeviceAdapter() = default;
    virtual const DeviceInfo& info() const = 0;
    virtual LinkState state() const = 0;
    virtual OpResult connect(int timeoutMs) = 0;
    virtual void disconnect() = 0;
    virtual OpResult readTelemetry(Telemetry& out) = 0;
    virtual void setFault(Fault f) = 0;
    virtual Fault fault() const = 0;
};

// Shared behaviour of all mocks: state machine, fault injection, deterministic noise.
class MockAdapterBase : public IDeviceAdapter {
public:
    MockAdapterBase(DeviceInfo info, std::uint64_t seed, int handshakeMs);
    const DeviceInfo& info() const override { return info_; }
    LinkState state() const override { return state_; }
    OpResult connect(int timeoutMs) override;
    void disconnect() override;
    OpResult readTelemetry(Telemetry& out) override;
    void setFault(Fault f) override { fault_ = f; }
    Fault fault() const override { return fault_; }

protected:
    virtual void fillTelemetry(Telemetry& t) = 0;
    double noise(double amplitude);  // uniform in [-amplitude, amplitude]

private:
    bool consumeTimeoutFault();

    DeviceInfo info_;
    LinkState state_ = LinkState::Disconnected;
    Fault fault_ = Fault::None;
    std::mt19937_64 rng_;
    int handshakeMs_;
    std::uint64_t seq_ = 0;
};

class MockMicrocontroller : public MockAdapterBase {
public:
    explicit MockMicrocontroller(std::string id);
protected:
    void fillTelemetry(Telemetry& t) override;
};

class MockFpga : public MockAdapterBase {
public:
    explicit MockFpga(std::string id);
protected:
    void fillTelemetry(Telemetry& t) override;
};

class MockJtagAdapter : public MockAdapterBase {
public:
    explicit MockJtagAdapter(std::string id);
protected:
    void fillTelemetry(Telemetry& t) override;
};

struct DeviceEvent {
    double hostTime_s;
    std::string deviceId;
    std::string level;  // info | warn | error
    std::string message;
};

// Thread-safe facade used by the HTTP layer.
class DeviceManager {
public:
    DeviceManager();
    // Simulated bus enumeration: (re)creates the three mock devices if absent.
    std::vector<DeviceInfo> discover();
    std::vector<std::string> ids() const;
    bool has(const std::string& id) const;
    LinkState state(const std::string& id) const;
    Fault fault(const std::string& id) const;
    DeviceInfo info(const std::string& id) const;
    // Retries up to `attempts` times; each failure is logged so recovery is visible.
    OpResult connect(const std::string& id, int attempts = 1, int timeoutMs = 200);
    OpResult disconnect(const std::string& id);
    OpResult telemetry(const std::string& id, Telemetry& out);
    OpResult setFault(const std::string& id, Fault f);
    std::vector<DeviceEvent> events() const;
    double now() const;

private:
    void log(const std::string& id, const std::string& level, const std::string& msg);
    IDeviceAdapter* find(const std::string& id) const;

    mutable std::mutex mu_;
    std::map<std::string, std::unique_ptr<IDeviceAdapter>> devices_;
    std::vector<DeviceEvent> events_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace wb
