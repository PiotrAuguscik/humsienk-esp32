#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace humsienk {
constexpr std::size_t kMaxFrame = 512;
constexpr std::size_t kMaxCells = 32;
constexpr std::size_t kMaxTemperatures = 32;
enum class Error { Ok, InvalidArgument, NotConnected, Busy, Transport,
                   Timeout, Overflow, Incomplete, InvalidFrame, BadCrc,
                   WrongResponse, UnsupportedVersion, DeviceError, InvalidData };
const char* errorName(Error error);
enum class ReadPoint : std::uint16_t {
  Product = 0x92, CollectionBoards = 0x1e, Analog = 0x8c, Warnings = 0x8d
};
enum class AddressType : std::uint8_t { Public = 0, Random = 1 };
struct Target { char address[18]{}; AddressType type = AddressType::Random; };
bool validTarget(const Target& target);
struct Candidate { Target target{}; char name[32]{}; int rssi = 0; };

// Owned bytes; safe after callback return or another request. Only decode
// frames accepted by validateResponse. Decoders revalidate as their boundary.
struct Response { std::array<std::uint8_t, kMaxFrame> bytes{}; std::size_t size = 0; };
struct Product { char firmware[21]{}; char manufacturer[21]{}; char serial[21]{}; };
struct Experimental {
  bool present = false;
  std::uint16_t sohPercent = 0;
  double cumulativeCapacityAh = 0;
  std::uint32_t remainingMinutes = 0;
  double balanceCurrentA = 0;
};
struct Snapshot {
  std::uint8_t cellCount = 0;
  std::array<std::uint16_t, kMaxCells> cellMillivolts{};
  std::uint8_t temperatureCount = 0;
  // Order from APK: MOS, PCB, then cell sensors; physical roles unconfirmed.
  std::array<double, kMaxTemperatures> temperaturesC{};
  double voltageV = 0, currentA = 0;
  double remainingCapacityAh = 0, totalCapacityAh = 0, designCapacityAh = 0;
  std::uint16_t cycles = 0, socPercent = 0;
  Experimental experimental{};
  // Retains unknown extension bytes as well, without inventing field names.
  Response raw{};
  double minCellV() const;
  double maxCellV() const;
};

std::uint16_t crc16(const std::uint8_t* data, std::size_t size);
// Fixed FC03 allowlist; extended form is permitted only for Analog.
Error buildRead(ReadPoint point, bool extended, std::uint8_t* out,
                std::size_t capacity, std::size_t& size);
Error validateResponse(const Response& response, ReadPoint expected);
Error decodeSnapshot(const Response& response, Snapshot& out);
Error decodeProduct(const Response& response, Product& out);

// Transport owns physical link and callback buffering. open() must subscribe
// FFF1 before returning; write() is acknowledged FFFA (auth) or FFF2 (read).
// All operations are synchronous, serialized by the caller, never callbacks.
class Transport {
 public:
  virtual ~Transport() = default;
  virtual Error open(const Target&) = 0;
  virtual void close() = 0;
  virtual bool connected() const = 0;
  virtual Error write(bool auth, const std::uint8_t*, std::size_t) = 0;
  virtual Error readAuthStatus(std::uint8_t& status) = 0;
  virtual Error receive(std::uint8_t*, std::size_t, std::size_t&,
                        std::uint32_t timeoutMs) = 0;
  virtual void clearNotifications() = 0;
  virtual void wait(std::uint32_t ms) = 0;
  virtual std::uint32_t nowMs() const = 0;
};

// No background task/retries/storage. A failed transaction closes the link so
// a delayed reply cannot be consumed by the next transaction on that session.
class Session {
 public:
  explicit Session(Transport& transport) : transport_(transport) {}
  ~Session() { disconnect(); }  // transport must outlive this session
  Session(const Session&) = delete;
  Session& operator=(const Session&) = delete;
  Error connect(const Target& target);
  void disconnect();
  bool connected() const { return ready_ && transport_.connected(); }
  Error readSnapshot(Snapshot& out);
  Error readProduct(Product& out);
  Error readRaw(ReadPoint point, Response& out);
 private:
  Error exchange(ReadPoint point, bool extended, Response& out);
  Transport& transport_;
  bool ready_ = false;
  std::uint8_t version_ = 0;
};
}  // namespace humsienk
